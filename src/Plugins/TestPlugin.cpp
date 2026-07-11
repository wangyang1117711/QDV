#include "Plugins/TestPlugin.h"
#include "Core/Logger.h"

using namespace QDV;

TestPlugin::TestPlugin() : IPlugin() {
}

bool TestPlugin::initialize() {
    Logger::info("TestPlugin::initialize() called");
    m_initialized = true;
    return true;
}

bool TestPlugin::enable() {
    if (!m_initialized) return false;
    Logger::info("TestPlugin::enable() called");
    m_enabled = true;
    return true;
}

bool TestPlugin::disable() {
    Logger::info("TestPlugin::disable() called");
    m_enabled = false;
    return true;
}

void TestPlugin::cleanup() {
    Logger::info("TestPlugin::cleanup() called");
    m_initialized = false;
    m_enabled = false;
}

QString TestPlugin::executeTest(const QString& input) {
    QString result = "TestPlugin processed: " + input;
    emit testCompleted(result);
    return result;
}

IPlugin* createPlugin() {
    return new TestPlugin();
}