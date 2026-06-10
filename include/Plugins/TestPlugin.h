#ifndef TEST_PLUGIN_H
#define TEST_PLUGIN_H

#include "Plugins/IPlugin.h"
#include <QObject>

class TestPlugin : public QObject, public IPlugin {
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

#endif