#include "PluginManager.h"
#include "IPlugin.h"
#include "Core/Logger.h"
#include <QDir>
#include <QLibrary>

PluginManager* PluginManager::s_instance = nullptr;

PluginManager::PluginManager(QObject* parent) : QObject(parent) {
}

PluginManager::~PluginManager() {
    unloadPlugins();
}

PluginManager* PluginManager::instance() {
    if (!s_instance) {
        s_instance = new PluginManager();
    }
    return s_instance;
}

bool PluginManager::loadPlugins(const QString& directory) {
    QDir dir(directory);
    if (!dir.exists()) {
        // P1-C16 修复（架构评估 D6）：插件目录不存在时记录警告而非静默返回
        QDV::Logger::warn(QString("PluginManager::loadPlugins: directory does not exist: %1").arg(directory));
        return false;
    }
    QStringList filters;
    filters << "*.dll";

    QStringList pluginFiles = dir.entryList(filters, QDir::Files);
    if (pluginFiles.isEmpty()) {
        QDV::Logger::info(QString("PluginManager::loadPlugins: no plugins found in %1").arg(directory));
        return true;  // 目录存在但无插件，不算失败
    }

    bool allLoaded = true;
    for (const QString& file : pluginFiles) {
        if (!loadPlugin(dir.absoluteFilePath(file))) {
            allLoaded = false;
        }
    }

    return allLoaded;
}

bool PluginManager::loadPlugin(const QString& filePath) {
    QLibrary* library = new QLibrary(filePath);
    if (!library->load()) {
        // P1-C16 修复：记录加载失败的详细错误
        QDV::Logger::error(QString("PluginManager::loadPlugin: failed to load %1: %2")
                      .arg(filePath).arg(library->errorString()));
        delete library;
        return false;
    }

    typedef IPlugin* (*CreatePluginFunc)();
    CreatePluginFunc createFunc = reinterpret_cast<CreatePluginFunc>(library->resolve("createPlugin"));

    if (!createFunc) {
        QDV::Logger::error(QString("PluginManager::loadPlugin: createPlugin symbol not found in %1: %2")
                      .arg(filePath).arg(library->errorString()));
        library->unload();
        delete library;
        return false;
    }

    IPlugin* plugin = createFunc();
    if (!plugin) {
        QDV::Logger::error(QString("PluginManager::loadPlugin: createPlugin returned null in %1").arg(filePath));
        library->unload();
        delete library;
        return false;
    }

    // P1-C9 修复：IPlugin 现已对齐 TestPlugin 接口，调用 initialize() 启用插件
    if (!plugin->initialize()) {
        QDV::Logger::warn(QString("PluginManager::loadPlugin: plugin %1 initialize() failed")
                     .arg(plugin->name()));
    }

    m_plugins[plugin->name()] = plugin;
    m_libraries[plugin->name()] = library;

    QDV::Logger::info(QString("Plugin loaded: %1 v%2 from %3")
                 .arg(plugin->name()).arg(plugin->version()).arg(filePath));
    emit pluginLoaded(plugin->name());
    return true;
}

void PluginManager::unloadPlugins() {
    for (IPlugin* plugin : m_plugins.values()) {
        plugin->cleanup();
        delete plugin;
    }

    for (QLibrary* library : m_libraries.values()) {
        library->unload();
        delete library;
    }

    m_plugins.clear();
    m_libraries.clear();
}

IPlugin* PluginManager::getPlugin(const QString& name) {
    return m_plugins.value(name, nullptr);
}

QStringList PluginManager::loadedPlugins() const {
    return m_plugins.keys();
}