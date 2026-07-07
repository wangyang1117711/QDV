#ifndef QDV_IOPERATOR_REGISTRY_H
#define QDV_IOPERATOR_REGISTRY_H

#include <QString>
#include <QStringList>
#include <functional>
#include "OperatorSDK/IOperator.h"

namespace QDV {

/// 算子注册表（单例）
/// 与现有 ToolFactory 并存：
/// - ToolFactory 仍管旧 15 算子
/// - IOperatorRegistry 管新 SDK 算子（实现 IOperator 接口的）
class IOperatorRegistry {
public:
    /// 单例访问
    static IOperatorRegistry& instance();

    /// 注册算子；重复 type 返回 false，不覆盖（RT-001）
    bool registerOperator(const QString& type,
                          const QString& version,
                          std::function<IOperator*()> creator);

    /// 按 type 创建实例；不存在返回 nullptr
    IOperator* createOperator(const QString& type);

    /// 返回已注册 type 列表；空注册表返回空列表（RT-005）
    QStringList availableTypes() const;

    /// 查询是否已注册
    bool isRegistered(const QString& type) const;

    /// 查询算子接口版本
    QString versionOf(const QString& type) const;

private:
    IOperatorRegistry() = default;
    ~IOperatorRegistry() = default;
    IOperatorRegistry(const IOperatorRegistry&) = delete;
    IOperatorRegistry& operator=(const IOperatorRegistry&) = delete;

    struct Entry {
        QString version;
        std::function<IOperator*()> creator;
    };
    QMap<QString, Entry> m_creators;  // type -> (version, creator)
};

} // namespace QDV

#endif // QDV_IOPERATOR_REGISTRY_H
