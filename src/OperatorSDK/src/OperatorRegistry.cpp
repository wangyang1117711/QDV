#include "OperatorSDK/IOperatorRegistry.h"

namespace QDV {

IOperatorRegistry& IOperatorRegistry::instance() {
    // 单例：首次调用时创建，C++11 线程安全保证
    static IOperatorRegistry s_instance;
    return s_instance;
}

bool IOperatorRegistry::registerOperator(const QString& type,
                                         const QString& version,
                                         std::function<IOperator*()> creator) {
    // RT-001：重复 type 返回 false，不覆盖原注册
    if (m_creators.contains(type)) {
        return false;
    }
    Entry entry;
    entry.version = version;
    entry.creator = std::move(creator);
    m_creators.insert(type, entry);
    return true;
}

IOperator* IOperatorRegistry::createOperator(const QString& type) {
    auto it = m_creators.find(type);
    if (it == m_creators.end()) {
        // RT-005：未注册返回 nullptr
        return nullptr;
    }
    return it.value().creator();
}

QStringList IOperatorRegistry::availableTypes() const {
    // RT-005：空注册表返回空列表
    return m_creators.keys();
}

bool IOperatorRegistry::isRegistered(const QString& type) const {
    return m_creators.contains(type);
}

QString IOperatorRegistry::versionOf(const QString& type) const {
    auto it = m_creators.find(type);
    if (it == m_creators.end()) {
        return QString();
    }
    return it.value().version;
}

} // namespace QDV
