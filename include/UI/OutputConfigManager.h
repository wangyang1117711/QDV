#ifndef OUTPUT_CONFIG_MANAGER_H
#define OUTPUT_CONFIG_MANAGER_H

// ============================================================================
// OutputConfigManager —— 算子输出配置管理器（从 EditViewBridge 拆分，Task 6）
// ----------------------------------------------------------------------------
// 职责：
//   1. 查询节点输出开关配置 getOutputConfig
//   2. 更新节点输出配置 updateOutputConfig（走 UndoCommand 可撤销路径）
//   3. 配置变更后同步 VariableManager 中的算子输出变量注册状态
//
// 设计要点：
//   - 持有 EditViewBridge* 弱引用，通过其 public 接口访问 currentNodes()/
//     undoStack()/updateParamInternal()/markDirty()/variableManager()。
//   - 通过 Qt 信号转发机制将 errorRaised / currentNodesChanged 回传给
//     EditViewBridge（保持其信号契约不变，phase 字符串为 "updateOutputConfig"）。
// ============================================================================

#include <QObject>
#include <QString>
#include <QVariantMap>

class EditViewBridge;

class OutputConfigManager : public QObject {
    Q_OBJECT
public:
    explicit OutputConfigManager(EditViewBridge* bridge, QObject* parent = nullptr);
    ~OutputConfigManager() override = default;

    /// 查询指定节点的输出开关配置（{outputName: {enabled: bool}, ...}）
    /// 节点不存在时返回空 map
    QVariantMap getOutputConfig(const QString& nodeId) const;

    /// 更新指定节点的输出开关配置（走 UndoCommand 可撤销路径）
    /// @param nodeId 目标节点 ID
    /// @param outputs 完整的输出配置 map（整体替换）
    void updateOutputConfig(const QString& nodeId, const QVariantMap& outputs);

signals:
    /// 错误通知（phase 固定为 "updateOutputConfig"），由 EditViewBridge 转发
    void errorRaised(const QString& phase, const QString& message);
    /// 节点列表变更通知（fallback 直接修改路径触发），由 EditViewBridge 转发
    void currentNodesChanged();

private:
    EditViewBridge* m_bridge;
};

#endif // OUTPUT_CONFIG_MANAGER_H
