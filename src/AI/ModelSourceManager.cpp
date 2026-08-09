// ============================================================================
// ModelSourceManager.cpp - 模型源路径管理器实现（v2.0 阶段七 Task 16-17）
//
// 实现：
// - 单例 + QMutex 线程安全
// - 加载/保存 config/model_sources.json
// - 递归扫描各源路径发现模型（.gguf/.onnx/.pt/.safetensors/.bin/.pth）
// - QFileSystemWatcher 监控路径变化，500ms 防抖合并扫描
// - 未挂载路径 30s 周期检查，恢复后自动重新监控
// - 信号通知 UI 刷新（modelListChanged / sourceStatusChanged / newModelsDetected）
// ============================================================================

#include "AI/ModelSourceManager.h"
#include "Core/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QCoreApplication>

namespace QDV {

// ========================================================================
// 静态成员初始化
// ========================================================================
ModelSourceManager* ModelSourceManager::s_instance = nullptr;
QMutex              ModelSourceManager::s_instanceMutex;

// ========================================================================
// ModelEntry 序列化
// ========================================================================

QVariantMap ModelEntry::toMap() const {
    QVariantMap m;
    m["name"]         = name;
    m["path"]         = path;
    m["sourceType"]   = sourceType;
    m["size"]         = static_cast<qint64>(size);
    m["modifiedTime"] = modifiedTime.toString(Qt::ISODate);
    m["parentPath"]   = parentPath;
    return m;
}

ModelEntry ModelEntry::fromMap(const QVariantMap& m) {
    ModelEntry e;
    e.name         = m.value("name").toString();
    e.path         = m.value("path").toString();
    e.sourceType   = m.value("sourceType").toString();
    e.size         = static_cast<qint64>(m.value("size").toLongLong());
    e.modifiedTime = QDateTime::fromString(m.value("modifiedTime").toString(), Qt::ISODate);
    e.parentPath   = m.value("parentPath").toString();
    return e;
}

// ========================================================================
// ModelSource 序列化
// ========================================================================

QVariantMap ModelSource::toMap() const {
    QVariantMap m;
    m["path"]       = path;
    m["sourceType"] = sourceType;
    m["enabled"]    = enabled;
    m["description"] = description;
    return m;
}

ModelSource ModelSource::fromMap(const QVariantMap& m) {
    ModelSource s;
    s.path        = m.value("path").toString();
    s.sourceType  = m.value("sourceType").toString();
    s.enabled     = m.value("enabled", true).toBool();
    s.description = m.value("description").toString();
    return s;
}

// ========================================================================
// 单例
// ========================================================================

ModelSourceManager* ModelSourceManager::instance() {
    QMutexLocker locker(&s_instanceMutex);
    if (!s_instance) {
        s_instance = new ModelSourceManager();
    }
    return s_instance;
}

ModelSourceManager::ModelSourceManager(QObject* parent)
    : QObject(parent)
{
    // 初始化文件系统监控器
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &ModelSourceManager::onFileChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &ModelSourceManager::onDirectoryChanged);

    // 防抖定时器：500ms 内多次变化合并为一次扫描
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(DEBOUNCE_MS);
    connect(m_debounceTimer, &QTimer::timeout,
            this, &ModelSourceManager::onDebounceTimeout);

    // 未挂载路径周期检查定时器：30s
    m_unmountedCheckTimer = new QTimer(this);
    m_unmountedCheckTimer->setInterval(UNMOUNTED_CHECK_MS);
    connect(m_unmountedCheckTimer, &QTimer::timeout,
            this, &ModelSourceManager::onUnmountedCheckTimeout);

    // 构造时立即加载默认配置（失败则用 builtinDefaults 兜底）
    QString err;
    loadFromJson(QString(), &err);

    // 首次扫描，建立初始模型清单
    rescan();
}

// ========================================================================
// 默认配置路径
// ========================================================================

QString ModelSourceManager::defaultConfigPath() {
    // <exe-dir>/config/model_sources.json
    const QString dir = QCoreApplication::applicationDirPath() + "/config";
    QDir().mkpath(dir);
    return dir + "/model_sources.json";
}

// ========================================================================
// 内置默认源路径
// ========================================================================

QList<ModelSource> ModelSourceManager::builtinDefaults() {
    QList<ModelSource> list;
    // 1. LM Studio 预设路径（F 盘，非 C 盘，符合存储要求）
    ModelSource lm;
    lm.path        = QStringLiteral("F:\\models\\.lmstudio\\models");
    lm.sourceType  = ModelSourceTypes::LMSTUDIO;
    lm.enabled     = true;
    lm.description = QStringLiteral("预设路径，存放 LM Studio 下载的本地大模型");
    list.append(lm);

    // 2. 现有模型路径（项目源码目录 models/，开发调试用）
    ModelSource localProject;
    localProject.path        = QCoreApplication::applicationDirPath() + QStringLiteral("/../models");
    localProject.sourceType  = ModelSourceTypes::LOCAL;
    localProject.enabled     = true;
    localProject.description = QStringLiteral("项目源码目录模型（开发调试）");
    list.append(localProject);

    // 3. 现有模型路径（D 盘公共）
    ModelSource local;
    local.path        = QStringLiteral("D:\\models");
    local.sourceType  = ModelSourceTypes::LOCAL;
    local.enabled     = true;
    local.description = QStringLiteral("现有模型路径（ONNX/零样本模型）");
    list.append(local);

    // 4. 训练 YOLO 产物目录（D 盘）
    ModelSource trained;
    trained.path        = QStringLiteral("D:\\QDV\\trained");
    trained.sourceType  = ModelSourceTypes::TRAINED;
    trained.enabled     = true;
    trained.description = QStringLiteral("训练 YOLO 产物目录");
    list.append(trained);

    // 5. 项目目录下 trained（开发调试）
    ModelSource trainedProject;
    trainedProject.path        = QCoreApplication::applicationDirPath() + QStringLiteral("/../trained");
    trainedProject.sourceType  = ModelSourceTypes::TRAINED;
    trainedProject.enabled     = true;
    trainedProject.description = QStringLiteral("项目目录训练产物（开发调试）");
    list.append(trainedProject);

    return list;
}

// ========================================================================
// 加载 / 保存 JSON
// ========================================================================

bool ModelSourceManager::loadFromJson(const QString& path, QString* outError) {
    QMutexLocker locker(&m_mutex);

    const QString loadPath = path.isEmpty() ? defaultConfigPath() : path;
    m_loadedPath = loadPath;

    QFile file(loadPath);
    if (!file.exists()) {
        // 文件不存在：回退到内置默认，不视为错误
        if (outError) *outError = QStringLiteral("配置文件不存在，使用内置默认: %1").arg(loadPath);
        m_sources = builtinDefaults();
        Logger::info(QStringLiteral("[ModelSourceManager] 配置文件不存在，使用内置默认: %1").arg(loadPath));
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        if (outError) *outError = QStringLiteral("无法打开配置文件: %1").arg(loadPath);
        Logger::warn(QStringLiteral("[ModelSourceManager] 无法打开配置文件，使用内置默认: %1").arg(loadPath));
        m_sources = builtinDefaults();
        return false;
    }

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    file.close();

    if (parseErr.error != QJsonParseError::NoError) {
        if (outError) *outError = QStringLiteral("JSON 解析失败: %1").arg(parseErr.errorString());
        Logger::warn(QStringLiteral("[ModelSourceManager] JSON 解析失败，使用内置默认: %1").arg(parseErr.errorString()));
        m_sources = builtinDefaults();
        return false;
    }

    QJsonObject root = doc.object();
    QJsonArray arr = root.value("sources").toArray();
    QList<ModelSource> loaded;
    for (const QJsonValue& v : arr) {
        if (!v.isObject()) continue;
        ModelSource s = ModelSource::fromMap(v.toObject().toVariantMap());
        if (!s.path.isEmpty()) {
            loaded.append(s);
        }
    }

    if (loaded.isEmpty()) {
        // 空配置：回退到默认
        if (outError) *outError = QStringLiteral("配置文件 sources 为空，使用内置默认");
        m_sources = builtinDefaults();
    } else {
        m_sources = loaded;
    }

    Logger::info(QStringLiteral("[ModelSourceManager] 已加载 %1 个源路径配置: %2")
                     .arg(m_sources.size()).arg(loadPath));
    return true;
}

bool ModelSourceManager::saveToJson(const QString& path) {
    QMutexLocker locker(&m_mutex);

    const QString savePath = path.isEmpty() ? (m_loadedPath.isEmpty() ? defaultConfigPath() : m_loadedPath) : path;

    QJsonObject root;
    root["version"] = 1;
    root["comment"] = QStringLiteral("Q-DetectVision 模型源路径配置 (v2.0 阶段七)");

    QJsonArray arr;
    for (const ModelSource& s : m_sources) {
        arr.append(QJsonObject::fromVariantMap(s.toMap()));
    }
    root["sources"] = arr;

    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Logger::warn(QStringLiteral("[ModelSourceManager] 无法写入配置文件: %1").arg(savePath));
        return false;
    }
    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    m_loadedPath = savePath;
    Logger::info(QStringLiteral("[ModelSourceManager] 已保存 %1 个源路径配置: %2")
                     .arg(m_sources.size()).arg(savePath));
    return true;
}

// ========================================================================
// 源路径管理
// ========================================================================

QList<ModelSource> ModelSourceManager::sources() const {
    QMutexLocker locker(&m_mutex);
    return m_sources;
}

QList<ModelSource> ModelSourceManager::enabledSources() const {
    QMutexLocker locker(&m_mutex);
    QList<ModelSource> result;
    for (const ModelSource& s : m_sources) {
        if (s.enabled) result.append(s);
    }
    return result;
}

void ModelSourceManager::addSource(const QString& path, const QString& sourceType,
                                   const QString& description) {
    if (path.isEmpty()) return;
    bool found = false;
    {
        QMutexLocker locker(&m_mutex);
        // 已存在则更新
        for (ModelSource& s : m_sources) {
            if (s.path == path) {
                s.sourceType = sourceType;
                if (!description.isEmpty()) s.description = description;
                found = true;
                Logger::info(QStringLiteral("[ModelSourceManager] 更新源路径: %1 (type=%2)").arg(path).arg(sourceType));
                break;
            }
        }
        if (!found) {
            // 新增
            ModelSource s;
            s.path = path;
            s.sourceType = sourceType;
            s.enabled = true;
            s.description = description.isEmpty() ? QStringLiteral("用户自定义路径") : description;
            m_sources.append(s);
            Logger::info(QStringLiteral("[ModelSourceManager] 新增源路径: %1 (type=%2)").arg(path).arg(sourceType));
        }
    }
    saveToJson();
    rescan();
    if (m_watching) setupWatcher();
}

void ModelSourceManager::removeSource(const QString& path) {
    if (path.isEmpty()) return;
    {
        QMutexLocker locker(&m_mutex);
        for (int i = 0; i < m_sources.size(); ++i) {
            if (m_sources[i].path == path) {
                m_sources.removeAt(i);
                // 同步移除该路径下的模型条目
                QList<ModelEntry> remaining;
                for (const ModelEntry& e : m_models) {
                    if (e.parentPath != path) remaining.append(e);
                }
                m_models = remaining;
                m_sourceStatuses.remove(path);
                Logger::info(QStringLiteral("[ModelSourceManager] 移除源路径: %1").arg(path));
                break;
            }
        }
    }
    saveToJson();
    if (m_watching) setupWatcher();
    // 通知 UI 刷新
    emit modelListChanged(allModelsAsVariant());
}

void ModelSourceManager::setSourceEnabled(const QString& path, bool enabled) {
    {
        QMutexLocker locker(&m_mutex);
        for (ModelSource& s : m_sources) {
            if (s.path == path) {
                if (s.enabled == enabled) return;
                s.enabled = enabled;
                Logger::info(QStringLiteral("[ModelSourceManager] 源路径 %1 启用状态: %2")
                                 .arg(path).arg(enabled ? "启用" : "禁用"));
                break;
            }
        }
    }
    saveToJson();
    rescan();
    if (m_watching) setupWatcher();
}

// ========================================================================
// 模型清单查询
// ========================================================================

QList<ModelEntry> ModelSourceManager::allModels() const {
    QMutexLocker locker(&m_mutex);
    return m_models;
}

QList<ModelEntry> ModelSourceManager::modelsBySource(const QString& sourceType) const {
    QMutexLocker locker(&m_mutex);
    QList<ModelEntry> result;
    for (const ModelEntry& e : m_models) {
        if (e.sourceType == sourceType) result.append(e);
    }
    return result;
}

QVariantList ModelSourceManager::allModelsAsVariant() const {
    QMutexLocker locker(&m_mutex);
    QVariantList list;
    for (const ModelEntry& e : m_models) {
        QVariantMap m = e.toMap();
        // 标记是否为新增（首次扫描时不标记，避免全部显示"新"）
        m["isNew"] = m_hasLastSnapshot ? m_newModelPaths.contains(e.path) : false;
        list.append(m);
    }
    return list;
}

bool ModelSourceManager::isLmStudioAvailable() const {
    QMutexLocker locker(&m_mutex);
    // 检查 LM Studio 源路径是否存在且至少有 1 个模型
    for (const ModelSource& s : m_sources) {
        if (s.sourceType == ModelSourceTypes::LMSTUDIO && s.enabled) {
            const QString status = m_sourceStatuses.value(s.path);
            return status == ModelSourceStatus::MOUNTED;
        }
    }
    return false;
}

// ========================================================================
// 扫描
// ========================================================================

bool ModelSourceManager::isModelFile(const QString& fileName) const {
    // 识别模型文件扩展名（不区分大小写）
    // .gguf:        LM Studio / llama.cpp 大模型格式
    // .onnx:        ONNX 通用推理格式
    // .pt/.pth:     PyTorch 训练产物
    // .safetensors: HuggingFace 安全张量格式
    // .bin:         通用二进制模型（部分大模型使用）
    const QString lower = fileName.toLower();
    return lower.endsWith(".gguf") ||
           lower.endsWith(".onnx") ||
           lower.endsWith(".pt") ||
           lower.endsWith(".pth") ||
           lower.endsWith(".safetensors") ||
           lower.endsWith(".bin");
}

QList<ModelEntry> ModelSourceManager::scanDirectory(const QString& rootPath,
                                                    const QString& sourceType,
                                                    const QString& sourceRoot) const {
    QList<ModelEntry> entries;
    // 使用 QDirIterator 递归扫描（支持中文路径，QDir::Readable 过滤可读）
    QDirIterator it(rootPath, QDir::NoDotAndDotDot | QDir::AllEntries, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (info.isFile() && isModelFile(info.fileName())) {
            ModelEntry e;
            e.name = info.fileName();
            e.path = info.absoluteFilePath();
            e.sourceType = sourceType;
            e.size = info.size();
            e.modifiedTime = info.lastModified();
            e.parentPath = sourceRoot;
            entries.append(e);
        }
        // 含模型文件的目录（如 D:\QDV\trained\rust_v1\model.onnx + labels.txt）
        // 暂不将目录作为单独条目，避免重复（文件级条目已足够）
    }
    return entries;
}

void ModelSourceManager::scanSource(const ModelSource& source, QList<ModelEntry>& outEntries) {
    const QFileInfo fi(source.path);
    if (!fi.exists() || !fi.isDir()) {
        // 路径不存在：标记未挂载
        updateSourceStatus(source.path, ModelSourceStatus::UNMOUNTED);
        return;
    }

    // 路径存在，扫描
    QList<ModelEntry> found = scanDirectory(source.path, source.sourceType, source.path);

    if (found.isEmpty()) {
        // 路径存在但无模型：标记空
        updateSourceStatus(source.path, ModelSourceStatus::EMPTY);
    } else {
        updateSourceStatus(source.path, ModelSourceStatus::MOUNTED);
        outEntries.append(found);
    }
}

int ModelSourceManager::rescan() {
    QList<ModelSource> toScan = enabledSources();
    QList<ModelEntry> newModels;
    QMap<QString, QString> newStatuses;

    // 扫描每个启用源
    for (const ModelSource& s : toScan) {
        scanSource(s, newModels);
    }

    // 对未启用源也更新状态（路径不存在则未挂载，存在则空）
    {
        QMutexLocker locker(&m_mutex);
        for (const ModelSource& s : m_sources) {
            if (!s.enabled) {
                const QFileInfo fi(s.path);
                if (!fi.exists() || !fi.isDir()) {
                    newStatuses[s.path] = ModelSourceStatus::UNMOUNTED;
                } else {
                    newStatuses[s.path] = ModelSourceStatus::EMPTY;
                }
            }
        }
    }

    // 计算新增模型（与上次快照对比；首次扫描不标记新增）
    QSet<QString> currentPaths;
    for (const ModelEntry& e : newModels) {
        currentPaths.insert(e.path);
    }

    QSet<QString> newPaths;
    {
        QMutexLocker locker(&m_mutex);
        if (m_hasLastSnapshot) {
            // 在当前但不在上次 = 新增
            for (const QString& p : currentPaths) {
                if (!m_lastModelPaths.contains(p)) {
                    newPaths.insert(p);
                }
            }
        }
        // 更新内部状态
        m_models = newModels;
        m_newModelPaths = newPaths;
        m_lastModelPaths = currentPaths;
        m_hasLastSnapshot = true;

        // 合并未启用源状态到 m_sourceStatuses
        for (auto it = newStatuses.constBegin(); it != newStatuses.constEnd(); ++it) {
            m_sourceStatuses[it.key()] = it.value();
        }
    }

    Logger::info(QStringLiteral("[ModelSourceManager] 扫描完成: %1 个模型, 新增 %2 个")
                     .arg(newModels.size()).arg(newPaths.size()));

    // 发射信号（在锁外）
    emit modelListChanged(allModelsAsVariant());
    if (!newPaths.isEmpty()) {
        emit newModelsDetected(newPaths);
    }

    return newModels.size();
}

// ========================================================================
// 文件系统监控
// ========================================================================

void ModelSourceManager::startWatching() {
    if (m_watching) return;
    m_watching = true;
    setupWatcher();
    m_unmountedCheckTimer->start();
    Logger::info(QStringLiteral("[ModelSourceManager] 文件系统监控已启动 (防抖 %1ms, 未挂载检查 %2ms)")
                     .arg(DEBOUNCE_MS).arg(UNMOUNTED_CHECK_MS));
}

void ModelSourceManager::stopWatching() {
    if (!m_watching) return;
    m_watching = false;
    // 清空 watcher 监控路径
    const QStringList dirs = m_watcher->directories();
    if (!dirs.isEmpty()) m_watcher->removePaths(dirs);
    const QStringList files = m_watcher->files();
    if (!files.isEmpty()) m_watcher->removePaths(files);
    m_unmountedCheckTimer->stop();
    m_debounceTimer->stop();
    Logger::info(QStringLiteral("[ModelSourceManager] 文件系统监控已停止"));
}

bool ModelSourceManager::isWatching() const {
    return m_watching;
}

void ModelSourceManager::setupWatcher() {
    // 清空旧监控路径
    const QStringList oldDirs = m_watcher->directories();
    if (!oldDirs.isEmpty()) m_watcher->removePaths(oldDirs);
    const QStringList oldFiles = m_watcher->files();
    if (!oldFiles.isEmpty()) m_watcher->removePaths(oldFiles);

    if (!m_watching) return;

    // 为每个已启用且存在的源路径建立监控
    QList<ModelSource> enabled = enabledSources();
    for (const ModelSource& s : enabled) {
        const QFileInfo fi(s.path);
        if (!fi.exists() || !fi.isDir()) {
            // 路径不存在，跳过监控（由未挂载检查定时器负责恢复）
            continue;
        }
        // 监控根目录
        m_watcher->addPath(s.path);

        // 监控所有子目录（一层一层加进去，便于检测新增/删除模型目录）
        QDirIterator it(s.path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            m_watcher->addPath(it.filePath());
        }
    }
}

void ModelSourceManager::scheduleDebouncedRescan() {
    // 启动防抖定时器（如果已在运行，singleShot 会重置计时）
    m_debounceTimer->start();
}

void ModelSourceManager::onFileChanged(const QString& path) {
    Q_UNUSED(path);
    // 文件变化（模型文件增删改）→ 触发防抖扫描
    scheduleDebouncedRescan();
}

void ModelSourceManager::onDirectoryChanged(const QString& path) {
    Q_UNUSED(path);
    // 目录变化（新增/删除子目录或文件）→ 触发防抖扫描
    // 注意：QFileSystemWatcher 在大量变化时可能丢失部分信号，所以
    // 防抖结束后会重新建立 watcher 路径
    scheduleDebouncedRescan();
}

void ModelSourceManager::onDebounceTimeout() {
    // 防抖结束，执行实际扫描
    rescan();
    // 扫描完成后重新建立 watcher（因为新增的子目录需要被监控）
    if (m_watching) setupWatcher();
}

void ModelSourceManager::onUnmountedCheckTimeout() {
    // 周期检查未挂载路径是否已恢复
    QList<ModelSource> toCheck;
    {
        QMutexLocker locker(&m_mutex);
        for (const ModelSource& s : m_sources) {
            if (s.enabled && m_sourceStatuses.value(s.path) == ModelSourceStatus::UNMOUNTED) {
                toCheck.append(s);
            }
        }
    }

    bool anyRecovered = false;
    for (const ModelSource& s : toCheck) {
        const QFileInfo fi(s.path);
        if (fi.exists() && fi.isDir()) {
            Logger::info(QStringLiteral("[ModelSourceManager] 路径已恢复挂载: %1").arg(s.path));
            updateSourceStatus(s.path, ModelSourceStatus::MOUNTED);
            anyRecovered = true;
        }
    }

    if (anyRecovered) {
        // 有路径恢复，立即扫描并重建 watcher
        rescan();
        if (m_watching) setupWatcher();
    }
}

void ModelSourceManager::updateSourceStatus(const QString& path, const QString& status) {
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        const QString old = m_sourceStatuses.value(path);
        if (old != status) {
            m_sourceStatuses[path] = status;
            changed = true;
        }
    }
    if (changed) {
        emit sourceStatusChanged(path, status);
    }
}

} // namespace QDV
