#ifndef TEST_PLUGIN_H
#define TEST_PLUGIN_H

// P1-C9 + P1-C12 修复：IPlugin 已继承 QObject，TestPlugin 不再多继承。
// 接口方法签名已对齐，所有 override 现在都是真正的 override（不再是新虚函数）。
#include "Plugins/IPlugin.h"

class TestPlugin : public IPlugin {
    Q_OBJECT
    Q_INTERFACES(IPlugin)

public:
    TestPlugin();

    QString name() const override { return "TestPlugin"; }
    QString version() const override { return "1.0.0"; }
    QString description() const override { return "测试插件 - 验证插件架构"; }

    bool initialize() override;
    bool enable() override;
    bool disable() override;
    void cleanup() override;

    bool isEnabled() const override { return m_enabled; }

    QString executeTest(const QString& input);

signals:
    void testCompleted(const QString& result);

private:
    bool m_enabled = false;
    bool m_initialized = false;
};

extern "C" {
    Q_DECL_EXPORT IPlugin* createPlugin();
}

#endif // TEST_PLUGIN_H
