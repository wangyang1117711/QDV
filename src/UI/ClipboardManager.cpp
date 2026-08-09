// ============================================================================
// ClipboardManager —— 算子参数剪贴板管理器实现（从 EditViewBridge 拆分，Task 6）
// ============================================================================

#include "UI/ClipboardManager.h"
#include "UI/OperatorDescriptors.h"

#include <QDebug>

bool ClipboardManager::copyFrom(const QVariantList& nodes, const QString& nodeId) {
    for (const QVariant& v : nodes) {
        const QVariantMap n = v.toMap();
        if (n.value("id").toString() == nodeId) {
            m_clipType = n.value("type").toString();
            m_clipParams = n.value("params").toMap();
            m_clipHasData = true;
            qDebug() << "[ClipboardManager] copyFrom" << m_clipType
                     << "params count=" << m_clipParams.size();
            return true;
        }
    }
    return false;  // 节点不存在
}

void ClipboardManager::setClip(const QString& type, const QVariantMap& params) {
    m_clipType = type;
    m_clipParams = params;
    m_clipHasData = true;
}

void ClipboardManager::clear() {
    m_clipHasData = false;
    m_clipType.clear();
    m_clipParams.clear();
}

ClipboardManager::PasteResult ClipboardManager::buildPaste(
    const QVariantMap& targetNode,
    const std::function<QStringList(const QString&, const QString&, const QVariant&)>& validator) const {
    PasteResult result;
    result.clipType = m_clipType;

    if (!m_clipHasData) {
        return result;  // 剪贴板为空，applied=false
    }

    const QString targetType = targetNode.value("type").toString();
    // 类型不同时仍允许粘贴，但只粘贴参数名匹配的字段（更宽松的策略）
    // 这样改名同义的参数（如 hMin/rMin）也能手工迁移，避免一刀切阻断

    // 取目标算子的 ParamSpec 列表，用于校验每个参数
    const QDV::UI::OperatorMeta targetMeta = QDV::UI::OperatorDescriptors::get(targetType);
    QHash<QString, const QDV::UI::ParamSpec*> targetSpecMap;
    for (const QDV::UI::ParamSpec& p : targetMeta.params) {
        targetSpecMap.insert(p.name, &p);
    }

    QVariantMap mergedParams = targetNode.value("params").toMap();
    int appliedCount = 0;
    for (auto it = m_clipParams.constBegin(); it != m_clipParams.constEnd(); ++it) {
        const QString& paramName = it.key();
        // 仅粘贴目标节点已有的参数（避免引入幽灵参数）
        if (!targetSpecMap.contains(paramName)) {
            result.skippedNames.append(paramName);
            continue;
        }
        // 类型校验：根据 ParamSpec.type 检查值是否可转换
        const QDV::UI::ParamSpec* spec = targetSpecMap.value(paramName);
        const QVariant& clipVal = it.value();
        QVariant converted = clipVal;
        bool ok = true;
        switch (spec->type) {
            case QDV::UI::ParamType::Int:
                converted = QVariant(clipVal.toInt(&ok));
                break;
            case QDV::UI::ParamType::Float:
                converted = QVariant(clipVal.toDouble(&ok));
                break;
            case QDV::UI::ParamType::Bool:
                converted = QVariant(clipVal.toBool());
                break;
            case QDV::UI::ParamType::Enum:
                // Enum 接受 string 或 int，校验是否在 optionKeys 中
                if (spec->optionKeys.contains(clipVal.toString())) {
                    converted = clipVal;
                } else {
                    // 尝试 int 索引（历史方案可能存的是索引而非 key）
                    bool intOk = false;
                    int idx = clipVal.toInt(&intOk);
                    if (intOk && idx >= 0 && idx < spec->optionKeys.size()) {
                        converted = spec->optionKeys.at(idx);
                    } else {
                        ok = false;
                    }
                }
                break;
            case QDV::UI::ParamType::String:
            case QDV::UI::ParamType::ROI:
            case QDV::UI::ParamType::Vector:
                // 字符串/ROI/Vector 直接接受（C++ validateParam 会再校验）
                converted = clipVal;
                break;
        }
        if (!ok) {
            result.skippedNames.append(paramName);
            continue;
        }
        // C++ 端校验（通过注入的 validator 回调复用 EditViewBridge::validateParam）
        const QStringList errs = validator(targetType, paramName, converted);
        if (!errs.isEmpty()) {
            result.skippedNames.append(paramName);
            continue;
        }
        mergedParams.insert(paramName, converted);
        ++appliedCount;
    }

    if (appliedCount > 0) {
        result.applied = true;
        result.appliedCount = appliedCount;
        result.mergedParams = mergedParams;
    }
    return result;
}
