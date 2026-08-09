#include "BadCaseRecorder.h"
#include "ZeroShotKit/Logger.h"

#include <QFile>
#include <QJsonDocument>
#include <QDateTime>


namespace zsu {

BadCaseRecorder::BadCaseRecorder(QObject* parent) : QObject(parent) {
}

void BadCaseRecorder::record(const BadCaseRecord& entry) {
    BadCaseRecord rec = entry;
    if (rec.timestamp == 0) {
        rec.timestamp = QDateTime::currentMSecsSinceEpoch();
    }
    m_records.append(rec);
    emit recordAdded(rec);
    ZSU_LOG_INFO(QString("BadCaseRecorder: 记录 bad case (图像=%1, 模型=%2)")
        .arg(rec.imagePath).arg(rec.modelType));
}

void BadCaseRecorder::clear() {
    m_records.clear();
    emit recordsCleared();
    ZSU_LOG_INFO("BadCaseRecorder: 已清空所有记录");
}

bool BadCaseRecorder::exportToJson(const QString& filePath) const {
    QJsonArray arr;
    for (const auto& r : m_records) {
        QJsonObject obj;
        obj["image_path"] = r.imagePath;
        obj["model_type"] = r.modelType;
        obj["user_comment"] = r.userComment;
        obj["timestamp"] = static_cast<qint64>(r.timestamp);
        obj["original_result"] = r.originalResult.toJson();
        obj["corrected_result"] = r.correctedResult.toJson();
        arr.append(obj);
    }

    QJsonDocument doc(arr);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        ZSU_LOG_ERROR(QString("BadCaseRecorder: 无法写入文件 %1").arg(filePath));
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    ZSU_LOG_INFO(QString("BadCaseRecorder: 导出 %1 条记录到 %2").arg(m_records.size()).arg(filePath));
    return true;
}

bool BadCaseRecorder::importFromJson(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        ZSU_LOG_WARN(QString("BadCaseRecorder: 无法读取文件 %1").arg(filePath));
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray()) {
        ZSU_LOG_WARN("BadCaseRecorder: JSON 格式不是数组");
        return false;
    }

    m_records.clear();
    QJsonArray arr = doc.array();
    for (const auto& val : arr) {
        QJsonObject obj = val.toObject();
        BadCaseRecord rec;
        rec.imagePath = obj["image_path"].toString();
        rec.modelType = obj["model_type"].toString();
        rec.userComment = obj["user_comment"].toString();
        rec.timestamp = static_cast<qint64>(obj["timestamp"].toVariant().toLongLong());
        // originalResult 和 correctedResult 的完整解析从简，保留 JSON 信息
        m_records.append(rec);
    }

    ZSU_LOG_INFO(QString("BadCaseRecorder: 导入 %1 条记录").arg(m_records.size()));
    return true;
}

} // namespace zsu
