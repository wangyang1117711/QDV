#ifndef QDV_OPERATORLIBRARY_DEPENDENCYGRAPH_H
#define QDV_OPERATORLIBRARY_DEPENDENCYGRAPH_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include "OperatorLibrary/OperatorDefinition.h"

namespace QDV {
namespace OperatorLibrary {

/// 算子依赖有向图
/// 节点=算子 type；边 type -> 其依赖的 type 列表。
class OperatorDependencyGraph {
public:
    void build(const QList<OperatorDef>& ops);

    QList<QString> dependenciesOf(const QString& type) const;  ///< 我依赖谁
    QList<QString> dependentsOf(const QString& type) const;    ///< 谁依赖我
    bool hasCycle() const;                                     ///< 是否存在环
    bool canDelete(const QString& type) const;                 ///< 无被依赖方可删
    QList<QString> topoOrder() const;                          ///< 拓扑序（被依赖者在前）

    QMap<QString, QList<QString>> dependentsCache() const;     ///< 反查缓存，供 Store 使用

private:
    QMap<QString, QList<QString>> m_deps;        ///< type -> dependencies
    QMap<QString, QList<QString>> m_dependents;  ///< type -> dependents
};

} // namespace OperatorLibrary
} // namespace QDV

#endif // QDV_OPERATORLIBRARY_DEPENDENCYGRAPH_H
