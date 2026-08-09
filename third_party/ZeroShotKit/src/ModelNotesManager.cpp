#include "ModelNotesManager.h"
#include "ZeroShotKit/Logger.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>


namespace zsu {

ModelNotesManager::ModelNotesManager(QObject* parent) : QObject(parent) {
}

QString ModelNotesManager::modelTypeKey(ZeroShotModelType type) {
    switch (type) {
    case ZeroShotModelType::AnomalyCLIP:   return "anomaly_clip";
    case ZeroShotModelType::GroundingDINO: return "grounding_dino";
    case ZeroShotModelType::MobileSAM:     return "mobile_sam";
    case ZeroShotModelType::OpenCLIP:      return "open_clip";
    case ZeroShotModelType::PatchCore:     return "patch_core";
    default: return "unknown";
    }
}

ZeroShotModelType ModelNotesManager::keyToModelType(const QString& key) {
    if (key == "anomaly_clip")   return ZeroShotModelType::AnomalyCLIP;
    if (key == "grounding_dino") return ZeroShotModelType::GroundingDINO;
    if (key == "mobile_sam")     return ZeroShotModelType::MobileSAM;
    if (key == "open_clip")      return ZeroShotModelType::OpenCLIP;
    if (key == "patch_core")     return ZeroShotModelType::PatchCore;
    return ZeroShotModelType::Unknown;
}

ModelNote ModelNotesManager::parseNote(const QString& key, const QJsonObject& obj) const {
    ModelNote note;
    note.modelType = key;
    note.displayName = obj["display_name"].toString(key);
    note.description = obj["description"].toString();

    QJsonArray notesArr = obj["notes"].toArray();
    for (const auto& v : notesArr) note.notes << v.toString();

    QJsonArray inputArr = obj["input_format"].toArray();
    for (const auto& v : inputArr) note.inputFormat << v.toString();

    QJsonArray limitArr = obj["limitations"].toArray();
    for (const auto& v : limitArr) note.limitations << v.toString();

    QJsonArray tipsArr = obj["tips"].toArray();
    for (const auto& v : tipsArr) note.tips << v.toString();

    return note;
}

QJsonObject ModelNotesManager::noteToJson(const ModelNote& note) const {
    QJsonObject obj;
    obj["display_name"] = note.displayName;
    obj["description"] = note.description;

    QJsonArray notesArr;
    for (const auto& s : note.notes) notesArr.append(s);
    obj["notes"] = notesArr;

    QJsonArray inputArr;
    for (const auto& s : note.inputFormat) inputArr.append(s);
    obj["input_format"] = inputArr;

    QJsonArray limitArr;
    for (const auto& s : note.limitations) limitArr.append(s);
    obj["limitations"] = limitArr;

    QJsonArray tipsArr;
    for (const auto& s : note.tips) tipsArr.append(s);
    obj["tips"] = tipsArr;

    return obj;
}

bool ModelNotesManager::loadDefaultNotes(const QString& defaultPath) {
    QFile file(defaultPath);
    if (!file.open(QIODevice::ReadOnly)) {
        ZSU_LOG_WARN(QString("ModelNotesManager: 默认注意事项文件不存在: %1").arg(defaultPath));
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        ZSU_LOG_WARN("ModelNotesManager: 默认注意事项 JSON 格式无效");
        return false;
    }

    QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        QString key = it.key();
        ModelNote note = parseNote(key, it.value().toObject());
        m_notes[key] = note;
    }

    ZSU_LOG_INFO(QString("ModelNotesManager: 加载 %1 条默认注意事项").arg(m_notes.size()));
    return true;
}

bool ModelNotesManager::loadUserOverride(const QString& userPath) {
    QFile file(userPath);
    if (!file.open(QIODevice::ReadOnly)) {
        // 用户覆盖文件不存在是正常情况，不算错误
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        QString key = it.key();
        ModelNote note = parseNote(key, it.value().toObject());
        m_notes[key] = note;  // 覆盖默认值
    }

    ZSU_LOG_INFO(QString("ModelNotesManager: 加载用户覆盖注意事项: %1 条").arg(root.size()));
    return true;
}

bool ModelNotesManager::saveUserOverride(const QString& userPath) const {
    QJsonObject root;
    for (auto it = m_notes.begin(); it != m_notes.end(); ++it) {
        root[it.key()] = noteToJson(it.value());
    }

    QJsonDocument doc(root);
    QFile file(userPath);
    if (!file.open(QIODevice::WriteOnly)) {
        ZSU_LOG_ERROR(QString("ModelNotesManager: 无法写入用户覆盖文件: %1").arg(userPath));
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    ZSU_LOG_INFO(QString("ModelNotesManager: 保存用户覆盖注意事项到 %1").arg(userPath));
    return true;
}

ModelNote ModelNotesManager::getNote(ZeroShotModelType type) const {
    QString key = modelTypeKey(type);
    if (m_notes.contains(key)) {
        return m_notes[key];
    }
    // 返回空注意事项
    ModelNote empty;
    empty.modelType = key;
    empty.displayName = key;
    return empty;
}

void ModelNotesManager::setNote(ZeroShotModelType type, const ModelNote& note) {
    QString key = modelTypeKey(type);
    m_notes[key] = note;
}

QList<ModelNote> ModelNotesManager::allNotes() const {
    return m_notes.values();
}

} // namespace zsu
