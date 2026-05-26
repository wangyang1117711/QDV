#include "PluginManager.h"
#include "IPlugin.h"
#include <QDir>

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
    QStringList filters;
    filters << "*.dll";
    
    QStringList pluginFiles = dir.entryList(filters, QDir::Files);
    
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
        delete library;
        return false;
    }
    
    typedef IPlugin* (*CreatePluginFunc)();
    CreatePluginFunc createFunc = reinterpret_cast<CreatePluginFunc>(library->resolve("createPlugin"));
    
    if (!createFunc) {
        library->unload();
        delete library;
        return false;
    }
    
    IPlugin* plugin = createFunc();
    if (!plugin) {
        library->unload();
        delete library;
        return false;
    }
    
    m_plugins[plugin->name()] = plugin;
    m_libraries[plugin->name()] = library;
    
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