#include "Core/PromptLibrary.h"
#include "Core/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QCoreApplication>

namespace QDV {

// ========================================================================
// 静态成员初始化
// ========================================================================
PromptLibrary* PromptLibrary::s_instance = nullptr;
QMutex         PromptLibrary::s_instanceMutex;

// ========================================================================
// PromptEntry 序列化
// ========================================================================

QVariantMap PromptEntry::toMap() const {
    QVariantMap m;
    m["name"]           = name;
    m["prompt"]         = prompt;
    m["cnName"]         = cnName;
    m["scene"]          = scene;
    m["exampleImagePath"] = exampleImagePath;
    // v2.0 阶段五 Task 13：专用模型标记序列化
    m["hasSpecializedModel"]  = hasSpecializedModel;
    m["specializedModelPath"] = specializedModelPath;
    m["source"]         = sourceToString(source);
    return m;
}

PromptEntry PromptEntry::fromMap(const QVariantMap& m, PromptSource src) {
    PromptEntry e;
    e.name             = m.value("name").toString();
    e.prompt           = m.value("prompt").toString();
    e.cnName           = m.value("cnName").toString();
    e.scene            = m.value("scene").toString();
    e.exampleImagePath = m.value("exampleImagePath").toString();
    // v2.0 阶段五 Task 13：专用模型标记反序列化（向后兼容：旧文件无此字段视为 false）
    e.hasSpecializedModel  = m.value("hasSpecializedModel", false).toBool();
    e.specializedModelPath = m.value("specializedModelPath").toString();
    // source 优先用参数指定，其次读 map 中的 source 字段
    if (m.contains("source")) {
        e.source = sourceFromString(m.value("source").toString());
    } else {
        e.source = src;
    }
    return e;
}

QString PromptEntry::sourceToString(PromptSource s) {
    switch (s) {
    case PromptSource::Builtin: return QStringLiteral("builtin");
    case PromptSource::Custom:  return QStringLiteral("custom");
    case PromptSource::Recent:  return QStringLiteral("recent");
    }
    return QStringLiteral("builtin");
}

PromptSource PromptEntry::sourceFromString(const QString& s) {
    if (s == QStringLiteral("custom"))  return PromptSource::Custom;
    if (s == QStringLiteral("recent")) return PromptSource::Recent;
    return PromptSource::Builtin;  // 默认 builtin
}

// ========================================================================
// 单例
// ========================================================================

PromptLibrary* PromptLibrary::instance() {
    QMutexLocker locker(&s_instanceMutex);
    if (!s_instance) {
        s_instance = new PromptLibrary();
    }
    return s_instance;
}

PromptLibrary::PromptLibrary(QObject* parent)
    : QObject(parent)
{
    // 构造时立即加载默认配置（失败则用 builtinDefaults 兜底）
    QString err;
    loadFromJson(QString(), &err);
}

// ========================================================================
// 默认配置路径
// ========================================================================

QString PromptLibrary::defaultConfigPath() {
    // <exe-dir>/config/prompt_library.json
    const QString dir = QCoreApplication::applicationDirPath() + "/config";
    QDir().mkpath(dir);
    return dir + "/prompt_library.json";
}

// ========================================================================
// 内置 12 类默认缺陷
// ========================================================================

QList<PromptEntry> PromptLibrary::builtinDefaults() {
    static const QList<PromptEntry> defaults = []() {
        QList<PromptEntry> list;
        auto add = [&](const char* name, const char* prompt, const char* cnName, const char* scene) {
            PromptEntry e;
            e.name   = QString::fromUtf8(name);
            e.prompt = QString::fromUtf8(prompt);
            e.cnName = QString::fromUtf8(cnName);
            e.scene  = QString::fromUtf8(scene);
            e.source = PromptSource::Builtin;
            list.append(e);
        };
        add("scratch",       "scratch",       "划痕",       "表面划伤检测");
        add("dent",          "dent",          "凹坑",       "冲压/注塑件凹陷");
        add("stain",         "stain",         "污渍",       "表面污染检测");
        add("crack",         "crack",         "裂纹",       "结构性缺陷");
        add("deformation",   "deformation",   "变形",       "形状偏差检测");
        add("discoloration", "discoloration", "变色",       "涂层/电镀缺陷");
        add("missing_part",  "missing part",  "缺件",       "装配完整性");
        add("misalignment",  "misalignment",  "错位",       "装配位置偏差");
        add("burr",          "burr",          "毛刺",       "机加工边缘缺陷");
        add("hole",          "hole",          "孔洞",       "铸造/焊接缺陷");
        add("contamination", "contamination", "异物污染",   "异物混入检测");
        add("rust",          "rust",          "锈点/锈蚀",  "金属锈蚀");
        return list;
    }();
    return defaults;
}

// ========================================================================
// 加载 / 保存
// ========================================================================

bool PromptLibrary::loadFromJson(const QString& path, QString* outError) {
    auto fail = [outError](const QString& m) -> bool {
        if (outError) *outError = m;
        return false;
    };

    const QString realPath = path.isEmpty() ? defaultConfigPath() : path;

    QMutexLocker locker(&m_mutex);

    QFile f(realPath);
    if (!f.exists()) {
        // 文件不存在：用默认值兜底，不视为错误
        m_builtin = builtinDefaults();
        m_custom.clear();
        m_recent.clear();
        m_loadedPath = realPath;
        locker.unlock();
        emit libraryChanged();
        QDV::Logger::info(QString("[PromptLibrary] config not found, using builtin defaults: %1").arg(realPath));
        return true;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        m_builtin = builtinDefaults();
        m_custom.clear();
        m_recent.clear();
        m_loadedPath = realPath;
        locker.unlock();
        emit libraryChanged();
        return fail(QStringLiteral("open failed: %1").arg(f.errorString()));
    }
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError) {
        m_builtin = builtinDefaults();
        m_custom.clear();
        m_recent.clear();
        m_loadedPath = realPath;
        locker.unlock();
        emit libraryChanged();
        return fail(QStringLiteral("JSON parse error at offset %1: %2")
                        .arg(perr.offset).arg(perr.errorString()));
    }
    if (!doc.isObject()) {
        m_builtin = builtinDefaults();
        m_custom.clear();
        m_recent.clear();
        m_loadedPath = realPath;
        locker.unlock();
        emit libraryChanged();
        return fail(QStringLiteral("root is not JSON object"));
    }

    const QJsonObject root = doc.object();
    // builtin 节缺失时用默认值兜底（保证 12 类始终可用）
    if (root.contains("builtin") && root.value("builtin").isArray()) {
        m_builtin = jsonToEntries(root.value("builtin").toArray(), PromptSource::Builtin);
        // 兜底：若 builtin 为空或不足 12 类，补齐默认值
        if (m_builtin.size() < builtinDefaults().size()) {
            m_builtin = builtinDefaults();
        }
    } else {
        m_builtin = builtinDefaults();
    }
    m_custom  = jsonToEntries(root.value("custom").toArray(), PromptSource::Custom);
    m_recent  = jsonToEntries(root.value("recent").toArray(), PromptSource::Recent);
    m_loadedPath = realPath;

    locker.unlock();
    emit libraryChanged();

    QDV::Logger::info(QString("[PromptLibrary] loaded %1 builtin / %2 custom / %3 recent from %4")
                          .arg(m_builtin.size()).arg(m_custom.size()).arg(m_recent.size()).arg(realPath));
    return true;
}

bool PromptLibrary::saveToJson(const QString& path) {
    const QString realPath = path.isEmpty() ? defaultConfigPath() : path;

    QMutexLocker locker(&m_mutex);

    QJsonObject root;
    root["version"] = 1;
    root["comment"] = QStringLiteral(
        "Q-DetectVision 共享提示词库\n"
        "builtin: 12 类工业检测常见缺陷默认提示词（不可删除，可被覆盖）\n"
        "custom: 用户自定义类别（跨重启持久化）\n"
        "recent: 最近使用的提示词组合（最多 10 条，去重）");
    root["builtin"] = entriesToJson(m_builtin);
    root["custom"]  = entriesToJson(m_custom);
    root["recent"]  = entriesToJson(m_recent);

    QFileInfo fi(realPath);
    QDir().mkpath(fi.absolutePath());

    QFile f(realPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QDV::Logger::warn(QString("[PromptLibrary] save failed: %1").arg(f.errorString()));
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();

    m_loadedPath = realPath;
    QDV::Logger::info(QString("[PromptLibrary] saved %1 builtin / %2 custom / %3 recent to %4")
                          .arg(m_builtin.size()).arg(m_custom.size()).arg(m_recent.size()).arg(realPath));
    return true;
}

// ========================================================================
// 查询
// ========================================================================

QList<PromptEntry> PromptLibrary::builtin() const {
    QMutexLocker locker(&m_mutex);
    return m_builtin;
}

QList<PromptEntry> PromptLibrary::custom() const {
    QMutexLocker locker(&m_mutex);
    return m_custom;
}

QList<PromptEntry> PromptLibrary::recent() const {
    QMutexLocker locker(&m_mutex);
    return m_recent;
}

QVariantList PromptLibrary::allEntriesAsVariant() const {
    QMutexLocker locker(&m_mutex);
    QVariantList result;
    // 分组顺序：builtin → custom → recent（UI 可据此分组显示）
    for (const PromptEntry& e : m_builtin) {
        result.append(e.toMap());
    }
    for (const PromptEntry& e : m_custom) {
        result.append(e.toMap());
    }
    for (const PromptEntry& e : m_recent) {
        result.append(e.toMap());
    }
    return result;
}

PromptEntry PromptLibrary::findByPrompt(const QString& prompt) const {
    QMutexLocker locker(&m_mutex);
    // 依次在 builtin/custom/recent 中查找
    auto findInList = [&](const QList<PromptEntry>& list) -> PromptEntry {
        for (const PromptEntry& e : list) {
            if (e.prompt.compare(prompt, Qt::CaseInsensitive) == 0) {
                return e;
            }
        }
        return PromptEntry{};
    };
    PromptEntry found = findInList(m_builtin);
    if (found.name.isEmpty()) found = findInList(m_custom);
    if (found.name.isEmpty()) found = findInList(m_recent);
    return found;
}

// ========================================================================
// 修改
// ========================================================================

bool PromptLibrary::addCustom(const QString& name, const QString& prompt,
                              const QString& cnName, const QString& scene,
                              const QString& exampleImagePath) {
    // 参数校验
    if (name.trimmed().isEmpty() || prompt.trimmed().isEmpty()) {
        QDV::Logger::warn(QStringLiteral("[PromptLibrary] addCustom rejected: name/prompt empty"));
        return false;
    }
    // 不允许与 builtin 同名（避免覆盖内置类别）
    {
        QMutexLocker locker(&m_mutex);
        for (const PromptEntry& e : m_builtin) {
            if (e.name == name.trimmed()) {
                QDV::Logger::warn(QStringLiteral("[PromptLibrary] addCustom rejected: name conflicts with builtin '%1'").arg(name));
                return false;
            }
        }
    }

    {
        QMutexLocker locker(&m_mutex);
        // 同名 custom 覆盖
        for (int i = 0; i < m_custom.size(); ++i) {
            if (m_custom[i].name == name.trimmed()) {
                m_custom[i].prompt           = prompt.trimmed();
                m_custom[i].cnName           = cnName;
                m_custom[i].scene            = scene;
                m_custom[i].exampleImagePath = exampleImagePath;
                m_custom[i].source           = PromptSource::Custom;
                locker.unlock();
                // 持久化
                saveToJson(m_loadedPath);
                emit libraryChanged();
                QDV::Logger::info(QString("[PromptLibrary] custom entry updated: %1").arg(name));
                return true;
            }
        }
        // 新增
        PromptEntry e;
        e.name             = name.trimmed();
        e.prompt           = prompt.trimmed();
        e.cnName           = cnName;
        e.scene            = scene;
        e.exampleImagePath = exampleImagePath;
        e.source           = PromptSource::Custom;
        m_custom.append(e);
    }

    // 持久化
    saveToJson(m_loadedPath);
    emit libraryChanged();
    QDV::Logger::info(QString("[PromptLibrary] custom entry added: %1 -> '%2'").arg(name).arg(prompt));
    return true;
}

void PromptLibrary::addRecent(const QStringList& prompts) {
    // 过滤空字符串并去重（保留顺序）
    QStringList cleaned;
    for (const QString& p : prompts) {
        QString trimmed = p.trimmed();
        if (!trimmed.isEmpty() && !cleaned.contains(trimmed)) {
            cleaned.append(trimmed);
        }
    }
    if (cleaned.isEmpty()) return;

    // 拼接为组合串（用 " . " 分隔），作为一条 recent 记录
    const QString combined = cleaned.join(QStringLiteral(" . "));

    {
        QMutexLocker locker(&m_mutex);
        // 去重：若已存在相同组合，先移除旧的
        for (int i = m_recent.size() - 1; i >= 0; --i) {
            if (m_recent[i].prompt == combined) {
                m_recent.removeAt(i);
            }
        }
        // 新条目置顶
        PromptEntry e;
        e.name   = QStringLiteral("recent_") + QString::number(m_recent.size() + 1);
        e.prompt = combined;
        e.cnName = cleaned.join(QStringLiteral(", "));
        e.scene  = QStringLiteral("最近使用");
        e.source = PromptSource::Recent;
        m_recent.prepend(e);

        // 超限丢弃尾部
        while (m_recent.size() > RECENT_MAX) {
            m_recent.removeLast();
        }
    }

    // 持久化
    saveToJson(m_loadedPath);
    emit libraryChanged();
}

void PromptLibrary::clearRecent() {
    {
        QMutexLocker locker(&m_mutex);
        if (m_recent.isEmpty()) return;
        m_recent.clear();
    }
    saveToJson(m_loadedPath);
    emit libraryChanged();
    QDV::Logger::info("[PromptLibrary] recent cleared");
}

bool PromptLibrary::removeCustom(const QString& name) {
    {
        QMutexLocker locker(&m_mutex);
        for (int i = 0; i < m_custom.size(); ++i) {
            if (m_custom[i].name == name) {
                m_custom.removeAt(i);
                locker.unlock();
                saveToJson(m_loadedPath);
                emit libraryChanged();
                QDV::Logger::info(QString("[PromptLibrary] custom entry removed: %1").arg(name));
                return true;
            }
        }
    }
    return false;
}

// ========================================================================
// v2.0 阶段五 Task 13：专用模型标记实现
// ========================================================================

bool PromptLibrary::setSpecializedModel(const QString& prompt, const QString& modelPath) {
    if (prompt.trimmed().isEmpty()) return false;

    {
        QMutexLocker locker(&m_mutex);
        const QString target = prompt.trimmed();
        // 优先在 builtin 中查找（12 类默认缺陷最常被标记）
        for (PromptEntry& e : m_builtin) {
            if (e.prompt.compare(target, Qt::CaseInsensitive) == 0) {
                e.hasSpecializedModel  = true;
                e.specializedModelPath = modelPath;
                locker.unlock();
                saveToJson(m_loadedPath);
                emit libraryChanged();
                emit specializedModelChanged(target, true, modelPath);
                QDV::Logger::info(QString("[PromptLibrary] specialized model marked: %1 -> %2").arg(target).arg(modelPath));
                return true;
            }
        }
        // 然后在 custom 中查找
        for (PromptEntry& e : m_custom) {
            if (e.prompt.compare(target, Qt::CaseInsensitive) == 0) {
                e.hasSpecializedModel  = true;
                e.specializedModelPath = modelPath;
                locker.unlock();
                saveToJson(m_loadedPath);
                emit libraryChanged();
                emit specializedModelChanged(target, true, modelPath);
                QDV::Logger::info(QString("[PromptLibrary] specialized model marked: %1 -> %2").arg(target).arg(modelPath));
                return true;
            }
        }
    }
    QDV::Logger::warn(QString("[PromptLibrary] setSpecializedModel: prompt not found: %1").arg(prompt));
    return false;
}

bool PromptLibrary::clearSpecializedModel(const QString& prompt) {
    if (prompt.trimmed().isEmpty()) return false;

    {
        QMutexLocker locker(&m_mutex);
        const QString target = prompt.trimmed();
        for (PromptEntry& e : m_builtin) {
            if (e.prompt.compare(target, Qt::CaseInsensitive) == 0 && e.hasSpecializedModel) {
                e.hasSpecializedModel  = false;
                e.specializedModelPath.clear();
                locker.unlock();
                saveToJson(m_loadedPath);
                emit libraryChanged();
                emit specializedModelChanged(target, false, QString());
                return true;
            }
        }
        for (PromptEntry& e : m_custom) {
            if (e.prompt.compare(target, Qt::CaseInsensitive) == 0 && e.hasSpecializedModel) {
                e.hasSpecializedModel  = false;
                e.specializedModelPath.clear();
                locker.unlock();
                saveToJson(m_loadedPath);
                emit libraryChanged();
                emit specializedModelChanged(target, false, QString());
                return true;
            }
        }
    }
    return false;
}

QPair<bool, QString> PromptLibrary::querySpecializedModel(const QString& prompt) const {
    QMutexLocker locker(&m_mutex);
    const QString target = prompt.trimmed();
    for (const PromptEntry& e : m_builtin) {
        if (e.prompt.compare(target, Qt::CaseInsensitive) == 0) {
            return qMakePair(e.hasSpecializedModel, e.specializedModelPath);
        }
    }
    for (const PromptEntry& e : m_custom) {
        if (e.prompt.compare(target, Qt::CaseInsensitive) == 0) {
            return qMakePair(e.hasSpecializedModel, e.specializedModelPath);
        }
    }
    return qMakePair(false, QString());
}

// ========================================================================
// JSON 序列化辅助
// ========================================================================

QJsonArray PromptLibrary::entriesToJson(const QList<PromptEntry>& entries) {
    QJsonArray arr;
    for (const PromptEntry& e : entries) {
        QJsonObject obj;
        obj["name"]           = e.name;
        obj["prompt"]         = e.prompt;
        obj["cnName"]         = e.cnName;
        obj["scene"]          = e.scene;
        if (!e.exampleImagePath.isEmpty()) {
            obj["exampleImagePath"] = e.exampleImagePath;
        }
        // v2.0 阶段五 Task 13：专用模型标记持久化（仅在已标记时写入，减少 JSON 体积）
        if (e.hasSpecializedModel) {
            obj["hasSpecializedModel"]  = e.hasSpecializedModel;
            if (!e.specializedModelPath.isEmpty()) {
                obj["specializedModelPath"] = e.specializedModelPath;
            }
        }
        arr.append(obj);
    }
    return arr;
}

QList<PromptEntry> PromptLibrary::jsonToEntries(const QJsonArray& arr, PromptSource source) {
    QList<PromptEntry> list;
    for (const QJsonValue& v : arr) {
        if (!v.isObject()) continue;
        QJsonObject obj = v.toObject();
        PromptEntry e;
        e.name             = obj.value("name").toString();
        e.prompt           = obj.value("prompt").toString();
        e.cnName           = obj.value("cnName").toString();
        e.scene            = obj.value("scene").toString();
        e.exampleImagePath = obj.value("exampleImagePath").toString();
        // v2.0 阶段五 Task 13：专用模型标记反序列化（向后兼容）
        e.hasSpecializedModel  = obj.value("hasSpecializedModel").toBool(false);
        e.specializedModelPath = obj.value("specializedModelPath").toString();
        e.source           = source;
        if (!e.name.isEmpty() && !e.prompt.isEmpty()) {
            list.append(e);
        }
    }
    return list;
}

} // namespace QDV
