#ifndef OUTPUT_CONFLICT_DETECTOR_H
#define OUTPUT_CONFLICT_DETECTOR_H

// ============================================================================
// OutputConflictDetector —— 输入/输出项冲突检测引擎（spec：editor-output-connection-optimization）
// ----------------------------------------------------------------------------
// 职责（三类冲突）：
//   1. 同名冲突（dupAlias）：同一节点多个输出端口别名(alias)相同，无法区分；
//      或不同节点输出到同一下游输入端口时产生语义歧义。
//   2. 类型不兼容（typeMismatch）：上游输出端口类型与下游输入端口类型不兼容
//      （复用 EditViewBridge::checkPortCompatible —— 本类声明为好友）。
//   3. 多输入绑定歧义（multiInputAmbiguity）：某下游输入端口被多个上游绑定，
//      且类型能力冲突（或输出别名相同），无法明确取哪个。
//
// 设计要点：
//   - 与 PortBindingManager 风格一致：QObject 派生，持有 EditViewBridge* 弱引用，
//     通过其 public 接口访问 currentNodes()/connections()/getOperatorMeta()，
//     复用 PortBindingManager 的 bindingsForInput() 做多输入语义查询。
//   - 不新增独立存储：冲突均为对当前 nodes/connections 的即时扫描结果，
//     通过 conflictsChanged() 在数据变化时通知 QML 刷新。
// ============================================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class EditViewBridge;
class PortBindingManager;

class OutputConflictDetector : public QObject {
    Q_OBJECT
public:
    /// 一条冲突的语义化视图（供 detectAll/detectForNode 返回 QVariantMap）
    struct Conflict {
        QString kind;         ///< "dupAlias" | "typeMismatch" | "multiInputAmbiguity"
        QString nodeId;       ///< 主节点 ID（歧义指向下游节点）
        QString portName;     ///< 冲突端口名（下游输入端口或上游重复别名）
        QString detail;       ///< 冲突描述
        QStringList candidates; ///< 候选（重复别名端口 / 多上游候选）
    };

    explicit OutputConflictDetector(EditViewBridge* bridge, QObject* parent = nullptr);
    ~OutputConflictDetector() override = default;

    /// 扫描全部冲突（返回 QVariantMap 列表，字段见 appendConflict）
    Q_INVOKABLE QVariantList detectAll() const;
    /// 扫描指定节点的冲突（nodeId 命中即返回）
    Q_INVOKABLE QVariantList detectForNode(const QString& nodeId) const;
    /// 是否存在任意冲突
    Q_INVOKABLE bool hasConflicts() const;

signals:
    /// 冲突集合变化（nodes/connections 变化时触发）
    void conflictsChanged();

private:
    void appendConflict(QVariantList& out, const Conflict& c) const;
    /// 根据 nodeId 查找算子 type（节点不存在返回空串）
    QString nodeTypeById(const QString& nodeId) const;
    /// 节点声明的输出项列表（含 name/alias/group/priority/typeName）
    QVariantList outputMetaForNode(const QString& nodeId) const;
    /// 节点声明的输入项列表（含 name/typeName）
    QVariantList inputMetaForNode(const QString& nodeId) const;
    QVariantMap findOutputMeta(const QString& nodeId, const QString& portName) const;
    /// 上游某输出端口的类型名（未找到返回空串）
    QString outputTypeFor(const QString& nodeId, const QString& portName) const;
    /// 上游某输出端口的别名（未找到返回空串）
    QString outputAliasFor(const QString& nodeId, const QString& portName) const;
    /// 类型名是否为通配（Any 或空）
    static bool isWildcardType(const QString& typeName);

    EditViewBridge*      m_bridge;    ///< 弱引用（生命周期跟随 bridge）
    PortBindingManager*  m_bindings;  ///< 端口绑定语义查询模型（复用其多输入查询）
};

#endif // OUTPUT_CONFLICT_DETECTOR_H