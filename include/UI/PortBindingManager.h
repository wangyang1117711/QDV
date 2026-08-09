#ifndef PORT_BINDING_MANAGER_H
#define PORT_BINDING_MANAGER_H

// ============================================================================
// PortBindingManager —— 端口绑定数据模型（spec：editor-output-connection-optimization）
// ----------------------------------------------------------------------------
// 职责：
//   1. 基于 EditViewBridge.m_connections 提供端口级绑定查询——
//      下游某输入端口绑定哪些上游输出端口（多输入）
//      上游某输出端口被哪些下游消费（多输出扇出）
//   2. 校验绑定合法性：端口类型兼容（复用 EditViewBridge::checkPortCompatible）
//      + 上游输出已勾选（outputConfig.enabled）
//
// 设计要点：
//   - 与 OutputConfigManager 一致，持有 EditViewBridge* 弱引用，通过其 public
//     接口访问 currentNodes()/connections()/checkPortCompatible()/getOutputConfig()。
//   - 不新增独立存储：绑定即 m_connections（{fromId,fromPort,toId,toPort}），
//     本类提供语义化查询视图，避免数据冗余与双写不一致。
// ============================================================================

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class EditViewBridge;

class PortBindingManager : public QObject {
    Q_OBJECT
public:
    /// 一条端口绑定的语义化视图
    struct Binding {
        QString upstreamToolId;   ///< 上游算子 ID
        QString upstreamPort;     ///< 上游输出端口名
        QString downstreamToolId; ///< 下游算子 ID
        QString downstreamPort;   ///< 下游输入端口名
        bool    upstreamEnabled = true; ///< 上游该输出是否已勾选（enabled）
    };

    explicit PortBindingManager(EditViewBridge* bridge, QObject* parent = nullptr);
    ~PortBindingManager() override = default;

    /// 查询下游某输入端口绑定的所有上游输出（多输入）
    /// @return 绑定列表（可能为空）
    QList<Binding> bindingsForInput(const QString& downstreamToolId,
                                    const QString& downstreamPort) const;

    /// 查询上游某输出端口被哪些下游消费（多输出扇出）
    QList<Binding> consumersOfOutput(const QString& upstreamToolId,
                                     const QString& upstreamPort) const;

    /// 查询某节点全部输入绑定（跨所有输入端口）
    QList<Binding> allInputBindings(const QString& downstreamToolId) const;

    /// 查询某节点全部输出扇出（跨所有输出端口）
    QList<Binding> allOutputFans(const QString& upstreamToolId) const;

    /// 校验一条绑定是否合法：端口类型兼容 + 上游输出已勾选
    /// @param outReason 校验失败原因（通过指针回填）
    bool validateBinding(const QString& fromId, const QString& fromPort,
                         const QString& toId,   const QString& toPort,
                         QString* outReason = nullptr) const;

    /// 上游某输出是否已勾选（outputConfig 缺失时回退 defaultEnabled=true）
    bool isOutputEnabled(const QString& upstreamToolId,
                         const QString& upstreamPort) const;

private:
    EditViewBridge* m_bridge;
};

#endif // PORT_BINDING_MANAGER_H