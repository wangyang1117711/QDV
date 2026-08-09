#include "BranchControlTool.h"
#include "BranchNode.h"
#include "Core/Logger.h"
#include <QSet>
#include <QRegularExpression>
#include <QStringList>

using namespace QDV;

bool BranchControlTool::configure(const QJsonObject& params) {
    if (params.isEmpty()) {
        return false;
    }

    if (m_branchNode) {
        delete m_branchNode;
    }

    // P1-A12 修复：OperatorDescriptors 元数据定义的 param name 是 conditionOp/conditionValue/source，
    // 但 BranchNode::deserialize 期望的 JSON key 是 op/value/source。
    // 之前直接转发导致 conditionOp 永远为空，分支节点永远 invalid。
    // 这里做 key 映射，将元数据参数名转为 BranchNode 期望的 key。
    QJsonObject mapped = params;
    if (params.contains("conditionOp") && !params.contains("op")) {
        mapped["op"] = params.value("conditionOp");
        mapped.remove("conditionOp");
    }
    if (params.contains("conditionValue") && !params.contains("value")) {
        mapped["value"] = params.value("conditionValue");
        mapped.remove("conditionValue");
    }
    // source 参数名在元数据和 deserialize 中一致（都是 "source"），无需映射
    // trueBranch/falseBranch 也一致

    m_branchNode = new BranchNode();
    m_branchNode->deserialize(mapped);

    // v2.7.0：多分支选择
    if (params.contains("switchValue")) m_switchValue = params["switchValue"].toString();
    if (params.contains("branchCases")) m_branchCases = params["branchCases"].toString();
    // v2.7.0：逻辑运算输入
    if (params.contains("logicInputs")) m_logicInputs = params["logicInputs"].toString();

    return m_branchNode->isValid();
}

bool BranchControlTool::execute(const cv::Mat& input, ToolResult& result) {
    m_inputImage = input.clone();
    result.overlayImage = input.clone();
    result.ok = true;
    result.elapsedMs = 0;

    // P1-4c 扩展：若配置了有效 branchNode，执行比较并输出结果
    // 原有行为（仅透传图像）保持不变；新增比较结果写入 ports/data 供下游引用
    if (m_branchNode && m_branchNode->isValid()) {
        const bool conditionMet = evaluateCondition(result);
        result.ports["conditionMet"] = QVariant(conditionMet);
        result.ports["operation"]    = QVariant(m_branchNode->conditionOp);
        result.data["conditionMet"]  = conditionMet;
        result.data["operation"]     = m_branchNode->conditionOp;
        result.data["conditionValue"] = QJsonValue::fromVariant(m_branchNode->conditionValue);
    }

    // v2.7.0：多分支选择和逻辑运算输出
    if (m_switchValue.isEmpty() == false) {
        result.ports["switchValue"] = m_switchValue;
        result.ports["branchCases"] = m_branchCases;
    }
    if (m_logicInputs.isEmpty() == false) {
        result.ports["logicInputs"] = m_logicInputs;
        result.ports["logicOp"] = m_branchNode ? m_branchNode->conditionOp : QString();
    }

    return true;
}

QJsonObject BranchControlTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    if (m_branchNode) {
        QJsonObject branchObj = m_branchNode->serialize();
        for (auto it = branchObj.begin(); it != branchObj.end(); ++it) {
            obj[it.key()] = it.value();
        }
    }
    // v2.7.0：多分支选择 + 逻辑运算参数序列化
    obj["switchValue"]  = m_switchValue;
    obj["branchCases"]  = m_branchCases;
    obj["logicInputs"]  = m_logicInputs;
    return obj;
}

bool BranchControlTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    if (m_branchNode) {
        delete m_branchNode;
    }
    // P1-B8 修复：deserialize 复用 configure 的参数名映射逻辑
    // 之前 deserialize 直接转发给 BranchNode::deserialize(data)，但 data 可能用元数据字段名
    // (conditionOp/conditionValue)，而 BranchNode 期望 op/value，导致反序列化后分支节点 invalid
    QJsonObject mapped = data;
    if (data.contains("conditionOp") && !data.contains("op")) {
        mapped["op"] = data.value("conditionOp");
        mapped.remove("conditionOp");
    }
    if (data.contains("conditionValue") && !data.contains("value")) {
        mapped["value"] = data.value("conditionValue");
        mapped.remove("conditionValue");
    }
    m_branchNode = new BranchNode();
    m_branchNode->deserialize(mapped);

    // v2.7.0：多分支选择 + 逻辑运算参数反序列化
    if (data.contains("switchValue")) m_switchValue = data["switchValue"].toString();
    if (data.contains("branchCases")) m_branchCases = data["branchCases"].toString();
    if (data.contains("logicInputs")) m_logicInputs = data["logicInputs"].toString();

    return m_branchNode->isValid();
}

// =====================================================
// P1-4c 扩展：比较条件评估
// 支持原有算子（==/!=/>/>=/</<=/ok/ng，向后兼容）+
// 新增算子（ge/le/ne/contains/startswith/endswith/in）
//
// 数据来源（左值）：
//   优先 result.data["value"]，其次 result.data["count"]，再次 result.score
//   字符串算子用左值的字符串形式；数值算子用左值的数值形式
// =====================================================
bool BranchControlTool::evaluateCondition(const ToolResult& result) const {
    if (!m_branchNode || !m_branchNode->isValid()) return false;

    const QString op = m_branchNode->conditionOp;
    const QVariant& condVal = m_branchNode->conditionValue;

    // ---- 原有算子：直接交给 BranchNode::evaluate（向后兼容，不改语义）----
    // 包括 == / != / > / >= / < / <= / ok / ng
    static const QSet<QString> legacyOps = {
        "==", "!=", ">", ">=", "<", "<=", "ok", "ng"
    };
    if (legacyOps.contains(op)) {
        return m_branchNode->evaluate(result);
    }

    // v2.7.0：逻辑运算 AND/OR/NOT/XOR
    if (m_branchNode && m_branchNode->conditionOp == "and") {
        // AND：所有输入条件都为 true
        const QStringList inputs = parseLogicInputs(m_logicInputs);
        for (const auto& input : inputs) {
            if (!evaluateLogicInput(input, result)) return false;
        }
        return true;
    }
    if (m_branchNode && m_branchNode->conditionOp == "or") {
        // OR：任意输入条件为 true
        const QStringList inputs = parseLogicInputs(m_logicInputs);
        for (const auto& input : inputs) {
            if (evaluateLogicInput(input, result)) return true;
        }
        return false;
    }
    if (m_branchNode && m_branchNode->conditionOp == "not") {
        // NOT：取反第一个输入条件
        const QStringList inputs = parseLogicInputs(m_logicInputs);
        if (inputs.isEmpty()) return true;
        return !evaluateLogicInput(inputs.first(), result);
    }
    if (m_branchNode && m_branchNode->conditionOp == "xor") {
        // XOR：两个输入条件不一致时为 true
        const QStringList inputs = parseLogicInputs(m_logicInputs);
        if (inputs.size() < 2) return false;
        const bool a = evaluateLogicInput(inputs[0], result);
        const bool b = evaluateLogicInput(inputs[1], result);
        return a != b;
    }
    // v2.7.0：多分支选择 switch
    if (m_branchNode && m_branchNode->conditionOp == "switch") {
        // switchValue 匹配 branchCases 中的 case 索引
        // 实际分支路由由 ToolChainExecutor 消费 conditionMet + switchValue
        // 这里仅返回是否匹配成功
        return !m_switchValue.isEmpty() && !m_branchCases.isEmpty();
    }

    // ---- 提取左值（数值 + 字符串两种形式）----
    double leftNum = result.score;
    QString leftStr;
    if (result.data.contains("value")) {
        const QVariant v = result.data.value("value").toVariant();
        leftStr = v.toString();
        bool ok = false;
        const double d = v.toDouble(&ok);
        if (ok) leftNum = d;
    } else if (result.data.contains("count")) {
        leftNum = result.data.value("count").toDouble();
        leftStr = QString::number(leftNum);
    } else {
        leftStr = QString::number(leftNum);
    }

    // ---- P1-4c 新增：ge/le/ne（>=/<=/!= 的语义别名）----
    if (op == "ge") {
        const double th = condVal.toDouble();
        return leftNum >= th;
    }
    if (op == "le") {
        const double th = condVal.toDouble();
        return leftNum <= th;
    }
    if (op == "ne") {
        // 数值优先；无法解析为数值时回退字符串比较
        bool ok = false;
        const double th = condVal.toDouble(&ok);
        if (ok) {
            return qAbs(leftNum - th) >= 1e-9;
        }
        return leftStr != condVal.toString();
    }

    // ---- P1-4c 新增：字符串算子 ----
    if (op == "contains") {
        return leftStr.contains(condVal.toString());
    }
    if (op == "startswith") {
        return leftStr.startsWith(condVal.toString());
    }
    if (op == "endswith") {
        return leftStr.endsWith(condVal.toString());
    }

    // ---- P1-4c 新增：in（左值在 conditionValue 列表中）----
    // conditionValue 为逗号/分号分隔的字符串，如 "a,b,c" 或 "1;2;3"
    if (op == "in") {
        const QStringList list = condVal.toString().split(
            QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts);
        for (const QString& item : list) {
            if (leftStr == item.trimmed()) return true;
        }
        return false;
    }

    // ---- 未知算子：回退 BranchNode::evaluate（保持兜底语义）----
    Logger::warn(QString("BranchControlTool: 未知算子 '%1'，回退 BranchNode::evaluate").arg(op));
    return m_branchNode->evaluate(result);
}

// =====================================================
// 端口声明（P1-3 typed ports）
// =====================================================
QList<PortDescriptor> BranchControlTool::outputPorts() const {
    return {
        PortDescriptor{ "conditionMet", "条件成立", PortType::Bool,   PortDirection::Out, "比较条件是否成立" },
        PortDescriptor{ "operation",    "算子",     PortType::String, PortDirection::Out, "比较算子名" },
    };
}

QList<PortDescriptor> BranchControlTool::inputPorts() const {
    return {
        PortDescriptor{ "image", "图像", PortType::Image, PortDirection::In, "输入图像（透传到 overlay）" },
    };
}

void BranchControlTool::setBranchNode(BranchNode* node) {
    if (m_branchNode) {
        delete m_branchNode;
    }
    m_branchNode = node;
}

// =====================================================
// v2.7.0：逻辑运算辅助方法
// =====================================================

/// v2.7.0：解析逻辑运算输入列表
/// 格式 "nodeId1:expectedBool1,nodeId2:expectedBool2"
/// 返回每个输入的字符串（未解析）
QStringList BranchControlTool::parseLogicInputs(const QString& inputs) const {
    return inputs.split(',', Qt::SkipEmptyParts);
}

/// v2.7.0：评估单个逻辑输入
/// input 格式 "nodeId:expectedBool"
/// 从 result 的上游结果中查找 nodeId 的 conditionMet，与 expectedBool 比较
bool BranchControlTool::evaluateLogicInput(const QString& input, const ToolResult& result) const {
    const QStringList parts = input.split(':');
    if (parts.size() < 2) return false;
    const QString nodeId = parts[0].trimmed();
    const QString expected = parts[1].trimmed().toLower();
    const bool expectedBool = (expected == "true" || expected == "1");
    // 从 result.data 中查找上游 conditionMet
    // 注意：实际实现需要访问 ExecutionContext 的 upstreamResults
    // 当前简化：从 result.data["logicResults"] 中查找
    const QJsonObject logicResults = result.data.value("logicResults").toObject();
    if (!logicResults.contains(nodeId)) return false;
    const bool actualBool = logicResults.value(nodeId).toBool();
    return actualBool == expectedBool;
}