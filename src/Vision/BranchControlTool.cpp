#include "BranchControlTool.h"
#include "BranchNode.h"

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
    return m_branchNode->isValid();
}

bool BranchControlTool::execute(const cv::Mat& input, ToolResult& result) {
    m_inputImage = input.clone();
    result.overlayImage = input.clone();
    result.ok = true;
    result.elapsedMs = 0;
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
    return m_branchNode->isValid();
}

void BranchControlTool::setBranchNode(BranchNode* node) {
    if (m_branchNode) {
        delete m_branchNode;
    }
    m_branchNode = node;
}