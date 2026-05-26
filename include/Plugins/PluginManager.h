#ifndef PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QLibrary>

class IPlugin;

class PluginManager : public QObject {
    Q_OBJECT
    
public:
    static PluginManager* instance();
    
    bool loadPlugins(const QString& directory);
    bool loadPlugin(const QString& filePath);
    
    void unloadPlugins();
    
    IPlugin* getPlugin(const QString& name);
    
    QStringList loadedPlugins() const;
    
signals:
    void pluginLoaded(const QString& name);
    void pluginUnloaded(const QString& name);
    
private:
    PluginManager(QObject* parent = nullptr);
    ~PluginManager();
    
    QMap<QString, IPlugin*> m_plugins;
    QMap<QString, QLibrary*> m_libraries;
    
    static PluginManager* s_instance;
};

#endif // PLUGIN_MANAGER_H