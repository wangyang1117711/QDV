// ============================================================================
// OutputConfigManager —— 算子输出配置管理器实现（从 EditViewBridge 拆分，Task 6）
// ============================================================================

#include "UI/OutputConfigManager.h"
#include "UI/EditViewBridge.h"        // 访问 currentNodes/undoStack/updateParamInternal/markDirty
#include "UI/OperatorDescriptors.h"   // OperatorMeta.outputs
#include "UI/UndoCommands.h"          // PropertyChangeCommand
#include "Core/VariableManager.h"     // registerOperatorOutput / unregisterOperatorOutput
#include "Core/Logger.h"

#include <QUndoStack>

OutputConfigManager::OutputConfigManager(EditViewBridge* bridge, QObject* parent)
    : QObject(parent), m_bridge(bridge) {}

QVariantMap OutputConfigManager::getOutputConfig(const QString& nodeId) const {
    for (const QVariant& v : m_bridge->currentNodes()) {
        const QVariantMap n = v.toMap();
        if (n.value("id").toString() == nodeId) {
            return n.value("outputConfig").toMap();
        }
    }
    return QVariantMap{};  // 节点不存在返回空
}

// v5.4：更新指定节点的输出开关配置（走 UndoCommand 可撤销路径）
// 复用 PropertyChangeCommand，属性键为 "outputConfig"，updateParamInternal 内部据此写到 node["outputConfig"]
void OutputConfigManager::updateOutputConfig(const QString& nodeId, const QVariantMap& outputs) {
    const QVariantList nodes = m_bridge->currentNodes();
    int foundIdx = -1;
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes[i].toMap().value("id").toString() == nodeId) {
            foundIdx = i;
            break;
        }
    }
    if (foundIdx < 0) {
        emit errorRaised(QStringLiteral("updateOutputConfig"),
                         QStringLiteral("未找到节点：%1").arg(nodeId));
        return;
    }
    const QVariantMap node = nodes[foundIdx].toMap();
    const QVariantMap oldOutputs = node.value("outputConfig").toMap();
    // 简单比较：若整体相等则跳过
    if (oldOutputs == outputs) {
        return;
    }
    QDV::Logger::info(QString("updateOutputConfig: nodeId=%1 outputsCount=%2")
                      .arg(nodeId).arg(outputs.size()));
    QUndoStack* stack = m_bridge->undoStack();
    if (stack) {
        // 复用 PropertyChangeCommand，属性键为 "outputConfig"
        stack->push(new QDV::UI::PropertyChangeCommand(
            m_bridge, nodeId, QStringLiteral("outputConfig"),
            oldOutputs, outputs));
    } else {
        // fallback：直接修改
        m_bridge->updateParamInternal(nodeId, QStringLiteral("outputConfig"), outputs, false);
        emit currentNodesChanged();
    }
    m_bridge->markDirty();

    // v5.4：同步 VariableManager 中的算子输出变量
    // 比较 oldOutputs 与 outputs 中每个输出的 enabled 状态，按差异注册/反注册变量
    QObject* vmObj = m_bridge->variableManager();
    QDV::VariableManager* vm = vmObj ? qobject_cast<QDV::VariableManager*>(vmObj) : nullptr;
    if (vm) {
        const QString type = node.value("type").toString();
        const QDV::UI::OperatorMeta meta = QDV::UI::OperatorDescriptors::get(type);
        for (const QVariantMap& out : meta.outputs) {
            const QString outName = out.value("name").toString();
            const bool oldEnabled = oldOutputs.value(outName).toMap().value("enabled", true).toBool();
            const bool newEnabled = outputs.value(outName).toMap().value("enabled", true).toBool();
            if (!oldEnabled && newEnabled) {
                // 启用：注册变量
                vm->registerOperatorOutput(nodeId, outName,
                    out.value("typeName", "string").toString());
            } else if (oldEnabled && !newEnabled) {
                // 禁用：反注册变量
                vm->unregisterOperatorOutput(nodeId, outName);
            }
        }
    }
}
