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

    // S3 修复：加载前 SHA256 校验
    // 返回值：true=校验通过可加载，false=校验失败拒绝加载
    bool verifyPluginHash(const QString& filePath);

    // S3 修复：计算文件 SHA256 摘要（读取整个文件分块哈希）
    static QString computeFileSha256(const QString& filePath);

    // S3 修复：定位 trusted_plugins.json 配置文件路径
    // 搜索顺序：<appdir>/config/trusted_plugins.json → <appdir>/trusted_plugins.json
    static QString locateTrustedPluginsConfig();

    QMap<QString, IPlugin*> m_plugins;
    QMap<QString, QLibrary*> m_libraries;

    static PluginManager* s_instance;
};

#endif // PLUGIN_MANAGER_H