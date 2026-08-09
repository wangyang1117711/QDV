#include "TrainingInference/TrainingProject.h"
#include <QJsonDocument>

TrainingProject::TrainingProject(QObject* parent)
    : QObject(parent)
{
    m_createdAt = QDateTime::currentDateTime();
    m_modifiedAt = m_createdAt;
}

// ===== 项目元数据 =====
QString TrainingProject::name() const { return m_name; }
void TrainingProject::setName(const QString& name) { m_name = name; }

QString TrainingProject::description() const { return m_description; }
void TrainingProject::setDescription(const QString& desc) { m_description = desc; }

QDateTime TrainingProject::createdAt() const { return m_createdAt; }
QDateTime TrainingProject::modifiedAt() const { return m_modifiedAt; }

void TrainingProject::updateModifiedTime() {
    m_modifiedAt = QDateTime::currentDateTime();
}

int TrainingProject::schemaVersion() const { return m_schemaVersion; }

QString TrainingProject::filePath() const { return m_filePath; }
void TrainingProject::setFilePath(const QString& path) {
    if (m_filePath != path) {
        m_filePath = path;
        emit filePathChanged(path);
    }
}

bool TrainingProject::isDirty() const { return m_dirty; }

void TrainingProject::markDirty() {
    if (!m_dirty) {
        m_dirty = true;
        emit modifiedChanged(true);
    }
}

void TrainingProject::markClean() {
    if (m_dirty) {
        m_dirty = false;
        emit modifiedChanged(false);
    }
}

// ===== 快照收集 =====
void TrainingProject::setImagesSnapshot(const QList<ImageEntrySnapshot>& snapshots) {
    m_imagesSnapshot = snapshots;
}

void TrainingProject::setCategoriesSnapshot(const QJsonObject& categoriesJson) {
    m_categoriesSnapshot = categoriesJson;
}

void TrainingProject::setTrainingSnapshot(const TrainingParamsSnapshot& params,
                                           const TrainingStateSnapshot& state) {
    m_trainingParams = params;
    m_trainingState = state;
}

// ===== 快照读取 =====
QList<ImageEntrySnapshot> TrainingProject::imagesSnapshot() const {
    return m_imagesSnapshot;
}

QJsonObject TrainingProject::categoriesSnapshot() const {
    return m_categoriesSnapshot;
}

TrainingParamsSnapshot TrainingProject::trainingParams() const {
    return m_trainingParams;
}

TrainingStateSnapshot TrainingProject::trainingState() const {
    return m_trainingState;
}

// ===== 历史记录 =====
void TrainingProject::appendHistory(const QString& action, const QString& summary) {
    HistoryEntry entry;
    entry.time = QDateTime::currentDateTime();
    entry.action = action;
    entry.summary = summary;
    m_history.append(entry);
    // 超过上限丢弃最旧
    while (m_history.size() > MAX_HISTORY_ENTRIES) {
        m_history.removeFirst();
    }
}

QJsonArray TrainingProject::historyJson() const {
    QJsonArray arr;
    for (const auto& entry : m_history) {
        QJsonObject obj;
        obj["time"] = entry.time.toString(Qt::ISODate);
        obj["action"] = entry.action;
        obj["summary"] = entry.summary;
        arr.append(obj);
    }
    return arr;
}

void TrainingProject::loadHistory(const QJsonArray& arr) {
    m_history.clear();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        HistoryEntry entry;
        entry.time = QDateTime::fromString(obj["time"].toString(), Qt::ISODate);
        entry.action = obj["action"].toString();
        entry.summary = obj["summary"].toString();
        m_history.append(entry);
    }
}

// ===== 序列化 =====
QJsonObject TrainingProject::toJson() const {
    QJsonObject root;
    root["format"] = "qdv-training-project";
    root["version"] = "1.0";
    root["mode"] = "reference";  // 默认引用模式，由 ProjectSerializer 覆盖

    // 项目元数据
    QJsonObject projectMeta;
    projectMeta["name"] = m_name;
    projectMeta["description"] = m_description;
    projectMeta["createdAt"] = m_createdAt.toString(Qt::ISODate);
    projectMeta["modifiedAt"] = m_modifiedAt.toString(Qt::ISODate);
    projectMeta["schemaVersion"] = m_schemaVersion;
    root["project"] = projectMeta;

    // 修改历史
    root["history"] = historyJson();

    // 图像列表
    QJsonObject imagesObj;
    QJsonArray imagesArr;
    for (const auto& snap : m_imagesSnapshot) {
        QJsonObject imgObj;
        imgObj["filePath"] = snap.filePath;
        imgObj["originalPath"] = snap.originalPath;
        imgObj["fileName"] = snap.fileName;
        imgObj["width"] = snap.width;
        imgObj["height"] = snap.height;
        imgObj["channels"] = snap.channels;
        imgObj["format"] = snap.format;
        imgObj["fileSize"] = static_cast<qint64>(snap.fileSize);
        imgObj["isAnnotated"] = snap.isAnnotated;
        imgObj["label"] = snap.label;
        imgObj["isSelected"] = snap.isSelected;
        imagesArr.append(imgObj);
    }
    imagesObj["entries"] = imagesArr;
    root["images"] = imagesObj;

    // 类别树
    root["categories"] = m_categoriesSnapshot;

    // 训练参数和状态
    QJsonObject trainingObj;
    QJsonObject paramsObj;
    paramsObj["modelType"] = m_trainingParams.modelType;
    paramsObj["numEpochs"] = m_trainingParams.numEpochs;
    paramsObj["batchSize"] = m_trainingParams.batchSize;
    paramsObj["learningRate"] = m_trainingParams.learningRate;
    paramsObj["valSplit"] = m_trainingParams.valSplit;
    trainingObj["params"] = paramsObj;

    QJsonObject stateObj;
    stateObj["hasTrained"] = m_trainingState.hasTrained;
    stateObj["lastTrainedAt"] = m_trainingState.lastTrainedAt.toString(Qt::ISODate);
    stateObj["lastMetrics"] = QJsonObject::fromVariantMap(m_trainingState.lastMetrics);
    stateObj["onnxPath"] = m_trainingState.onnxPath;
    trainingObj["state"] = stateObj;

    root["training"] = trainingObj;

    return root;
}

bool TrainingProject::fromJson(const QJsonObject& obj, QString* errMsg) {
    // 校验 format
    if (obj["format"].toString() != "qdv-training-project") {
        if (errMsg) *errMsg = QString::fromUtf8("无效的项目文件格式: %1").arg(obj["format"].toString());
        return false;
    }

    // 校验 version
    QString version = obj["version"].toString();
    if (version != "1.0") {
        if (errMsg) *errMsg = QString::fromUtf8("项目文件版本不兼容，当前支持 1.0，文件版本 %1").arg(version);
        return false;
    }

    // 重置为初始状态，确保不保留旧项目无关数据
    reset();

    // 仅保留项目名称（用于 UI 标题显示），其余项目元数据不保留
    QJsonObject projectMeta = obj["project"].toObject();
    m_name = projectMeta["name"].toString();
    m_createdAt = QDateTime::currentDateTime();
    m_modifiedAt = m_createdAt;
    m_schemaVersion = 1;

    // 不保留修改历史
    m_history.clear();

    // 解析图像列表（核心信息 a）
    m_imagesSnapshot.clear();
    QJsonObject imagesObj = obj["images"].toObject();
    QJsonArray imagesArr = imagesObj["entries"].toArray();
    for (int i = 0; i < imagesArr.size(); ++i) {
        QJsonObject imgObj = imagesArr[i].toObject();
        ImageEntrySnapshot snap;
        snap.filePath = imgObj["filePath"].toString();
        snap.originalPath = imgObj["originalPath"].toString();
        snap.fileName = imgObj["fileName"].toString();
        snap.width = imgObj["width"].toInt(0);
        snap.height = imgObj["height"].toInt(0);
        snap.channels = imgObj["channels"].toInt(0);
        snap.format = imgObj["format"].toString();
        snap.fileSize = static_cast<qint64>(imgObj["fileSize"].toVariant().toLongLong());
        snap.isAnnotated = imgObj["isAnnotated"].toBool(false);
        snap.label = imgObj["label"].toString();
        snap.isSelected = imgObj["isSelected"].toBool(false);
        m_imagesSnapshot.append(snap);
    }

    // 解析类别树（核心信息 b）
    m_categoriesSnapshot = obj["categories"].toObject();

    // 仅保留模型选择/训练配置参数（核心信息 c），不保留训练历史状态
    QJsonObject trainingObj = obj["training"].toObject();
    QJsonObject paramsObj = trainingObj["params"].toObject();
    m_trainingParams.modelType = paramsObj["modelType"].toString("resnet18");
    m_trainingParams.numEpochs = paramsObj["numEpochs"].toInt(20);
    m_trainingParams.batchSize = paramsObj["batchSize"].toInt(8);
    m_trainingParams.learningRate = paramsObj["learningRate"].toDouble(0.001);
    m_trainingParams.valSplit = paramsObj["valSplit"].toDouble(0.2);

    // 训练结果状态重置为默认值，避免带入历史训练产物路径与指标
    m_trainingState = TrainingStateSnapshot();

    return true;
}

// ===== 重置 =====
void TrainingProject::reset() {
    m_name.clear();
    m_description.clear();
    m_createdAt = QDateTime::currentDateTime();
    m_modifiedAt = m_createdAt;
    m_schemaVersion = 1;
    m_filePath.clear();
    m_dirty = false;
    m_imagesSnapshot.clear();
    m_categoriesSnapshot = QJsonObject();
    m_trainingParams = TrainingParamsSnapshot();
    m_trainingState = TrainingStateSnapshot();
    m_history.clear();
    emit modifiedChanged(false);
}
