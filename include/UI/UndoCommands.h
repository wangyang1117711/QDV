#ifndef UNDO_COMMANDS_H
#define UNDO_COMMANDS_H

/**
 * @file UndoCommands.h
 * @brief EditView 撤销/重做命令（v2.1.0 M4 引入）
 *
 * 设计原则：
 * 1. **AI 零侵入**：所有命令仅修改 UI 层的 m_currentNodes（QVariantList），
 *    不直接持有 Core/Scheme 指针，避免与 AI 推理模型产生紧耦合
 * 2. **未来扩展**：每个命令接收 `void* schemeContext`（nullptr 时仅操作 UI）；
 *    后续 M5+ 与 Scheme 协同时，可在该指针中传入 Scheme*，在 redo()/undo() 同步推送到 Scheme
 * 3. **原子性**：每个命令对应一次用户操作（添加/删除/移动/连接/断开/改属性）
 * 4. **可重入**：undo() 必须精确逆转 redo()，且对同一命令可重入
 *
 * 集成：
 * - EditView 持有 QUndoStack
 * - EditViewBridge::addOperator/moveNode/deleteNode/updateOperatorParams 等槽
 *   改为 push 对应的 UndoCommand 到 QUndoStack（而非直接修改 m_currentNodes）
 * - EditViewBridge::undo()/redo() 转发到 m_undoStack->undo()/redo()
 *
 * 不在本期：
 * - Core/Scheme 双向同步（schemeContext 留接口）
 * - 命令合并（如连续移动合并为一次 Undo）—— M5+ 阶段
 */

#include <QUndoCommand>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <QStringList>

// v2.1.0 M4：EditViewBridge 在全局命名空间，命令参数使用 :: 前缀
// 前向声明必须放在全局作用域（不放在 namespace QDV::UI 内）
class EditViewBridge;

namespace QDV {
namespace UI {

class Scheme;   // 前向声明（M5+ 协同时使用）

/**
 * @brief 撤销命令基类（v2.1.0 M4 引入）
 *
 * 提供公共的 schemeContext 字段（void*，nullptr 表示仅操作 UI 快照）。
 * M5+ 阶段接入 Scheme 协同时，可在 redo()/undo() 内 cast 为 Scheme* 使用。
 */
class UndoCommandBase : public QUndoCommand {
public:
    explicit UndoCommandBase(::EditViewBridge* bridge,
                             void* schemeContext = nullptr,
                             QUndoCommand* parent = nullptr);
    virtual ~UndoCommandBase() override = default;

    /// 获取绑定的 bridge（操作 m_currentNodes）
    ::EditViewBridge* bridge() const { return m_bridge; }
    /// 获取 Scheme 上下文（M4 阶段恒为 nullptr，M5+ 接入 Scheme 协同时使用）
    void* schemeContext() const { return m_schemeContext; }

protected:
    ::EditViewBridge* m_bridge;
    void*             m_schemeContext;   ///< M4: nullptr；M5+ 可传入 Scheme*
};

/**
 * @brief 添加节点
 * redo: 在 m_currentNodes 末尾插入节点快照
 * undo: 移除该节点
 */
class AddNodeCommand : public UndoCommandBase {
public:
    AddNodeCommand(EditViewBridge* bridge,
                   const QVariantMap& nodeSnapshot,
                   void* schemeContext = nullptr,
                   QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;
    QString text() const { return QStringLiteral("添加节点 %1").arg(m_nodeType); }

private:
    QVariantMap m_nodeSnapshot;   ///< 节点完整快照（id/type/x/y/params）
    QString     m_nodeType;       ///< 缓存用于 text() 显示
};

/**
 * @brief 删除节点
 * redo: 从 m_currentNodes 移除节点
 * undo: 恢复节点到原位置
 */
class RemoveNodeCommand : public UndoCommandBase {
public:
    RemoveNodeCommand(::EditViewBridge* bridge,
                      const QString& nodeId,
                      void* schemeContext = nullptr,
                      QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;
    QString text() const { return QStringLiteral("删除节点"); }

private:
    QString     m_nodeId;
    QVariantMap m_nodeSnapshot;   ///< undo 用
    int         m_originIndex;    ///< 节点原位置
};

/**
 * @brief 移动节点
 * redo: 更新节点 (x, y)
 * undo: 恢复 (x, y)
 */
class MoveNodeCommand : public UndoCommandBase {
public:
    MoveNodeCommand(EditViewBridge* bridge,
                    const QString& nodeId,
                    qreal oldX, qreal oldY,
                    qreal newX, qreal newY,
                    void* schemeContext = nullptr,
                    QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;
    QString text() const { return QStringLiteral("移动节点"); }

    /// 合并连续移动（如拖拽过程中多次 mouseMove）
    int id() const override { return 1001; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    QString m_nodeId;
    qreal   m_oldX, m_oldY;
    qreal   m_newX, m_newY;
};

/**
 * @brief 连接两个节点
 * redo: 添加一条边（from → to）
 * undo: 移除该边
 */
class ConnectNodesCommand : public UndoCommandBase {
public:
    ConnectNodesCommand(::EditViewBridge* bridge,
                        const QString& fromId, const QString& fromPort,
                        const QString& toId,   const QString& toPort,
                        void* schemeContext = nullptr,
                        QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;
    QString text() const { return QStringLiteral("连接节点"); }

private:
    QString m_fromId, m_fromPort;
    QString m_toId,   m_toPort;
};

/**
 * @brief 断开节点连接
 */
class DisconnectNodesCommand : public UndoCommandBase {
public:
    DisconnectNodesCommand(EditViewBridge* bridge,
                           const QString& fromId, const QString& fromPort,
                           const QString& toId,   const QString& toPort,
                           void* schemeContext = nullptr,
                           QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;
    QString text() const { return QStringLiteral("断开连接"); }

private:
    QString m_fromId, m_fromPort;
    QString m_toId,   m_toPort;
};

/**
 * @brief 修改节点参数
 * redo: 替换节点 params
 * undo: 恢复 params
 */
class PropertyChangeCommand : public UndoCommandBase {
public:
    PropertyChangeCommand(EditViewBridge* bridge,
                          const QString& nodeId,
                          const QString& paramName,
                          const QVariant& oldValue,
                          const QVariant& newValue,
                          void* schemeContext = nullptr,
                          QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;
    QString text() const {
        return QStringLiteral("修改 %1").arg(m_paramName);
    }

    /// 合并连续修改（同一参数的连续 setValue 只算一次 undo）
    int id() const override { return 1002; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    QString   m_nodeId;
    QString   m_paramName;
    QVariant  m_oldValue;
    QVariant  m_newValue;
};

} // namespace UI
} // namespace QDV

#endif // UNDO_COMMANDS_H
