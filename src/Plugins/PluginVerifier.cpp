#include "Plugins/PluginVerifier.h"
#include "Plugins/TestPlugin.h"
#include "Plugins/PluginManager.h"

PluginVerifier::PluginVerifier(QObject* parent) : QObject(parent) {
}

QList<PluginVerifyResult> PluginVerifier::verifyAll() {
    m_results.clear();
    m_results.append(verifyTestPluginCreation());
    m_results.append(verifyPluginLifecycle());
    m_results.append(verifyPluginExecution());
    m_results.append(verifyPluginManagerEnumeration());
    return m_results;
}

PluginVerifyResult PluginVerifier::verifyTestPluginCreation() {
    PluginVerifyResult r;
    r.checkName = "插件创建";

    TestPlugin* plugin = new TestPlugin();
    if (!plugin) {
        r.passed = false;
        r.message = "TestPlugin分配失败";
        return r;
    }

    r.passed = (plugin->name() == "TestPlugin")
            && (plugin->version() == "1.0.0");
    r.message = r.passed ? "TestPlugin创建并查询属性成功"
                         : "TestPlugin属性查询失败";
    delete plugin;
    return r;
}

PluginVerifyResult PluginVerifier::verifyPluginLifecycle() {
    PluginVerifyResult r;
    r.checkName = "插件生命周期";

    TestPlugin* plugin = new TestPlugin();

    bool initOk = plugin->initialize();
    bool enableOk = initOk && plugin->enable();
    bool isEnabled = plugin->isEnabled();
    bool disableOk = plugin->disable();
    bool isDisabled = !plugin->isEnabled();

    plugin->cleanup();

    r.passed = initOk && enableOk && isEnabled && disableOk && isDisabled;
    r.message = r.passed
        ? "初始化→启用→禁用→清理 全部正常"
        : "插件生命周期验证失败";
    delete plugin;
    return r;
}

PluginVerifyResult PluginVerifier::verifyPluginExecution() {
    PluginVerifyResult r;
    r.checkName = "插件执行";

    TestPlugin* plugin = new TestPlugin();
    plugin->initialize();
    plugin->enable();

    QString result = plugin->executeTest("hello");
    r.passed = result.contains("hello");

    plugin->cleanup();
    r.message = r.passed
        ? "TestPlugin执行测试通过: " + result
        : "TestPlugin执行返回异常";
    delete plugin;
    return r;
}

PluginVerifyResult PluginVerifier::verifyPluginManagerEnumeration() {
    PluginVerifyResult r;
    r.checkName = "插件管理器";

    PluginManager* mgr = PluginManager::instance();
    if (!mgr) {
        r.passed = false;
        r.message = "PluginManager单例为空";
        return r;
    }

    QStringList loaded = mgr->loadedPlugins();
    r.passed = true;
    r.message = "PluginManager枚举成功，已加载插件数: "
                + QString::number(loaded.size());
    return r;
}

int PluginVerifier::passedCount() const {
    int count = 0;
    for (const auto& r : m_results) {
        if (r.passed) count++;
    }
    return count;
}

int PluginVerifier::totalCount() const {
    return m_results.size();
}