#include "OperatorLibrary/OperatorListModel.h"

// =====================================================================
// OperatorListModel 实现（tasks.md Task 5.1）
//
// 职责：QAbstractListModel 包装 OperatorDef 列表，供 QML ListView 绑定
// 角色：type / cnName / category / version / status / kind / author /
//       description / iconPath / tags
//
// 过滤：setFilter(category, keyword) → 内存过滤 m_all → m_filtered
// 刷新：refresh() → emit dataChanged；外部通过 setOperators 重新装载
// =====================================================================

namespace QDV {
namespace OperatorLibrary {

// ---------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------
OperatorListModel::OperatorListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

// ---------------------------------------------------------------------
// 行数
// ---------------------------------------------------------------------
int OperatorListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_filtered.size();
}

// ---------------------------------------------------------------------
// 角色名（QML 端通过 model.type / model.cnName 等访问）
// ---------------------------------------------------------------------
QHash<int, QByteArray> OperatorListModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[TypeRole]     = "type";
    roles[CnNameRole]   = "cnName";
    roles[CategoryRole] = "category";
    roles[VersionRole]  = "version";
    roles[StatusRole]   = "status";
    roles[KindRole]     = "kind";
    roles[AuthorRole]   = "author";
    roles[DescRole]     = "description";
    roles[IconRole]     = "iconPath";
    roles[TagsRole]     = "tags";
    return roles;
}

// ---------------------------------------------------------------------
// 数据访问
// ---------------------------------------------------------------------
QVariant OperatorListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return QVariant();
    const int row = index.row();
    if (row < 0 || row >= m_filtered.size()) return QVariant();

    const OperatorDef& def = m_filtered.at(row);
    switch (role) {
        case TypeRole:     return def.type;
        case CnNameRole:   return def.cnName;
        case CategoryRole: return def.category;
        case VersionRole:  return def.version;
        case StatusRole:   return def.statusString();
        case KindRole:     return def.kindString();
        case AuthorRole:   return def.author;
        case DescRole:     return def.description;
        case IconRole:     return def.iconPath;
        case TagsRole:     return def.tags;
        default:           return QVariant();
    }
}

// ---------------------------------------------------------------------
// 设置过滤条件并重新过滤
// ---------------------------------------------------------------------
void OperatorListModel::setFilter(const QString& category, const QString& keyword) {
    m_categoryFilter = category;
    m_keyword = keyword;

    beginResetModel();
    m_filtered.clear();

    for (const auto& def : m_all) {
        // 分类过滤（空=不限）
        if (!m_categoryFilter.isEmpty() && def.category != m_categoryFilter) {
            continue;
        }
        // 关键词过滤（匹配 type / cnName / description，不区分大小写）
        if (!m_keyword.isEmpty()) {
            const QString kw = m_keyword.toLower();
            const bool match = def.type.toLower().contains(kw)
                            || def.cnName.toLower().contains(kw)
                            || def.description.toLower().contains(kw);
            if (!match) continue;
        }
        m_filtered.append(def);
    }
    endResetModel();
}

// ---------------------------------------------------------------------
// 刷新（通知 QML 数据可能已变，需配合 setOperators 重新装载）
// ---------------------------------------------------------------------
void OperatorListModel::refresh() {
    setFilter(m_categoryFilter, m_keyword);
}

// ---------------------------------------------------------------------
// 获取指定行的完整算子定义（QVariantMap 形式，QML 友好）
// ---------------------------------------------------------------------
QVariantMap OperatorListModel::get(int row) const {
    QVariantMap m;
    if (row < 0 || row >= m_filtered.size()) return m;
    const OperatorDef& def = m_filtered.at(row);
    m["type"]        = def.type;
    m["cnName"]      = def.cnName;
    m["category"]    = def.category;
    m["version"]     = def.version;
    m["status"]      = def.statusString();
    m["kind"]        = def.kindString();
    m["author"]      = def.author;
    m["description"] = def.description;
    m["iconPath"]    = def.iconPath;
    m["tags"]        = def.tags;
    return m;
}

// ---------------------------------------------------------------------
// 批量设置算子列表（供 Controller/Bridge 装载）
// ---------------------------------------------------------------------
void OperatorListModel::setOperators(const QList<OperatorDef>& ops) {
    m_all = ops;
    refresh();  // 重新过滤并重置模型
}

// ---------------------------------------------------------------------
// 获取指定行的 OperatorDef（C++ 端使用）
// ---------------------------------------------------------------------
OperatorDef OperatorListModel::operatorAt(int row) const {
    if (row < 0 || row >= m_filtered.size()) return OperatorDef();
    return m_filtered.at(row);
}

} // namespace OperatorLibrary
} // namespace QDV
