#include "OperatorLibrary/OperatorRegistryStore.h"

// =====================================================================
// OperatorRegistryStore 实现
//
// 实现契约（spec §3.3 + tasks.md Task 1.1）：
//   - 一算子一文件持久化
//   - 原子写入（tmp → rename）
//   - 备份策略（.bak 仅保留最近一份）
//   - 目录降级（QStandardPaths::AppDataLocation）
//
// 模块自包含原则：不依赖 QDV::Core/Logger，使用 qDebug 输出诊断日志
// =====================================================================

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>

namespace QDV {
namespace OperatorLibrary {

OperatorRegistryStore* OperatorRegistryStore::s_instance = nullptr;

OperatorRegistryStore* OperatorRegistryStore::instance() {
    if (!s_instance) {
        s_instance = new OperatorRegistryStore();
    }
    return s_instance;
}

OperatorRegistryStore::OperatorRegistryStore(QObject* parent)
    : QObject(parent) {}

// ---------------------------------------------------------------------
// 初始化持久化目录
// ---------------------------------------------------------------------
bool OperatorRegistryStore::init(const QString& dirPath, QString* err) {
    if (!ensureDirectory(dirPath, err)) {
        return false;
    }
    // 目录就绪后立即扫描加载已有算子
    scanAll(err);  // scanAll 内部对单文件损坏静默处理，err 仅传递目录级错误
    qDebug().noquote() << "[OperatorRegistryStore] init at" << m_dirPath
                       << "loaded" << m_loaded.size() << "operator(s)";
    return true;
}

// ---------------------------------------------------------------------
// 扫描目录下所有 .qdvop 文件
// ---------------------------------------------------------------------
int OperatorRegistryStore::scanAll(QString* err) {
    m_loaded.clear();

    if (m_dirPath.isEmpty()) {
        if (err) *err = QStringLiteral("目录未初始化，请先调用 init()");
        return 0;
    }

    QDir dir(m_dirPath);
    if (!dir.exists()) {
        // 目录不存在视为空（不视为错误），init 已负责创建
        return 0;
    }

    // 仅加载 *.qdvop，按文件名字典序
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.qdvop"),
                                            QDir::Files | QDir::NoSymLinks,
                                            QDir::Name);
    int loaded = 0;
    for (const QString& fileName : files) {
        // 防御性：跳过 .tmp/.bak（理论上 entryList 已过滤，但保险）
        if (fileName.endsWith(QStringLiteral(".tmp"), Qt::CaseInsensitive) ||
            fileName.endsWith(QStringLiteral(".bak"), Qt::CaseInsensitive)) {
            continue;
        }

        const QString fullPath = dir.absoluteFilePath(fileName);
        QFile f(fullPath);
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning().noquote() << "[OperatorRegistryStore] 跳过无法读取的文件:" << fullPath
                                 << "原因:" << f.errorString();
            continue;
        }

        const QByteArray data = f.readAll();
        f.close();

        // JSON 解析
        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning().noquote() << "[OperatorRegistryStore] 跳过损坏的 JSON 文件:" << fullPath
                                 << "原因:" << parseErr.errorString();
            continue;  // 不阻断启动
        }

        // OperatorDef 反序列化
        QString opErr;
        OperatorDef def = OperatorDef::fromJson(doc.object(), &opErr);
        if (def.type.isEmpty()) {
            qWarning().noquote() << "[OperatorRegistryStore] 跳过无效算子定义:" << fullPath
                                 << "原因:" << (opErr.isEmpty() ? QStringLiteral("type 缺失") : opErr);
            continue;
        }

        // 文件名与 type 不一致仅警告，仍以 type 为 key（更宽容）
        const QString expectedBase = def.type + QStringLiteral(".qdvop");
        if (fileName != expectedBase) {
            qWarning().noquote() << "[OperatorRegistryStore] 文件名与 type 不一致:" << fileName
                                 << "vs type=" << def.type << "(以 type 为缓存 key)";
        }

        m_loaded.insert(def.type, def);
        ++loaded;
    }

    if (err) err->clear();
    return loaded;
}

// ---------------------------------------------------------------------
// 保存单个算子（原子写入 + 备份）
// ---------------------------------------------------------------------
bool OperatorRegistryStore::save(const OperatorDef& def, QString* err) {
    if (def.type.isEmpty()) {
        if (err) *err = QStringLiteral("算子 type 为空，无法保存");
        return false;
    }
    if (m_dirPath.isEmpty()) {
        if (err) *err = QStringLiteral("目录未初始化");
        return false;
    }

    const QString targetPath = filePathFor(def.type);

    // 序列化为 JSON（Indented 2 空格，UTF-8 不带 BOM）
    const QJsonDocument doc(def.toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    // 备份已存在的文件（仅保留最近一份 .bak）
    if (!backupIfExists(targetPath)) {
        // 备份失败不致命（可能是文件本就不存在），继续
    }

    // 原子写入
    if (!atomicWriteFile(targetPath, data, err)) {
        return false;
    }

    // 更新内存缓存
    m_loaded.insert(def.type, def);

    qDebug().noquote() << "[OperatorRegistryStore] saved" << def.type
                       << "to" << targetPath;
    emit operatorSaved(def.type);
    emit changed();
    return true;
}

// ---------------------------------------------------------------------
// 删除单个算子文件
// ---------------------------------------------------------------------
bool OperatorRegistryStore::remove(const QString& type, QString* err) {
    if (type.isEmpty()) {
        if (err) *err = QStringLiteral("type 为空");
        return false;
    }
    if (m_dirPath.isEmpty()) {
        if (err) *err = QStringLiteral("目录未初始化");
        return false;
    }

    const QString targetPath = filePathFor(type);
    const QString bakPath = targetPath + QStringLiteral(".bak");

    // 文件不存在视为成功（幂等）
    if (!QFile::exists(targetPath)) {
        m_loaded.remove(type);  // 同步清理内存
        // 顺手清理 .bak
        if (QFile::exists(bakPath)) {
            QFile::remove(bakPath);
        }
        return true;
    }

    if (!QFile::remove(targetPath)) {
        if (err) *err = QStringLiteral("文件删除失败: %1").arg(targetPath);
        return false;
    }

    // 清理 .bak
    if (QFile::exists(bakPath)) {
        QFile::remove(bakPath);  // 失败不致命
    }

    m_loaded.remove(type);

    qDebug().noquote() << "[OperatorRegistryStore] removed" << type;
    emit operatorRemoved(type);
    emit changed();
    return true;
}

// ---------------------------------------------------------------------
// 查询方法
// ---------------------------------------------------------------------
bool OperatorRegistryStore::contains(const QString& type) const {
    return m_loaded.contains(type);
}

OperatorDef OperatorRegistryStore::get(const QString& type, bool* ok) const {
    if (m_loaded.contains(type)) {
        if (ok) *ok = true;
        return m_loaded.value(type);
    }
    if (ok) *ok = false;
    return OperatorDef();  // 空 OperatorDef
}

QList<OperatorDef> OperatorRegistryStore::all() const {
    // m_loaded 是 QMap，已按 key 字典序排列
    return m_loaded.values();
}

QStringList OperatorRegistryStore::types() const {
    return m_loaded.keys();
}

QList<OperatorDef> OperatorRegistryStore::byCategory(const QString& category) const {
    QList<OperatorDef> result;
    for (const OperatorDef& def : m_loaded) {
        if (def.category == category) {
            result.append(def);
        }
    }
    return result;
}

QStringList OperatorRegistryStore::categories() const {
    QStringList cats;
    for (const OperatorDef& def : m_loaded) {
        if (!def.category.isEmpty() && !cats.contains(def.category)) {
            cats.append(def.category);
        }
    }
    cats.sort();
    return cats;
}

QList<OperatorDef> OperatorRegistryStore::query(const QString& category, const QString& keyword) const {
    QList<OperatorDef> result;
    const bool hasCategory = !category.isEmpty();
    const bool hasKeyword = !keyword.isEmpty();
    const QString kw = keyword.trimmed().toLower();

    for (const OperatorDef& def : m_loaded) {
        // 分类过滤
        if (hasCategory && def.category != category) {
            continue;
        }
        // 关键字过滤（type/cnName/tags 任意命中）
        if (hasKeyword) {
            bool matched = def.type.toLower().contains(kw) ||
                           def.cnName.toLower().contains(kw);
            if (!matched) {
                for (const QString& tag : def.tags) {
                    if (tag.toLower().contains(kw)) {
                        matched = true;
                        break;
                    }
                }
            }
            if (!matched) continue;
        }
        result.append(def);
    }
    return result;
}

QString OperatorRegistryStore::filePathFor(const QString& type) const {
    if (m_dirPath.isEmpty() || type.isEmpty()) {
        return QString();
    }
    // type 已通过 Validator 校验仅含字母数字下划线，无需额外 sanitize
    return m_dirPath + QStringLiteral("/") + type + QStringLiteral(".qdvop");
}

// ---------------------------------------------------------------------
// 私有：目录创建（含降级）
// ---------------------------------------------------------------------
bool OperatorRegistryStore::ensureDirectory(const QString& preferredPath, QString* err) {
    // 优先尝试 preferredPath
    QDir dir(preferredPath);
    if (dir.exists() || dir.mkpath(QStringLiteral("."))) {
        m_dirPath = QDir(preferredPath).absolutePath();
        return true;
    }

    // 降级：QStandardPaths::AppDataLocation/operators_imported
    const QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                             + QStringLiteral("/operators_imported");
    QDir fallbackDir(fallback);
    if (fallbackDir.exists() || fallbackDir.mkpath(QStringLiteral("."))) {
        m_dirPath = fallbackDir.absolutePath();
        qWarning().noquote() << "[OperatorRegistryStore] 主目录创建失败，降级到:" << m_dirPath;
        return true;
    }

    if (err) {
        *err = QStringLiteral("目录创建失败。主路径: %1；降级路径: %2")
                   .arg(preferredPath, fallback);
    }
    return false;
}

// ---------------------------------------------------------------------
// 私有：原子写入（tmp → rename）
// ---------------------------------------------------------------------
bool OperatorRegistryStore::atomicWriteFile(const QString& targetPath, const QByteArray& data, QString* err) {
    const QString tmpPath = targetPath + QStringLiteral(".tmp");

    // 写 tmp 文件
    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QStringLiteral("无法创建临时文件: %1 (%2)").arg(tmpPath, tmp.errorString());
        return false;
    }
    const qint64 written = tmp.write(data);
    if (written != data.size()) {
        if (err) *err = QStringLiteral("临时文件写入不完整: 期望 %1 字节，实际 %2 字节")
                            .arg(data.size()).arg(written);
        tmp.close();
        QFile::remove(tmpPath);
        return false;
    }
    tmp.flush();
    tmp.close();

    // rename tmp → target（Windows 上若 target 已存在需先删除）
    if (QFile::exists(targetPath)) {
        if (!QFile::remove(targetPath)) {
            if (err) *err = QStringLiteral("无法覆盖目标文件: %1").arg(targetPath);
            QFile::remove(tmpPath);
            return false;
        }
    }
    if (!QFile::rename(tmpPath, targetPath)) {
        if (err) *err = QStringLiteral("临时文件重命名失败: %1 -> %2").arg(tmpPath, targetPath);
        // tmp 残留由下次扫描跳过（.tmp 后缀）
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------
// 私有：备份已存在的文件
// ---------------------------------------------------------------------
bool OperatorRegistryStore::backupIfExists(const QString& targetPath) {
    if (!QFile::exists(targetPath)) {
        return true;  // 无需备份
    }
    const QString bakPath = targetPath + QStringLiteral(".bak");
    // 覆盖旧 .bak
    if (QFile::exists(bakPath)) {
        QFile::remove(bakPath);
    }
    if (!QFile::copy(targetPath, bakPath)) {
        qWarning().noquote() << "[OperatorRegistryStore] 备份失败:" << targetPath << "->" << bakPath;
        return false;
    }
    return true;
}

} // namespace OperatorLibrary
} // namespace QDV
