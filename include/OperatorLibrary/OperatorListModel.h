#ifndef QDV_OPERATORLIBRARY_LISTMODEL_H
#define QDV_OPERATORLIBRARY_LISTMODEL_H

#include <QAbstractListModel>
#include <QString>
#include <QList>
#include "OperatorLibrary/OperatorDefinition.h"

namespace QDV {
namespace OperatorLibrary {

/// 算子列表模型（供 QML ListView 绑定）
/// 角色：type / cnName / category / version / status / kind / author /
///       description / iconPath / tags
class OperatorListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        TypeRole = Qt::UserRole + 1,
        CnNameRole,
        CategoryRole,
        VersionRole,
        StatusRole,
        KindRole,
        AuthorRole,
        DescRole,
        IconRole,
        TagsRole
    };

    explicit OperatorListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void setFilter(const QString& category, const QString& keyword);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap get(int row) const;

    void setOperators(const QList<OperatorDef>& ops);
    OperatorDef operatorAt(int row) const;

private:
    QList<OperatorDef> m_all;
    QList<OperatorDef> m_filtered;
    QString m_categoryFilter;
    QString m_keyword;
};

} // namespace OperatorLibrary
} // namespace QDV

#endif // QDV_OPERATORLIBRARY_LISTMODEL_H
