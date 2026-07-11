#include "UI/SchemeSerializer.h"
#include "Core/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QDir>
#include <QtConcurrent/QtConcurrent>
#include <QXmlStreamWriter>
#include <QXmlStreamReader>
#include <QByteArray>
#include <QBuffer>
#include <QDebug>

namespace QDV {
namespace UI {

// =====================================================
// v5.3.1：格式检测
// =====================================================
SchemeSerializer::SchemeFormat SchemeSerializer::detectFormat(const QString& filePath) {
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == "qdvz") return FormatCompressed;
    if (suffix == "xml")  return FormatXml;
    return FormatJson;  // 默认 json
}

// =====================================================================
// 构造 / 析构
// =====================================================================
SchemeSerializer::SchemeSerializer(QObject* parent)
    : QObject(parent) {
    // 保存任务：worker 线程返回错误消息（"OK" 表示成功）
    connect(&m_saveWatcher, &QFutureWatcher<QString>::finished,
            this, &SchemeSerializer::onSaveFinished);
    // 加载任务：worker 线程返回 JSON 文本（空字符串 + 错误信息走 signal 消息）
    connect(&m_loadWatcher, &QFutureWatcher<QString>::finished,
            this, &SchemeSerializer::onLoadFinished);
}

SchemeSerializer::~SchemeSerializer() {
    // 析构时若 watcher 仍在运行，等待其完成（避免 worker 访问已析构对象）
    if (m_saveWatcher.isRunning()) m_saveWatcher.waitForFinished();
    if (m_loadWatcher.isRunning()) m_loadWatcher.waitForFinished();
}

// =====================================================================
// 工作线程：把 QVariantList 序列化为 JSON 文本
// =====================================================================
QString SchemeSerializer::serializeToJson(const QVariantList& nodes,
                                          const QVariantList& connections,
                                          const QString& schemeName,
                                          const QString& variablesJson,
                                          QString* errMsg) {
    QJsonObject root;
    root["version"]    = QStringLiteral("2.1.0");
    root["schemeName"] = schemeName;
    root["savedAt"]    = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonArray nodesArr;
    for (const QVariant& v : nodes) {
        const QVariantMap m = v.toMap();
        QJsonObject node;
        node["id"]     = m.value("id").toString();
        node["type"]   = m.value("type").toString();
        node["x"]      = m.value("x").toDouble();
        node["y"]      = m.value("y").toDouble();

        // params（QVariantMap → QJsonObject）
        const QVariantMap params = m.value("params").toMap();
        QJsonObject paramsObj;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            paramsObj.insert(it.key(), QJsonValue::fromVariant(it.value()));
        }
        node["params"] = paramsObj;
        nodesArr.append(node);
    }
    root["nodes"] = nodesArr;

    QJsonArray connArr;
    for (const QVariant& v : connections) {
        const QVariantMap m = v.toMap();
        QJsonObject conn;
        conn["fromId"]   = m.value("fromId").toString();
        conn["fromPort"] = m.value("fromPort").toString();
        conn["toId"]     = m.value("toId").toString();
        conn["toPort"]   = m.value("toPort").toString();
        connArr.append(conn);
    }
    root["connections"] = connArr;

    // v2.6.0：写入控制变量 JSON（解析 variablesJson 字符串 → 写入 root["variables"]）
    // 解析失败时记录为空数组（不阻断保存）
    QJsonArray varsArr;
    if (!variablesJson.isEmpty()) {
        QJsonParseError pe;
        const QJsonDocument varsDoc = QJsonDocument::fromJson(variablesJson.toUtf8(), &pe);
        if (pe.error == QJsonParseError::NoError && varsDoc.isArray()) {
            varsArr = varsDoc.array();
        }
    }
    root["variables"] = varsArr;

    const QJsonDocument doc(root);
    const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
    if (errMsg) *errMsg = QStringLiteral("OK");
    return QString::fromUtf8(bytes);
}

// =====================================================================
// 异步保存（公共 API）—— v5.3.1：按扩展名自动选择格式
// =====================================================================
void SchemeSerializer::saveAsync(const QString& filePath,
                                 const QVariantList& nodes,
                                 const QVariantList& connections,
                                 const QString& schemeName) {
    if (isBusy()) {
        emit saveFinished(filePath, false,
                          QStringLiteral("序列化器忙：请等待上一次 I/O 完成"));
        return;
    }
    if (filePath.isEmpty()) {
        emit saveFinished(filePath, false, QStringLiteral("文件路径为空"));
        return;
    }
    // v5.3.1：检测格式
    m_saveFormat = detectFormat(filePath);
    Logger::info(QString("SchemeSerializer: 保存方案 format=%1 path=%2 nodes=%3")
                     .arg(m_saveFormat == FormatJson ? "JSON" :
                          m_saveFormat == FormatCompressed ? "QDVZ" : "XML")
                     .arg(filePath).arg(nodes.size()));

    // 缓存参数（worker 线程不能直接读 Qt 对象）
    m_saveFilePath    = filePath;
    m_saveSchemeName  = schemeName;
    m_saveNodes       = nodes;
    m_saveConnections = connections;

    const QString fp        = filePath;
    const QString name      = schemeName;
    const QVariantList nd   = nodes;
    const QVariantList cd   = connections;
    const QString varsJson  = m_saveVariablesJson;
    const SchemeFormat fmt  = m_saveFormat;

    QFuture<QString> fut = QtConcurrent::run([fp, name, nd, cd, varsJson, fmt]() -> QString {
        // 1. 序列化为文本（JSON 或 XML）
        QString serErr;
        QString text;
        if (fmt == FormatXml) {
            text = serializeToXml(nd, cd, name, varsJson, &serErr);
        } else {
            text = serializeToJson(nd, cd, name, varsJson, &serErr);
        }
        if (serErr != QStringLiteral("OK")) {
            return QStringLiteral("SER_ERR:") + serErr;
        }

        // 2. 转换为字节流（压缩格式做 gzip）
        QByteArray bytes = text.toUtf8();
        if (fmt == FormatCompressed) {
            bytes = qCompress(bytes, 9);  // 最高压缩级别
        }

        // 3. 写入（QSaveFile 原子写）
        QSaveFile sf(fp);
        if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return QStringLiteral("OPEN_ERR:") + sf.errorString();
        }
        if (sf.write(bytes) != bytes.size()) {
            sf.cancelWriting();
            return QStringLiteral("WRITE_ERR:") + sf.errorString();
        }
        if (!sf.commit()) {
            return QStringLiteral("COMMIT_ERR:") + sf.errorString();
        }
        return QStringLiteral("OK");
    });
    m_saveWatcher.setFuture(fut);
}

// v2.6.0：设置待保存的控制变量 JSON（保存前由 EditViewBridge 注入）
void SchemeSerializer::setVariablesJson(const QString& variablesJson) {
    m_saveVariablesJson = variablesJson;
}

void SchemeSerializer::onSaveFinished() {
    const QString result = m_saveWatcher.result();
    const QString filePath = m_saveFilePath;
    if (result == QStringLiteral("OK")) {
        emit saveFinished(filePath, true,
                          QStringLiteral("已保存 %1 个节点到 %2")
                              .arg(m_saveNodes.size())
                              .arg(QFileInfo(filePath).fileName()));
    } else {
        // 解析 "ERR_TYPE:msg"
        const int colonIdx = result.indexOf(':');
        const QString errType = colonIdx > 0 ? result.left(colonIdx) : QStringLiteral("UNKNOWN");
        const QString errMsg  = colonIdx > 0 ? result.mid(colonIdx + 1) : result;
        emit saveFinished(filePath, false,
                          QStringLiteral("保存失败 [%1] %2").arg(errType, errMsg));
    }
}

// =====================================================================
// 异步加载（公共 API）—— v5.3.1：按扩展名自动识别格式
// =====================================================================
void SchemeSerializer::loadAsync(const QString& filePath) {
    if (isBusy()) {
        emit loadFinished(filePath, false,
                          QStringLiteral("序列化器忙：请等待上一次 I/O 完成"), QString());
        return;
    }
    if (filePath.isEmpty()) {
        emit loadFinished(filePath, false, QStringLiteral("文件路径为空"), QString());
        return;
    }
    m_loadFilePath = filePath;
    m_loadFormat = detectFormat(filePath);
    Logger::info(QString("SchemeSerializer: 加载方案 format=%1 path=%2")
                     .arg(m_loadFormat == FormatJson ? "JSON" :
                          m_loadFormat == FormatCompressed ? "QDVZ" : "XML")
                     .arg(filePath));

    const QString fp = filePath;
    const SchemeFormat fmt = m_loadFormat;
    QFuture<QString> fut = QtConcurrent::run([fp, fmt]() -> QString {
        // 返回 "OK:<json>" 或 "ERR:<msg>"
        QFile f(fp);
        if (!f.open(QIODevice::ReadOnly)) {
            return QStringLiteral("ERR:打开文件失败：") + f.errorString();
        }
        QByteArray bytes = f.readAll();
        f.close();
        if (bytes.isEmpty()) {
            return QStringLiteral("ERR:文件为空");
        }

        // v5.3.1：按格式处理
        QString jsonText;
        if (fmt == FormatCompressed) {
            // 解压缩
            const QByteArray decompressed = qUncompress(bytes);
            if (decompressed.isEmpty()) {
                return QStringLiteral("ERR:解压缩失败，文件可能已损坏");
            }
            jsonText = QString::fromUtf8(decompressed);
        } else if (fmt == FormatXml) {
            // XML → JSON 转换
            QString xmlErr;
            jsonText = xmlToJson(QString::fromUtf8(bytes), &xmlErr);
            if (jsonText.isEmpty()) {
                return QStringLiteral("ERR:XML 解析失败：") + xmlErr;
            }
        } else {
            // JSON 直接使用
            jsonText = QString::fromUtf8(bytes);
        }

        // 校验 JSON 可解析
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &pe);
        if (pe.error != QJsonParseError::NoError) {
            return QStringLiteral("ERR:JSON 解析失败：") + pe.errorString();
        }
        if (!doc.isObject()) {
            return QStringLiteral("ERR:JSON 根元素不是对象");
        }
        return QStringLiteral("OK:") + jsonText;
    });
    m_loadWatcher.setFuture(fut);
}

void SchemeSerializer::onLoadFinished() {
    const QString result = m_loadWatcher.result();
    const QString filePath = m_loadFilePath;
    if (result.startsWith(QStringLiteral("OK:"))) {
        const QString jsonText = result.mid(3);
        // v2.6.0：解析 variables 字段，发出 variablesLoaded 信号
        // 失败/无字段时发出 "[]"，由 EditViewBridge 决定是否清空当前变量
        QString varsJson = QStringLiteral("[]");
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &pe);
        if (pe.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject root = doc.object();
            if (root.contains(QStringLiteral("variables")) && root.value("variables").isArray()) {
                const QJsonArray varsArr = root.value("variables").toArray();
                varsJson = QString::fromUtf8(
                    QJsonDocument(varsArr).toJson(QJsonDocument::Compact));
            }
        }
        emit variablesLoaded(varsJson);
        emit loadFinished(filePath, true, QStringLiteral("加载完成"), jsonText);
    } else if (result.startsWith(QStringLiteral("ERR:"))) {
        const QString errMsg = result.mid(4);
        emit loadFinished(filePath, false, errMsg, QString());
    } else {
        emit loadFinished(filePath, false, QStringLiteral("未知结果：") + result, QString());
    }
}

// =====================================================================
// v5.3.1：XML 序列化
// =====================================================================
QString SchemeSerializer::serializeToXml(const QVariantList& nodes,
                                          const QVariantList& connections,
                                          const QString& schemeName,
                                          const QString& variablesJson,
                                          QString* errMsg) {
    QByteArray ba;
    QXmlStreamWriter w(&ba);
    w.setAutoFormatting(true);
    w.writeStartDocument();

    w.writeStartElement("scheme");
    w.writeAttribute("version", "2.1.0");
    w.writeTextElement("schemeName", schemeName);
    w.writeTextElement("savedAt", QDateTime::currentDateTime().toString(Qt::ISODate));

    // nodes
    w.writeStartElement("nodes");
    for (const QVariant& v : nodes) {
        const QVariantMap m = v.toMap();
        w.writeStartElement("node");
        w.writeAttribute("id", m.value("id").toString());
        w.writeAttribute("type", m.value("type").toString());
        w.writeAttribute("x", QString::number(m.value("x").toDouble()));
        w.writeAttribute("y", QString::number(m.value("y").toDouble()));
        // params
        const QVariantMap params = m.value("params").toMap();
        if (!params.isEmpty()) {
            w.writeStartElement("params");
            for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
                w.writeStartElement("param");
                w.writeAttribute("name", it.key());
                w.writeCharacters(it.value().toString());
                w.writeEndElement(); // param
            }
            w.writeEndElement(); // params
        }
        w.writeEndElement(); // node
    }
    w.writeEndElement(); // nodes

    // connections
    w.writeStartElement("connections");
    for (const QVariant& v : connections) {
        const QVariantMap m = v.toMap();
        w.writeStartElement("connection");
        w.writeAttribute("fromId", m.value("fromId").toString());
        w.writeAttribute("fromPort", m.value("fromPort").toString());
        w.writeAttribute("toId", m.value("toId").toString());
        w.writeAttribute("toPort", m.value("toPort").toString());
        w.writeEndElement(); // connection
    }
    w.writeEndElement(); // connections

    // variables
    w.writeStartElement("variables");
    if (!variablesJson.isEmpty()) {
        QJsonParseError pe;
        const QJsonDocument varsDoc = QJsonDocument::fromJson(variablesJson.toUtf8(), &pe);
        if (pe.error == QJsonParseError::NoError && varsDoc.isArray()) {
            const QJsonArray varsArr = varsDoc.array();
            for (const QJsonValue& v : varsArr) {
                const QJsonObject vo = v.toObject();
                w.writeStartElement("variable");
                w.writeAttribute("name", vo.value("name").toString());
                w.writeAttribute("type", vo.value("type").toString());
                w.writeTextElement("value", vo.value("value").toString());
                w.writeTextElement("description", vo.value("description").toString());
                w.writeEndElement(); // variable
            }
        }
    }
    w.writeEndElement(); // variables

    w.writeEndElement(); // scheme
    w.writeEndDocument();

    if (errMsg) *errMsg = QStringLiteral("OK");
    return QString::fromUtf8(ba);
}

// =====================================================================
// v5.3.1：XML → JSON 转换（统一走 JSON 中间格式，复用 applyLoadedJson）
// =====================================================================
QString SchemeSerializer::xmlToJson(const QString& xmlText, QString* errMsg) {
    QXmlStreamReader r(xmlText);
    QJsonObject root;
    QJsonArray nodesArr;
    QJsonArray connArr;
    QJsonArray varsArr;

    // 简易状态机解析
    QString currentElement;
    QJsonObject currentNode;
    QJsonObject currentParam;
    QJsonObject currentVar;
    QJsonObject currentConn;
    QVariantMap currentParams;

    while (!r.atEnd()) {
        const QXmlStreamReader::TokenType tt = r.readNext();
        if (tt == QXmlStreamReader::StartElement) {
            currentElement = r.name().toString();
            const QXmlStreamAttributes attrs = r.attributes();

            if (currentElement == "scheme") {
                root["version"] = attrs.value("version").toString();
            } else if (currentElement == "node") {
                currentNode = QJsonObject();
                currentNode["id"]   = attrs.value("id").toString();
                currentNode["type"] = attrs.value("type").toString();
                currentNode["x"]    = attrs.value("x").toDouble();
                currentNode["y"]    = attrs.value("y").toDouble();
                currentParams.clear();
            } else if (currentElement == "param") {
                currentParam = QJsonObject();
                currentParam["name"] = attrs.value("name").toString();
            } else if (currentElement == "connection") {
                currentConn = QJsonObject();
                currentConn["fromId"]   = attrs.value("fromId").toString();
                currentConn["fromPort"] = attrs.value("fromPort").toString();
                currentConn["toId"]     = attrs.value("toId").toString();
                currentConn["toPort"]   = attrs.value("toPort").toString();
            } else if (currentElement == "variable") {
                currentVar = QJsonObject();
                currentVar["name"] = attrs.value("name").toString();
                currentVar["type"] = attrs.value("type").toString();
            }
        } else if (tt == QXmlStreamReader::Characters && !r.isWhitespace()) {
            const QString text = r.text().toString().trimmed();
            if (text.isEmpty()) continue;

            if (currentElement == "schemeName") {
                root["schemeName"] = text;
            } else if (currentElement == "savedAt") {
                root["savedAt"] = text;
            } else if (currentElement == "param" && !currentParam.isEmpty()) {
                currentParam["value"] = text;
                currentParams[currentParam["name"].toString()] = text;
            } else if (currentElement == "value" && !currentVar.isEmpty()) {
                currentVar["value"] = text;
            } else if (currentElement == "description" && !currentVar.isEmpty()) {
                currentVar["description"] = text;
            }
        } else if (tt == QXmlStreamReader::EndElement) {
            const QString name = r.name().toString();
            if (name == "node") {
                // params → QJsonObject
                QJsonObject paramsObj;
                for (auto it = currentParams.constBegin(); it != currentParams.constEnd(); ++it) {
                    paramsObj.insert(it.key(), QJsonValue::fromVariant(it.value()));
                }
                currentNode["params"] = paramsObj;
                nodesArr.append(currentNode);
                currentNode = QJsonObject();
            } else if (name == "connection") {
                connArr.append(currentConn);
                currentConn = QJsonObject();
            } else if (name == "variable") {
                varsArr.append(currentVar);
                currentVar = QJsonObject();
            }
        }
    }

    if (r.hasError()) {
        if (errMsg) *errMsg = r.errorString();
        return QString();
    }

    root["nodes"] = nodesArr;
    root["connections"] = connArr;
    root["variables"] = varsArr;

    if (errMsg) *errMsg = QStringLiteral("OK");
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

} // namespace UI
} // namespace QDV
