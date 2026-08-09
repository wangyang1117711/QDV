// ============================================================================
// PortBindingManager —— 端口绑定数据模型实现（spec：editor-output-connection-optimization）
// ============================================================================

#include "UI/PortBindingManager.h"
#include "UI/EditViewBridge.h"
#include "Core/Logger.h"

PortBindingManager::PortBindingManager(EditViewBridge* bridge, QObject* parent)
    : QObject(parent), m_bridge(bridge) {}

QList<PortBindingManager::Binding> PortBindingManager::bindingsForInput(
    const QString& downstreamToolId, const QString& downstreamPort) const {
    QList<Binding> result;
    if (!m_bridge) return result;
    const QVariantList conns = m_bridge->connections();
    for (const QVariant& v : conns) {
        const QVariantMap c = v.toMap();
        if (c.value("toId").toString() == downstreamToolId &&
            c.value("toPort").toString() == downstreamPort) {
            Binding b;
            b.upstreamToolId   = c.value("fromId").toString();
            b.upstreamPort     = c.value("fromPort").toString();
            b.downstreamToolId = downstreamToolId;
            b.downstreamPort   = downstreamPort;
            b.upstreamEnabled  = isOutputEnabled(b.upstreamToolId, b.upstreamPort);
            result.append(b);
        }
    }
    return result;
}

QList<PortBindingManager::Binding> PortBindingManager::consumersOfOutput(
    const QString& upstreamToolId, const QString& upstreamPort) const {
    QList<Binding> result;
    if (!m_bridge) return result;
    const QVariantList conns = m_bridge->connections();
    for (const QVariant& v : conns) {
        const QVariantMap c = v.toMap();
        if (c.value("fromId").toString() == upstreamToolId &&
            c.value("fromPort").toString() == upstreamPort) {
            Binding b;
            b.upstreamToolId   = upstreamToolId;
            b.upstreamPort     = upstreamPort;
            b.downstreamToolId = c.value("toId").toString();
            b.downstreamPort   = c.value("toPort").toString();
            b.upstreamEnabled  = isOutputEnabled(upstreamToolId, upstreamPort);
            result.append(b);
        }
    }
    return result;
}

QList<PortBindingManager::Binding> PortBindingManager::allInputBindings(
    const QString& downstreamToolId) const {
    QList<Binding> result;
    if (!m_bridge) return result;
    const QVariantList conns = m_bridge->connections();
    for (const QVariant& v : conns) {
        const QVariantMap c = v.toMap();
        if (c.value("toId").toString() == downstreamToolId) {
            Binding b;
            b.upstreamToolId   = c.value("fromId").toString();
            b.upstreamPort     = c.value("fromPort").toString();
            b.downstreamToolId = downstreamToolId;
            b.downstreamPort   = c.value("toPort").toString();
            b.upstreamEnabled  = isOutputEnabled(b.upstreamToolId, b.upstreamPort);
            result.append(b);
        }
    }
    return result;
}

QList<PortBindingManager::Binding> PortBindingManager::allOutputFans(
    const QString& upstreamToolId) const {
    QList<Binding> result;
    if (!m_bridge) return result;
    const QVariantList conns = m_bridge->connections();
    for (const QVariant& v : conns) {
        const QVariantMap c = v.toMap();
        if (c.value("fromId").toString() == upstreamToolId) {
            Binding b;
            b.upstreamToolId   = upstreamToolId;
            b.upstreamPort     = c.value("fromPort").toString();
            b.downstreamToolId = c.value("toId").toString();
            b.downstreamPort   = c.value("toPort").toString();
            b.upstreamEnabled  = isOutputEnabled(upstreamToolId, b.upstreamPort);
            result.append(b);
        }
    }
    return result;
}

bool PortBindingManager::isOutputEnabled(const QString& upstreamToolId,
                                         const QString& upstreamPort) const {
    if (!m_bridge) return true;  // 无桥接时默认放行
    const QVariantMap oc = m_bridge->getOutputConfig(upstreamToolId);
    const QVariantMap item = oc.value(upstreamPort).toMap();
    // 配置缺失（未勾选过）→ 默认启用（向后兼容）
    if (item.isEmpty()) return true;
    return item.value("enabled", true).toBool();
}

bool PortBindingManager::validateBinding(const QString& fromId, const QString& fromPort,
                                         const QString& toId,   const QString& toPort,
                                         QString* outReason) const {
    if (fromId.isEmpty() || toId.isEmpty()) {
        if (outReason) *outReason = QStringLiteral("连接端点不能为空");
        return false;
    }
    if (fromId == toId) {
        if (outReason) *outReason = QStringLiteral("不支持节点自连接");
        return false;
    }
    // 上游输出必须已勾选（enabled）才可被引用
    if (!isOutputEnabled(fromId, fromPort)) {
        if (outReason) {
            *outReason = QStringLiteral("上游输出未勾选：无法引用已禁用的输出项");
        }
        return false;
    }
    // 端口类型兼容（任一端未声明端口时桥接实现会放行）
    if (m_bridge && !m_bridge->checkPortCompatible(fromId, fromPort, toId, toPort)) {
        if (outReason) {
            *outReason = QStringLiteral("端口类型不兼容：%1[%2] → %3[%4]")
                             .arg(fromId, fromPort, toId, toPort);
        }
        return false;
    }
    return true;
}