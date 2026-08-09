#include "VariableTool.h"
#include "Core/Logger.h"
#include <opencv2/core.hpp>
#include <QSet>
#include <cmath>
#include <cstdio>

using namespace QDV;

// =====================================================
// 构造
// =====================================================
VariableTool::VariableTool() {
    m_name = "变量运算";
}

// =====================================================
// 参数配置
// =====================================================
bool VariableTool::configure(const QJsonObject& params) {
    if (params.contains("operation")) {
        const QString v = params["operation"].toString();
        // 仅允许已知运算，非法值回退 add
        static const QSet<QString> validOps = {
            "add", "sub", "mul", "div", "mod", "concat", "format",
            "get", "set", "define", "delete"  // v2.7.0 新增
        };
        if (validOps.contains(v)) {
            m_operation = v;
        } else {
            Logger::warn(QString("VariableTool: operation '%1' 非法，回退为 'add'").arg(v));
            m_operation = "add";
        }
    }
    if (params.contains("operand1"))  m_operand1  = params["operand1"].toString();
    if (params.contains("operand2"))  m_operand2  = params["operand2"].toString();
    if (params.contains("format"))    m_format    = params["format"].toString();
    if (params.contains("outputVar")) m_outputVar = params["outputVar"].toString();
    if (m_outputVar.isEmpty()) m_outputVar = "result";
    m_params = params;
    return true;
}

// =====================================================
// 尝试解析字符串为 double
// 支持普通数字与科学计数法；失败返回 false
// =====================================================
bool VariableTool::tryParseDouble(const QString& s, double& out) {
    bool ok = false;
    // 先去空白
    const QString trimmed = s.trimmed();
    if (trimmed.isEmpty()) return false;
    out = trimmed.toDouble(&ok);
    return ok;
}

// =====================================================
// execute：执行运算
// =====================================================
bool VariableTool::execute(const cv::Mat& input, ToolResult& result) {
    try {
        QVariant resultVar;        // 最终结果（数值或字符串）

        // ---- 数值运算分支 ----
        if (m_operation == "add" || m_operation == "sub" ||
            m_operation == "mul" || m_operation == "div" ||
            m_operation == "mod") {

            double a = 0.0, b = 0.0;
            if (!tryParseDouble(m_operand1, a)) {
                result.ok = false;
                result.data["error"] = QString("operand1 '%1' 不是有效数值").arg(m_operand1);
                Logger::warn(QString("VariableTool: operand1 解析失败: %1").arg(m_operand1));
                if (!input.empty()) result.overlayImage = input.clone();
                return false;
            }
            if (!tryParseDouble(m_operand2, b)) {
                result.ok = false;
                result.data["error"] = QString("operand2 '%1' 不是有效数值").arg(m_operand2);
                Logger::warn(QString("VariableTool: operand2 解析失败: %1").arg(m_operand2));
                if (!input.empty()) result.overlayImage = input.clone();
                return false;
            }

            double val = 0.0;
            if (m_operation == "add") {
                val = a + b;
            } else if (m_operation == "sub") {
                val = a - b;
            } else if (m_operation == "mul") {
                val = a * b;
            } else if (m_operation == "div") {
                // 除零保护
                if (qAbs(b) < 1e-12) {
                    result.ok = false;
                    result.data["error"] = QString("除零错误: %1 / %2").arg(a).arg(b);
                    Logger::warn(QString("VariableTool: 除零错误: %1 / %2").arg(a).arg(b));
                    if (!input.empty()) result.overlayImage = input.clone();
                    return false;
                }
                val = a / b;
            } else if (m_operation == "mod") {
                // 取模除零保护
                if (qAbs(b) < 1e-12) {
                    result.ok = false;
                    result.data["error"] = QString("取模除零错误: %1 %% %2").arg(a).arg(b);
                    Logger::warn(QString("VariableTool: 取模除零错误: %1 %% %2").arg(a).arg(b));
                    if (!input.empty()) result.overlayImage = input.clone();
                    return false;
                }
                // 用 fmod 处理浮点取模（与 C 语义一致）
                val = std::fmod(a, b);
            }

            resultVar = QVariant(val);

        // ---- 字符串拼接分支 ----
        } else if (m_operation == "concat") {
            resultVar = QVariant(m_operand1 + m_operand2);

        // ---- 格式化分支 ----
        } else if (m_operation == "format") {
            // 按 format 模板格式化 operand1（operand2 作为附加参数）
            // 支持 "%.2f" / "%s_%d" / "%d-%s" 等 printf 风格模板
            const std::string fmt = m_format.toUtf8().constData();

            // 判断 operand1 是数值还是字符串
            double d1 = 0.0;
            const bool isNum1 = tryParseDouble(m_operand1, d1);
            double d2 = 0.0;
            const bool isNum2 = tryParseDouble(m_operand2, d2);

            // 缓冲区（足够大，避免溢出；printf 风格格式化一般不会超长）
            char buf[1024] = {0};

            // 根据模板中的占位符类型选择参数
            // 简化策略：模板含 %d → 用 int；含 %f/%e/%g → 用 double；含 %s → 用字符串
            // 这里用一个通用方案：尝试用 (double, double) 格式化，
            // 若模板含 %s 则用 (str, str) 格式化
            if (m_format.contains("%s")) {
                // 字符串格式化
                const std::string s1 = m_operand1.toUtf8().constData();
                const std::string s2 = m_operand2.toUtf8().constData();
                std::snprintf(buf, sizeof(buf), fmt.c_str(), s1.c_str(), s2.c_str());
            } else {
                // 数值格式化（同时支持 1 个或 2 个占位符）
                if (m_format.contains("%d")) {
                    // 含 %d：用 int 参数
                    const int i1 = isNum1 ? static_cast<int>(d1) : 0;
                    const int i2 = isNum2 ? static_cast<int>(d2) : 0;
                    std::snprintf(buf, sizeof(buf), fmt.c_str(), i1, i2);
                } else {
                    // 默认用 double
                    std::snprintf(buf, sizeof(buf), fmt.c_str(), d1, d2);
                }
            }

            resultVar = QVariant(QString::fromUtf8(buf));

        // ---- v2.7.0：get 操作 - 读取全局变量 ----
        } else if (m_operation == "get") {
            if (!m_variableManager) {
                result.ok = false;
                result.data["error"] = "VariableManager 未注入";
                Logger::warn("VariableTool: get 操作失败 - VariableManager 未注入");
                if (!input.empty()) result.overlayImage = input.clone();
                return false;
            }
            const QVariant v = m_variableManager->value(m_operand1);
            if (!v.isValid()) {
                result.ok = false;
                result.data["error"] = QString("变量 '%1' 未定义").arg(m_operand1);
                Logger::warn(QString("VariableTool: get 失败 - 变量 '%1' 未定义").arg(m_operand1));
                if (!input.empty()) result.overlayImage = input.clone();
                return false;
            }
            resultVar = v;

        // ---- v2.7.0：set 操作 - 写入全局变量 ----
        } else if (m_operation == "set") {
            if (!m_variableManager) {
                result.ok = false;
                result.data["error"] = "VariableManager 未注入";
                Logger::warn("VariableTool: set 操作失败 - VariableManager 未注入");
                if (!input.empty()) result.overlayImage = input.clone();
                return false;
            }
            // 尝试数值转换：若 operand2 可解析为 double，则按数值写入；否则按字符串
            QVariant val;
            double d;
            if (tryParseDouble(m_operand2, d)) {
                val = d;
            } else {
                val = m_operand2;
            }
            if (!m_variableManager->setValue(m_operand1, val)) {
                result.ok = false;
                result.data["error"] = QString("setValue 失败: %1（变量不存在或类型不匹配）").arg(m_operand1);
                Logger::warn(QString("VariableTool: setValue 失败 - %1").arg(m_operand1));
                if (!input.empty()) result.overlayImage = input.clone();
                return false;
            }
            resultVar = val;

        // ---- v2.7.0：define 操作 - 创建全局变量 ----
        } else if (m_operation == "define") {
            if (!m_variableManager) {
                result.ok = false;
                result.data["error"] = "VariableManager 未注入";
                Logger::warn("VariableTool: define 操作失败 - VariableManager 未注入");
                if (!input.empty()) result.overlayImage = input.clone();
                return false;
            }
            // operand1=变量名, operand2=类型(int/double/string/bool/roi/region/points), format=初始值
            const QString varName = m_operand1;
            const QString varType = m_operand2.toLower();
            const QString initValue = m_format;  // 复用 format 字段作为初始值
            QVariant defaultValue;
            if (varType == "int") {
                defaultValue = initValue.toInt();
            } else if (varType == "double") {
                defaultValue = initValue.toDouble();
            } else if (varType == "bool") {
                defaultValue = (initValue.toLower() == "true" || initValue == "1");
            } else if (varType == "roi") {
                // ROI 默认值：{x,y,w,h}（对齐 VariableManager::fromRoi 语义）
                QVariantMap m;
                m["x"] = 0.0;
                m["y"] = 0.0;
                m["w"] = 100.0;
                m["h"] = 100.0;
                defaultValue = m;
            } else if (varType == "region" || varType == "points") {
                defaultValue = QVariantList();
            } else {
                // string 或未知类型 → 按字符串处理
                defaultValue = initValue;
            }

            if (!m_variableManager->createVariable(varName, varType, defaultValue)) {
                result.ok = false;
                result.data["error"] = QString("createVariable 失败: %1（变量名非法或已存在）").arg(varName);
                Logger::warn(QString("VariableTool: createVariable 失败 - %1").arg(varName));
                if (!input.empty()) result.overlayImage = input.clone();
                return false;
            }
            resultVar = defaultValue;

        // ---- v2.7.0：delete 操作 - 删除全局变量 ----
        } else if (m_operation == "delete") {
            if (!m_variableManager) {
                result.ok = false;
                result.data["error"] = "VariableManager 未注入";
                Logger::warn("VariableTool: delete 操作失败 - VariableManager 未注入");
                if (!input.empty()) result.overlayImage = input.clone();
                return false;
            }
            if (!m_variableManager->removeVariable(m_operand1)) {
                result.ok = false;
                result.data["error"] = QString("removeVariable 失败: %1（变量不存在）").arg(m_operand1);
                Logger::warn(QString("VariableTool: removeVariable 失败 - %1").arg(m_operand1));
                if (!input.empty()) result.overlayImage = input.clone();
                return false;
            }
            resultVar = true;

        } else {
            // 不应到达（configure 已校验），兜底
            result.ok = false;
            result.data["error"] = QString("未知运算类型: %1").arg(m_operation);
            if (!input.empty()) result.overlayImage = input.clone();
            return false;
        }

        // ---- 写出结果（ports + data 双通道）----
        result.ok = true;
        // 固定端口名 "result"
        result.ports["result"] = resultVar;
        // 同时以 outputVar 名称为 key 写入（便于下游按变量名引用）
        if (m_outputVar != "result") {
            result.ports[m_outputVar] = resultVar;
        }

        result.data["result"] = QJsonValue::fromVariant(resultVar);
        result.data["operation"] = m_operation;
        result.data["operand1"] = m_operand1;
        result.data["operand2"] = m_operand2;
        result.data["outputVar"] = m_outputVar;

        // overlay 透传输入
        if (!input.empty()) result.overlayImage = input.clone();

        Logger::info(QString("VariableTool: op=%1, result=%2")
                         .arg(m_operation)
                         .arg(resultVar.toString()));
        return true;

    } catch (const std::exception& e) {
        Logger::error(QString("VariableTool: 异常: %1").arg(QString::fromStdString(e.what())));
        result.ok = false;
        result.data["error"] = QString::fromStdString(e.what());
        if (!input.empty()) result.overlayImage = input.clone();
        return false;
    }
}

// =====================================================
// 端口声明（P1-3 typed ports）
// =====================================================
QList<PortDescriptor> VariableTool::outputPorts() const {
    return {
        PortDescriptor{ "result", "结果", PortType::Any, PortDirection::Out, "运算结果（数值或字符串）" },
    };
}

QList<PortDescriptor> VariableTool::inputPorts() const {
    return {
        PortDescriptor{ "image", "图像", PortType::Image, PortDirection::In, "输入图像（透传到 overlay）" },
    };
}

// =====================================================
// 序列化 / 反序列化
// =====================================================
QJsonObject VariableTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["operation"] = m_operation;
    obj["operand1"]  = m_operand1;
    obj["operand2"]  = m_operand2;
    obj["format"]    = m_format;
    obj["outputVar"] = m_outputVar;
    return obj;
}

bool VariableTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("operation")) {
        const QString v = data["operation"].toString();
        static const QSet<QString> validOps = {
            "add", "sub", "mul", "div", "mod", "concat", "format",
            "get", "set", "define", "delete"  // v2.7.0 新增
        };
        if (validOps.contains(v)) m_operation = v;
    }
    if (data.contains("operand1"))  m_operand1  = data["operand1"].toString();
    if (data.contains("operand2"))  m_operand2  = data["operand2"].toString();
    if (data.contains("format"))    m_format    = data["format"].toString();
    if (data.contains("outputVar")) {
        const QString v = data["outputVar"].toString();
        m_outputVar = v.isEmpty() ? "result" : v;
    }
    return true;
}
