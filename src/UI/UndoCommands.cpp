#include "UI/UndoCommands.h"
#include "UI/EditViewBridge.h"

#include <QDebug>

namespace QDV {
namespace UI {

// =====================================================================
// 基类
// =====================================================================
UndoCommandBase::UndoCommandBase(::EditViewBridge* bridge,
                                 void* schemeContext,
                                 QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_bridge(bridge)
    , m_schemeContext(schemeContext) {}

// =====================================================================
// AddNodeCommand
// =====================================================================
AddNodeCommand::AddNodeCommand(::EditViewBridge* bridge,
                               const QVariantMap& nodeSnapshot,
                               void* schemeContext,
                               QUndoCommand* parent)
    : UndoCommandBase(bridge, schemeContext, parent)
    , m_nodeSnapshot(nodeSnapshot)
    , m_nodeType(nodeSnapshot.value("type").toString()) {
    // 首次创建时，QUndoCommand::redo() 会被自动调用一次（QUndoStack.push 行为）
    // 但若我们 push 时不调用 redo，状态可能错乱。EditViewBridge.addNode 负责：
    //   1. 创建新节点（直接调 m_currentNodes.append）—— 不通过 Undo
    //   2. push AddNodeCommand —— 首次 redo 已经是 m_currentNodes 已有，跳过
    // 或者：
    //   1. push AddNodeCommand —— 首次 redo 时 m_currentNodes 还没有
    // 我们选方案 B（更标准）。EditViewBridge 改造时只 push UndoCommand。
}

void AddNodeCommand::redo() {
    if (!m_bridge) return;
    QVariantList& nodes = m_bridge->currentNodesRef();
    // 检查是否已存在（首次 push 时 m_nodeSnapshot 不在 nodes 中，重做时存在）
    const QString nodeId = m_nodeSnapshot.value("id").toString();
    for (const QVariant& v : nodes) {
        if (v.toMap().value("id").toString() == nodeId) {
            return;  // 已存在，跳过（QUndoStack.push 首次不调 redo，本函数只在 redo 时被调）
        }
    }
    nodes.append(m_nodeSnapshot);
    m_bridge->notifyCurrentNodesChanged();
}

void AddNodeCommand::undo() {
    if (!m_bridge) return;
    QVariantList& nodes = m_bridge->currentNodesRef();
    const QString nodeId = m_nodeSnapshot.value("id").toString();
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes[i].toMap().value("id").toString() == nodeId) {
            nodes.removeAt(i);
            m_bridge->notifyCurrentNodesChanged();
            return;
        }
    }
}

// =====================================================================
// RemoveNodeCommand
// =====================================================================
RemoveNodeCommand::RemoveNodeCommand(EditViewBridge* bridge,
                                     const QString& nodeId,
                                     void* schemeContext,
                                     QUndoCommand* parent)
    : UndoCommandBase(bridge, schemeContext, parent)
    , m_nodeId(nodeId)
    , m_originIndex(-1) {
    // 构造时缓存节点快照（push 时节点已存在于 m_currentNodes）
    if (m_bridge) {
        const QVariantList& nodes = m_bridge->currentNodes();
        for (int i = 0; i < nodes.size(); ++i) {
            if (nodes[i].toMap().value("id").toString() == m_nodeId) {
                m_nodeSnapshot = nodes[i].toMap();
                m_originIndex = i;
                break;
            }
        }
    }
}

void RemoveNodeCommand::redo() {
    if (!m_bridge) return;
    // P1-A5 修复：删除节点时同步清理引用该节点的连接，避免悬空连接残留
    QVariantList& conns = m_bridge->connectionsRef();
    if (m_removedConnections.isEmpty()) {
        // 首次 redo：缓存并删除相关连接
        for (int i = conns.size() - 1; i >= 0; --i) {
            const QVariantMap c = conns[i].toMap();
            if (c.value("fromId").toString() == m_nodeId ||
                c.value("toId").toString() == m_nodeId) {
                m_removedConnections.append(conns[i]);
                conns.removeAt(i);
            }
        }
    } else {
        // 后续 redo（undo 后再 redo）：从缓存中删除
        for (int i = conns.size() - 1; i >= 0; --i) {
            const QVariantMap c = conns[i].toMap();
            if (c.value("fromId").toString() == m_nodeId ||
                c.value("toId").toString() == m_nodeId) {
                conns.removeAt(i);
            }
        }
    }
    QVariantList& nodes = m_bridge->currentNodesRef();
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes[i].toMap().value("id").toString() == m_nodeId) {
            nodes.removeAt(i);
            m_bridge->notifyConnectionsChanged();
            m_bridge->notifyCurrentNodesChanged();
            return;
        }
    }
}

void RemoveNodeCommand::undo() {
    if (!m_bridge || m_originIndex < 0) return;
    QVariantList& nodes = m_bridge->currentNodesRef();
    if (m_originIndex <= nodes.size()) {
        nodes.insert(m_originIndex, m_nodeSnapshot);
    } else {
        nodes.append(m_nodeSnapshot);
    }
    // P1-A5 修复：恢复被删除的连接
    QVariantList& conns = m_bridge->connectionsRef();
    for (const QVariant& c : m_removedConnections) {
        conns.append(c);
    }
    m_bridge->notifyConnectionsChanged();
    m_bridge->notifyCurrentNodesChanged();
}

// =====================================================================
// MoveNodeCommand
// =====================================================================
MoveNodeCommand::MoveNodeCommand(EditViewBridge* bridge,
                                 const QString& nodeId,
                                 qreal oldX, qreal oldY,
                                 qreal newX, qreal newY,
                                 void* schemeContext,
                                 QUndoCommand* parent)
    : UndoCommandBase(bridge, schemeContext, parent)
    , m_nodeId(nodeId)
    , m_oldX(oldX), m_oldY(oldY)
    , m_newX(newX), m_newY(newY) {}

void MoveNodeCommand::redo() {
    if (!m_bridge) return;
    m_bridge->moveNodeInternal(m_nodeId, m_newX, m_newY, true);
}

void MoveNodeCommand::undo() {
    if (!m_bridge) return;
    m_bridge->moveNodeInternal(m_nodeId, m_oldX, m_oldY, true);
}

bool MoveNodeCommand::mergeWith(const QUndoCommand* other) {
    // 合并连续移动：把 other 的 newX/newY 覆盖到自己的 newX/newY
    const MoveNodeCommand* o = dynamic_cast<const MoveNodeCommand*>(other);
    if (!o || o->m_nodeId != m_nodeId) return false;
    m_newX = o->m_newX;
    m_newY = o->m_newY;
    return true;
}

// =====================================================================
// ConnectNodesCommand
// =====================================================================
ConnectNodesCommand::ConnectNodesCommand(EditViewBridge* bridge,
                                         const QString& fromId, const QString& fromPort,
                                         const QString& toId,   const QString& toPort,
                                         void* schemeContext,
                                         QUndoCommand* parent)
    : UndoCommandBase(bridge, schemeContext, parent)
    , m_fromId(fromId), m_fromPort(fromPort)
    , m_toId(toId),     m_toPort(toPort) {}

void ConnectNodesCommand::redo() {
    if (!m_bridge) return;
    m_bridge->addConnectionInternal(m_fromId, m_fromPort, m_toId, m_toPort, true);
}

void ConnectNodesCommand::undo() {
    if (!m_bridge) return;
    m_bridge->removeConnectionInternal(m_fromId, m_fromPort, m_toId, m_toPort, true);
}

// =====================================================================
// DisconnectNodesCommand
// =====================================================================
DisconnectNodesCommand::DisconnectNodesCommand(EditViewBridge* bridge,
                                               const QString& fromId, const QString& fromPort,
                                               const QString& toId,   const QString& toPort,
                                               void* schemeContext,
                                               QUndoCommand* parent)
    : UndoCommandBase(bridge, schemeContext, parent)
    , m_fromId(fromId), m_fromPort(fromPort)
    , m_toId(toId),     m_toPort(toPort) {}

void DisconnectNodesCommand::redo() {
    if (!m_bridge) return;
    m_bridge->removeConnectionInternal(m_fromId, m_fromPort, m_toId, m_toPort, true);
}

void DisconnectNodesCommand::undo() {
    if (!m_bridge) return;
    m_bridge->addConnectionInternal(m_fromId, m_fromPort, m_toId, m_toPort, true);
}

// =====================================================================
// PropertyChangeCommand
// =====================================================================
PropertyChangeCommand::PropertyChangeCommand(::EditViewBridge* bridge,
                                             const QString& nodeId,
                                             const QString& paramName,
                                             const QVariant& oldValue,
                                             const QVariant& newValue,
                                             void* schemeContext,
                                             QUndoCommand* parent)
    : UndoCommandBase(bridge, schemeContext, parent)
    , m_nodeId(nodeId)
    , m_paramName(paramName)
    , m_oldValue(oldValue)
    , m_newValue(newValue) {}

void PropertyChangeCommand::redo() {
    if (!m_bridge) return;
    m_bridge->updateParamInternal(m_nodeId, m_paramName, m_newValue, true);
}

void PropertyChangeCommand::undo() {
    if (!m_bridge) return;
    m_bridge->updateParamInternal(m_nodeId, m_paramName, m_oldValue, true);
}

bool PropertyChangeCommand::mergeWith(const QUndoCommand* other) {
    const PropertyChangeCommand* o = dynamic_cast<const PropertyChangeCommand*>(other);
    if (!o || o->m_nodeId != m_nodeId || o->m_paramName != m_paramName) return false;
    // 连续修改同一参数：只保留最旧的 oldValue 和最新的 newValue
    m_newValue = o->m_newValue;
    return true;
}

} // namespace UI
} // namespace QDV
