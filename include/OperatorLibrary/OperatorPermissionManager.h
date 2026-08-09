#ifndef QDV_OPERATORLIBRARY_PERMISSIONMANAGER_H
#define QDV_OPERATORLIBRARY_PERMISSIONMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantMap>
#include "OperatorLibrary/OperatorDefinition.h"

namespace QDV {
namespace OperatorLibrary {

/// 算子权限管理（RBAC，最小权限原则）
/// 角色：admin / engineer / viewer / guest
/// 动作：view / edit / delete / publish / use
/// 与 AuthService 集成：admin 角色继承 AuthService::isAdmin。
class OperatorPermissionManager : public QObject {
    Q_OBJECT
public:
    enum Action { View, Edit, Delete, Publish, Use };

    static OperatorPermissionManager* instance();

    /// 判断 user 对 def 是否拥有 action 权限
    bool can(const QString& user, Action a, const OperatorDef& def) const;
    /// 当前登录用户（AuthService::currentUser）权限判断
    bool canCurrentUser(Action a, const OperatorDef& def) const;

    /// 角色 -> 动作列表 的默认映射（供 UI 初始化）
    static QVariantMap defaultRoleActions();

    /// 覆盖某算子的角色权限（写回注册表）
    void applyOverrides(const QString& type, const QVariantMap& roles);

private:
    explicit OperatorPermissionManager(QObject* parent = nullptr);
    static OperatorPermissionManager* s_instance;
};

} // namespace OperatorLibrary
} // namespace QDV

#endif // QDV_OPERATORLIBRARY_PERMISSIONMANAGER_H
