// =====================================================================
// ShapeRegistry.cpp — 能力注册表只读加载器实现（S0）
//
// 加载优先级（改配置即生效，无需重编译）：
//   1) 环境变量 QDV_CAPABILITIES
//   2) 可执行文件目录向上回溯 6 层内的 capabilities.json
//   3) 当前工作目录的 capabilities.json
//   4) qrc:/capabilities/capabilities.json（随 exe 打包的兜底副本）
// =====================================================================
#include "ShapeRegistry.h"

#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ShapeRegistry::ShapeRegistry(QObject* parent)
    : QObject(parent)
{
}

void ShapeRegistry::load(const QString& overridePath)
{
    m_shapes.clear();
    m_exports.clear();
    m_tasks.clear();
    m_shapeByIdx.clear();
    m_loaded = false;
    m_source.clear();

    // 1) 外部显式指定 / 环境变量
    QStringList paths;
    if (!overridePath.isEmpty())
        paths << overridePath;
    if (qEnvironmentVariableIsSet("QDV_CAPABILITIES"))
        paths << qEnvironmentVariable("QDV_CAPABILITIES");

    // 2) 可执行文件目录向上回溯 6 层
    QDir updir(QCoreApplication::applicationDirPath());
    for (int i = 0; i <= 6; ++i) {
        paths << QDir(updir.absolutePath() + "/capabilities.json").absolutePath();
        if (!updir.cdUp()) break;
    }

    // 3) 当前工作目录
    paths << QDir(QDir::currentPath() + "/capabilities.json").absolutePath();

    // 去重并按序尝试加载
    QStringList seen;
    for (const QString& p : paths) {
        const QString abs = QDir(p).absolutePath();
        if (!seen.contains(abs)) {
            seen << abs;
            if (tryLoadFromFile(abs))
                return;
        }
    }

    // 4) qrc 兜底
    if (loadFromResource())
        return;

    qWarning("[ShapeRegistry] 未找到 capabilities.json，能力注册表保持空（分派逻辑回退内置默认）。");
}

bool ShapeRegistry::tryLoadFromFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = f.readAll();
    f.close();
    if (data.isEmpty())
        return false;
    parse(data);
    if (m_loaded) {
        m_source = path;
        qInfo("[ShapeRegistry] 已从文件加载能力注册表: %s", qPrintable(path));
    }
    return m_loaded;
}

bool ShapeRegistry::loadFromResource()
{
    QFile f(QStringLiteral(":/capabilities/capabilities.json"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = f.readAll();
    f.close();
    parse(data);
    if (m_loaded) {
        m_source = QStringLiteral("qrc:/capabilities/capabilities.json");
        qInfo("[ShapeRegistry] 已从 qrc 兜底加载能力注册表。");
    }
    return m_loaded;
}

void ShapeRegistry::parse(const QByteArray& json)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("[ShapeRegistry] capabilities.json 解析失败: %s", qPrintable(err.errorString()));
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonArray shapes = root.value(QLatin1String("shapes")).toArray();
    const QJsonArray exports = root.value(QLatin1String("exports")).toArray();
    const QJsonArray tasks = root.value(QLatin1String("tasks")).toArray();

    for (const QJsonValue& v : shapes) {
        if (!v.isObject()) continue;
        QJsonObject o = v.toObject();
        const QString key = o.value(QLatin1String("key")).toString();
        if (key.isEmpty()) continue;
        m_shapeByIdx.insert(key, o.toVariantMap());
    }
    for (const QJsonValue& v : shapes)
        if (v.isObject()) m_shapes.append(v.toObject().toVariantMap());
    for (const QJsonValue& v : exports)
        if (v.isObject()) m_exports.append(v.toObject().toVariantMap());
    for (const QJsonValue& v : tasks)
        if (v.isObject()) m_tasks.append(v.toObject().toVariantMap());

    m_loaded = !m_shapes.isEmpty();
    emit shapesChanged();
}

bool ShapeRegistry::hasShape(const QString& key) const
{
    return m_shapeByIdx.contains(key);
}

QString ShapeRegistry::overlayTypeOf(const QString& key) const
{
    auto it = m_shapeByIdx.constFind(key);
    if (it == m_shapeByIdx.constEnd())
        return QLatin1String("rect");          // 未登记回退 rect
    return it->value(QLatin1String("overlayType")).toString();
}

bool ShapeRegistry::isShapeEnabled(const QString& key) const
{
    auto it = m_shapeByIdx.constFind(key);
    if (it == m_shapeByIdx.constEnd())
        return false;
    return it->value(QLatin1String("enabled"), true).toBool();  // enabled 缺省视为 true
}

QVariantMap ShapeRegistry::shapeInfo(const QString& key) const
{
    auto it = m_shapeByIdx.constFind(key);
    if (it == m_shapeByIdx.constEnd())
        return {};
    return it.value();
}

bool ShapeRegistry::supportsShape(const QString& format, const QString& shape) const
{
    for (const QVariant& v : m_exports) {
        const QVariantMap m = v.toMap();
        if (m.value(QLatin1String("key")).toString() != format) continue;
        const QVariantList needs = m.value(QLatin1String("needsShape")).toList();
        if (needs.isEmpty()) return true;   // needsShape 为空 = 接受任意/不校验
        for (const QVariant& s : needs)
            if (s.toString() == shape) return true;
    }
    return false;
}

QStringList ShapeRegistry::exportNeedsShapes(const QString& format) const
{
    for (const QVariant& v : m_exports) {
        const QVariantMap m = v.toMap();
        if (m.value(QLatin1String("key")).toString() != format) continue;
        QStringList out;
        const QVariantList needs = m.value(QLatin1String("needsShape")).toList();
        for (const QVariant& s : needs)
            out << s.toString();
        return out;
    }
    return {};
}

QString ShapeRegistry::taskDefaultShape(const QString& task) const
{
    for (const QVariant& v : m_tasks) {
        const QVariantMap m = v.toMap();
        if (m.value(QLatin1String("key")).toString() == task)
            return m.value(QLatin1String("defaultShape")).toString();
    }
    return QString();
}

QStringList ShapeRegistry::taskModels(const QString& task) const
{
    for (const QVariant& v : m_tasks) {
        const QVariantMap m = v.toMap();
        if (m.value(QLatin1String("key")).toString() != task) continue;
        QStringList out;
        const QVariantList models = m.value(QLatin1String("models")).toList();
        for (const QVariant& s : models)
            out << s.toString();
        return out;
    }
    return {};
}