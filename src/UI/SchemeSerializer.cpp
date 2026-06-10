#include "UI/SchemeSerializer.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QDir>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>

namespace QDV {
namespace UI {

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

    const QJsonDocument doc(root);
    const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
    if (errMsg) *errMsg = QStringLiteral("OK");
    return QString::fromUtf8(bytes);
}

// =====================================================================
// 异步保存（公共 API）
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
    // 缓存参数（worker 线程不能直接读 Qt 对象）
    m_saveFilePath    = filePath;
    m_saveSchemeName  = schemeName;
    m_saveNodes       = nodes;
    m_saveConnections = connections;

    // 启动异步任务：把 QVariantList → JSON 文本
    // 拷贝一份到 lambda 中（避免并发问题）
    const QString fp        = filePath;
    const QString name      = schemeName;
    const QVariantList nd   = nodes;
    const QVariantList cd   = connections;

    QFuture<QString> fut = QtConcurrent::run([fp, name, nd, cd]() -> QString {
        // 1. 序列化
        QString serErr;
        const QString json = serializeToJson(nd, cd, name, &serErr);
        if (serErr != QStringLiteral("OK")) {
            return QStringLiteral("SER_ERR:") + serErr;
        }
        // 2. 写入（QSaveFile 原子写，避免半截文件）
        QSaveFile sf(fp);
        if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return QStringLiteral("OPEN_ERR:") + sf.errorString();
        }
        const QByteArray bytes = json.toUtf8();
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
// 异步加载（公共 API）
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

    const QString fp = filePath;
    QFuture<QString> fut = QtConcurrent::run([fp]() -> QString {
        // 返回 "OK:<json>" 或 "ERR:<msg>"
        QFile f(fp);
        if (!f.open(QIODevice::ReadOnly)) {
            return QStringLiteral("ERR:打开文件失败：") + f.errorString();
        }
        const QByteArray bytes = f.readAll();
        f.close();
        if (bytes.isEmpty()) {
            return QStringLiteral("ERR:文件为空");
        }
        // 简单校验：JSON 必须可解析
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
        if (pe.error != QJsonParseError::NoError) {
            return QStringLiteral("ERR:JSON 解析失败：") + pe.errorString();
        }
        if (!doc.isObject()) {
            return QStringLiteral("ERR:JSON 根元素不是对象");
        }
        return QStringLiteral("OK:") + QString::fromUtf8(bytes);
    });
    m_loadWatcher.setFuture(fut);
}

void SchemeSerializer::onLoadFinished() {
    const QString result = m_loadWatcher.result();
    const QString filePath = m_loadFilePath;
    if (result.startsWith(QStringLiteral("OK:"))) {
        const QString jsonText = result.mid(3);
        emit loadFinished(filePath, true, QStringLiteral("加载完成"), jsonText);
    } else if (result.startsWith(QStringLiteral("ERR:"))) {
        const QString errMsg = result.mid(4);
        emit loadFinished(filePath, false, errMsg, QString());
    } else {
        emit loadFinished(filePath, false, QStringLiteral("未知结果：") + result, QString());
    }
}

} // namespace UI
} // namespace QDV
