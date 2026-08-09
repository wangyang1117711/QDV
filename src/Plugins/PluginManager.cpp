#include "PluginManager.h"
#include "IPlugin.h"
#include "Core/Logger.h"
#include "Core/PathValidator.h"  // S6 修复：路径校验
#include <QDir>
#include <QLibrary>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

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

QString PluginManager::computeFileSha256(const QString& filePath) {
    // S3 修复：分块读取文件计算 SHA256，避免一次性加载大 DLL 到内存
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QDV::Logger::error(QString("PluginManager::computeFileSha256: cannot open %1").arg(filePath));
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    constexpr qint64 kChunkSize = 64 * 1024;  // 64KB 分块
    while (!file.atEnd()) {
        QByteArray chunk = file.read(kChunkSize);
        if (chunk.isEmpty()) {
            break;
        }
        hash.addData(chunk);
    }
    file.close();
    return QString::fromLatin1(hash.result().toHex());
}

QString PluginManager::locateTrustedPluginsConfig() {
    // S3 修复：搜索 trusted_plugins.json 配置文件
    // 优先级：<appdir>/config/trusted_plugins.json → <appdir>/trusted_plugins.json
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/config/trusted_plugins.json",
        appDir + "/trusted_plugins.json"
    };
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return QString();
}

bool PluginManager::verifyPluginHash(const QString& filePath) {
    // S3 修复：加载前 SHA256 校验
    // 校验策略：
    //   1. 配置文件存在 → 严格白名单模式：哈希必须在白名单内，否则拒绝
    //   2. 配置文件缺失 → 兼容模式（fail-open）：记录 CRITICAL 警告但允许加载
    //      部署方应尽快部署 trusted_plugins.json 启用严格模式
    //   3. 环境变量 QDV_STRICT_PLUGIN_VERIFY=1 → 即使配置缺失也强制 fail-closed

    const QString configPath = locateTrustedPluginsConfig();

    if (configPath.isEmpty()) {
        // 配置缺失：检查是否强制严格模式
        const QByteArray strictEnv = qgetenv("QDV_STRICT_PLUGIN_VERIFY").toLower().trimmed();
        if (strictEnv == "1" || strictEnv == "true" || strictEnv == "yes") {
            QDV::Logger::error(
                QString("PluginManager::verifyPluginHash: STRICT MODE — config missing, rejecting %1. "
                        "Deploy trusted_plugins.json or unset QDV_STRICT_PLUGIN_VERIFY.")
                    .arg(filePath));
            return false;
        }
        // 兼容模式：fail-open + 明确告警（部署方应迁移到严格模式）
        QDV::Logger::warn(
            QString("PluginManager::verifyPluginHash: trusted_plugins.json NOT FOUND, "
                    "loading %1 WITHOUT hash verification (fail-open compat mode). "
                    "Deploy config/trusted_plugins.json to enable strict verification.")
                .arg(filePath));
        return true;
    }

    // 加载白名单配置
    QFile configFile(configPath);
    if (!configFile.open(QIODevice::ReadOnly)) {
        QDV::Logger::error(
            QString("PluginManager::verifyPluginHash: cannot read config %1, rejecting %2 (fail-closed)")
                .arg(configPath, filePath));
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(configFile.readAll(), &parseError);
    configFile.close();
    if (parseError.error != QJsonParseError::NoError) {
        QDV::Logger::error(
            QString("PluginManager::verifyPluginHash: invalid JSON in %1: %2, rejecting %3 (fail-closed)")
                .arg(configPath, parseError.errorString(), filePath));
        return false;
    }

    // 解析白名单：{ "trusted_plugins": [ { "name": "...", "sha256": "..." }, ... ] }
    QJsonObject root = doc.object();
    QJsonArray plugins = root.value("trusted_plugins").toArray();

    // 计算待加载 DLL 的 SHA256
    const QString actualHash = computeFileSha256(filePath);
    if (actualHash.isEmpty()) {
        QDV::Logger::error(
            QString("PluginManager::verifyPluginHash: failed to compute SHA256 for %1, rejecting")
                .arg(filePath));
        return false;
    }

    const QString fileName = QFileInfo(filePath).fileName();
    bool hashMatched = false;
    bool nameFound = false;

    for (const QJsonValue& v : plugins) {
        QJsonObject entry = v.toObject();
        QString entryName = entry.value("name").toString();
        QString entryHash = entry.value("sha256").toString().toLower().trimmed();

        if (entryName.compare(fileName, Qt::CaseInsensitive) == 0) {
            nameFound = true;
            if (!entryHash.isEmpty() && entryHash == actualHash.toLower()) {
                hashMatched = true;
                break;
            }
        }
    }

    if (hashMatched) {
        QDV::Logger::info(
            QString("PluginManager::verifyPluginHash: %1 SHA256 verified (%2)")
                .arg(fileName, actualHash.left(16) + "..."));
        return true;
    }

    if (nameFound) {
        // 文件名匹配但哈希不匹配 → 篡改嫌疑，拒绝
        QDV::Logger::error(
            QString("PluginManager::verifyPluginHash: HASH MISMATCH for %1 — possible tampering! "
                    "Rejecting load. Computed=%2...")
                .arg(filePath).arg(actualHash.left(16)));
    } else {
        // 文件名不在白名单 → 拒绝（fail-closed）
        QDV::Logger::error(
            QString("PluginManager::verifyPluginHash: %1 not in trusted_plugins.json whitelist. "
                    "Rejecting load. To trust this plugin, add its SHA256 (%2...) to config.")
                .arg(fileName).arg(actualHash.left(16)));
    }
    return false;
}

bool PluginManager::loadPlugin(const QString& filePath) {
    // S6 修复：清理路径，防止路径穿越与非法字符
    QString sanitizedPath = QDV::PathValidator::sanitize(filePath);
    if (sanitizedPath.isEmpty()) {
        QDV::Logger::error(QString("PluginManager::loadPlugin: path rejected by sanitize: %1").arg(filePath));
        return false;
    }

    // S3 修复：加载前先进行 SHA256 白名单校验，未通过则拒绝加载
    if (!verifyPluginHash(sanitizedPath)) {
        // 校验失败：verifyPluginHash 内部已记录详细日志
        return false;
    }

    QLibrary* library = new QLibrary(sanitizedPath);
    if (!library->load()) {
        // P1-C16 修复：记录加载失败的详细错误
        QDV::Logger::error(QString("PluginManager::loadPlugin: failed to load %1: %2")
                      .arg(sanitizedPath).arg(library->errorString()));
        delete library;
        return false;
    }

    typedef IPlugin* (*CreatePluginFunc)();
    CreatePluginFunc createFunc = reinterpret_cast<CreatePluginFunc>(library->resolve("createPlugin"));

    if (!createFunc) {
        QDV::Logger::error(QString("PluginManager::loadPlugin: createPlugin symbol not found in %1: %2")
                      .arg(sanitizedPath).arg(library->errorString()));
        library->unload();
        delete library;
        return false;
    }

    IPlugin* plugin = createFunc();
    if (!plugin) {
        QDV::Logger::error(QString("PluginManager::loadPlugin: createPlugin returned null in %1").arg(sanitizedPath));
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
                 .arg(plugin->name()).arg(plugin->version()).arg(sanitizedPath));
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