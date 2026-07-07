#include "OperatorSDK/OperatorManifest.h"
#include "OperatorSDK/IOperatorRegistry.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>

namespace QDV {

// ============================================================
// OperatorManifest 序列化/反序列化
// ============================================================

QJsonObject OperatorManifest::toJson() const {
    QJsonObject obj;
    obj["type"]        = type;
    obj["version"]     = version;
    obj["cnName"]      = cnName;
    obj["category"]    = category;
    obj["iconPath"]    = iconPath;
    obj["description"] = description;
    obj["library"]     = library;
    QJsonArray paramsArr;
    for (const QDV::UI::ParamSpec& ps : params) {
        paramsArr.append(QJsonObject::fromVariantMap(ps.toMap()));
    }
    obj["params"] = paramsArr;
    return obj;
}

OperatorManifest OperatorManifest::fromJson(const QJsonObject& obj, QString* outError) {
    OperatorManifest m;
    auto require = [&](const QString& key) -> bool {
        if (!obj.contains(key) || obj[key].toString().isEmpty()) {
            if (outError) *outError = QStringLiteral("missing required field: %1").arg(key);
            return false;
        }
        return true;
    };

    // 必填字段：type/version/cnName/category/library（RT-004 校验）
    if (!require("type"))     return m;
    if (!require("version"))  return m;
    if (!require("cnName"))   return m;
    if (!require("category")) return m;
    if (!require("library"))  return m;

    m.type        = obj["type"].toString();
    m.version     = obj["version"].toString();
    m.cnName      = obj["cnName"].toString();
    m.category    = obj["category"].toString();
    m.iconPath    = obj["iconPath"].toString();
    m.description = obj["description"].toString();
    m.library     = obj["library"].toString();

    // 可选字段：params（参数列表）
    if (obj.contains("params") && obj["params"].isArray()) {
        const QJsonArray arr = obj["params"].toArray();
        for (const QJsonValue& v : arr) {
            if (!v.isObject()) continue;
            QVariantMap pm = v.toObject().toVariantMap();
            m.params.append(QDV::UI::ParamSpec::fromMap(pm));
        }
    }

    return m;
}

// ============================================================
// ManifestLoader 自由函数
// ============================================================

namespace {

/// 内部辅助：把 OperatorManifest 字段校验逻辑独立出来，便于测试
bool validateFields(const OperatorManifest& m, QString* outError) {
    const QString missing = QStringLiteral("missing required field: %1");
    if (m.type.isEmpty())     { if (outError) *outError = missing.arg("type");     return false; }
    if (m.version.isEmpty())  { if (outError) *outError = missing.arg("version");  return false; }
    if (m.cnName.isEmpty())   { if (outError) *outError = missing.arg("cnName");   return false; }
    if (m.category.isEmpty()) { if (outError) *outError = missing.arg("category"); return false; }
    if (m.library.isEmpty())  { if (outError) *outError = missing.arg("library");  return false; }
    return true;
}

} // namespace

// ------------------------------------------------------------
// ManifestLoader 自由函数实现（声明见 OperatorManifest.h）
// ------------------------------------------------------------

/// 从 path 读取 JSON 并解析为 OperatorManifest（RT-004）
bool loadManifest(const QString& path, OperatorManifest& outManifest, QString* outError) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (outError) *outError = QStringLiteral("cannot open manifest file: %1").arg(path);
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (!doc.isObject()) {
        // JSON 解析失败 → outError 写入 "JSON parse error at offset X: Y"
        if (outError) {
            *outError = QStringLiteral("JSON parse error at offset %1: %2")
                            .arg(parseErr.offset).arg(parseErr.errorString());
        }
        return false;
    }

    OperatorManifest m = OperatorManifest::fromJson(doc.object(), outError);
    if (outError && !outError->isEmpty()) {
        // fromJson 已写入缺失字段名
        return false;
    }
    if (!validateManifest(m, outError)) {
        return false;
    }
    outManifest = m;
    return true;
}

bool validateManifest(const OperatorManifest& manifest, QString* outError) {
    return validateFields(manifest, outError);
}

} // namespace QDV
