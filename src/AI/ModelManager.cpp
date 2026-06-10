#include "AI/ModelManager.h"
#include "AI/InferenceEngine.h"
#include "Core/Logger.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QCryptographicHash>
#include <QFileInfoList>

using namespace QDV;

ModelManager* ModelManager::s_instance = nullptr;

ModelManager* ModelManager::instance() {
    if (!s_instance) {
        s_instance = new ModelManager();
    }
    return s_instance;
}

ModelManager::ModelManager(QObject* parent) : QObject(parent) {
}

ModelManager::~ModelManager() {
    unloadAll();
}

QString ModelManager::defaultModelDirectory() const {
    return QString::fromLatin1(DEFAULT_MODEL_DIR);
}

QStringList ModelManager::findDefaultModels() const {
    QString dirPath = defaultModelDirectory();
    QDir dir(dirPath);
    
    if (!dir.exists()) {
        QDV::Logger::warn(QString("默认模型目录不存在: %1").arg(dirPath));
        return QStringList();
    }
    
    QStringList filters = {"*.onnx", "*.pth", "*.pt", "*.bin"};
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    
    QStringList modelPaths;
    for (const auto& file : files) {
        modelPaths.append(file.absoluteFilePath());
    }
    
    if (modelPaths.isEmpty()) {
        QDV::Logger::warn(QString("在默认模型目录中未找到模型文件: %1").arg(dirPath));
    }
    
    return modelPaths;
}

QString ModelManager::getDefaultModelPath() const {
    QStringList models = findDefaultModels();
    
    if (models.isEmpty()) {
        return QString();
    }
    
    // 优先选择yolo模型，其次选择resnet，最后选择第一个
    for (const QString& path : models) {
        QString fileName = QFileInfo(path).fileName().toLower();
        if (fileName.contains("yolo")) {
            return path;
        }
    }
    
    for (const QString& path : models) {
        QString fileName = QFileInfo(path).fileName().toLower();
        if (fileName.contains("resnet")) {
            return path;
        }
    }
    
    return models.first();
}

bool ModelManager::loadDefaultModel(const QString& modelId) {
    QString modelPath = getDefaultModelPath();
    
    if (modelPath.isEmpty()) {
        QString error = QString("无法找到默认模型。请确保 %1 目录下存在有效的模型文件").arg(defaultModelDirectory());
        QDV::Logger::error(error);
        emit defaultModelLoadFailed(error);
        return false;
    }
    
    if (!QFile::exists(modelPath)) {
        QString error = QString("默认模型文件不存在: %1").arg(modelPath);
        QDV::Logger::error(error);
        emit defaultModelLoadFailed(error);
        return false;
    }
    
    QDV::Logger::info(QString("正在加载默认模型: %1").arg(modelPath));
    
    bool success = loadModel(modelPath, modelId);
    
    if (success) {
        QDV::Logger::info(QString("默认模型加载成功: %1").arg(modelPath));
    } else {
        QString error = QString("默认模型加载失败: %1").arg(modelPath);
        QDV::Logger::error(error);
        emit defaultModelLoadFailed(error);
    }
    
    return success;
}

bool ModelManager::isDefaultModelAvailable() const {
    return !getDefaultModelPath().isEmpty();
}

QStringList ModelManager::getAvailableModelNames() const {
    QStringList names;
    
    // 添加发现的所有模型
    QStringList models = findDefaultModels();
    for (const QString& path : models) {
        QFileInfo fi(path);
        QString name = fi.completeBaseName();
        if (!names.contains(name)) {
            names << name;
        }
    }
    
    return names;
}

QString ModelManager::getModelPathByName(const QString& modelName) const {
    QStringList models = findDefaultModels();
    for (const QString& path : models) {
        QFileInfo fi(path);
        if (fi.completeBaseName() == modelName) {
            return path;
        }
    }
    
    // 如果没有找到，尝试使用默认模型路径
    return getDefaultModelPath();
}

void ModelManager::setCacheSize(int size) {
    m_cacheSize = std::max(1, size);
    while (m_engines.size() > m_cacheSize) {
        evictLRU();
    }
}

void ModelManager::touch(const QString& modelId) {
    m_accessOrder.removeAll(modelId);
    m_accessOrder.prepend(modelId);
}

void ModelManager::evictLRU() {
    if (m_accessOrder.isEmpty()) return;

    QString evictId = m_accessOrder.takeLast();
    if (m_engines.contains(evictId)) {
        InferenceEngine* engine = m_engines.take(evictId);
        engine->unloadModel();
        delete engine;

        if (m_modelInfos.contains(evictId)) {
            m_modelInfos.remove(evictId);
        }

        QDV::Logger::info("LRU evicted model: " + evictId);
        emit modelEvicted(evictId);
    }
}

bool ModelManager::loadModel(const QString& modelPath, const QString& modelId,
                              const QSize& inputSize) {
    if (m_engines.contains(modelId)) {
        touch(modelId);
        if (m_modelInfos.contains(modelId)) {
            m_modelInfos[modelId].accessCount++;
        }
        QDV::Logger::debug("Model already loaded (cache hit): " + modelId);
        return true;
    }

    while (m_engines.size() >= m_cacheSize) {
        evictLRU();
    }

    InferenceEngine* engine = new InferenceEngine(this);
    if (!engine->loadModel(modelPath, inputSize)) {
        QDV::Logger::error("Failed to load model: " + modelPath);
        delete engine;
        return false;
    }

    m_engines[modelId] = engine;
    m_accessOrder.prepend(modelId);

    ModelInfo info;
    info.id = modelId;
    info.path = modelPath;
    info.inputSize = inputSize;
    info.accessCount = 1;
    m_modelInfos[modelId] = info;

    QDV::Logger::info(QString("Model loaded into cache: %1 (%2/%3)")
                  .arg(modelId).arg(m_engines.size()).arg(m_cacheSize));
    emit modelLoaded(modelId);
    return true;
}

bool ModelManager::unloadModel(const QString& modelId) {
    if (!m_engines.contains(modelId)) {
        return false;
    }

    InferenceEngine* engine = m_engines.take(modelId);
    engine->unloadModel();
    delete engine;

    m_accessOrder.removeAll(modelId);
    m_modelInfos.remove(modelId);

    emit modelUnloaded(modelId);
    return true;
}

void ModelManager::unloadAll() {
    for (auto it = m_engines.begin(); it != m_engines.end(); ++it) {
        it.value()->unloadModel();
        delete it.value();
    }
    m_engines.clear();
    m_accessOrder.clear();
    m_modelInfos.clear();
}

bool ModelManager::warmUp(const QString& modelId, int iterations) {
    InferenceEngine* engine = getEngine(modelId);
    if (!engine) {
        QDV::Logger::warn("Warm-up failed: model not loaded: " + modelId);
        return false;
    }

    QDV::Logger::info(QString("Warming up model: %1 (%2 iterations)").arg(modelId).arg(iterations));
    bool ok = engine->warmUp(iterations);

    if (ok && m_modelInfos.contains(modelId)) {
        m_modelInfos[modelId].avgInferenceMs = engine->lastMetrics().totalMs;
        emit warmUpCompleted(modelId, engine->lastMetrics().totalMs);
    }

    return ok;
}

InferenceEngine* ModelManager::getEngine(const QString& modelId) {
    if (m_engines.contains(modelId)) {
        touch(modelId);
        if (m_modelInfos.contains(modelId)) {
            m_modelInfos[modelId].accessCount++;
        }
    }
    return m_engines.value(modelId, nullptr);
}

QStringList ModelManager::loadedModelIds() const {
    return m_engines.keys();
}

QList<ModelInfo> ModelManager::loadedModelInfo() const {
    return m_modelInfos.values();
}

QStringList ModelManager::listModels(const QString& directory) {
    QStringList models;
    QDir dir(directory);
    if (!dir.exists()) return models;

    QStringList filters = {"*.onnx", "*.pth", "*.pt", "*.bin"};
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);
    for (const auto& file : files) {
        models.append(file.absoluteFilePath());
    }
    return models;
}

bool ModelManager::registerTrainedModel(const QString& onnxPath,
                                        const QString& labelsPath,
                                        const QString& modelName)
{
    // --- 输入校验 ---
    if (modelName.isEmpty()) {
        QDV::Logger::error("registerTrainedModel: modelName不能为空");
        return false;
    }

    const QString modelsDir = defaultModelDirectory();
    QDir dir(modelsDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            QDV::Logger::error("registerTrainedModel: 无法创建models目录: " + modelsDir);
            return false;
        }
    }

    // --- 处理命名冲突：检查目标路径是否已存在同名文件 ---
    QString finalName = modelName;
    QString destOnnxPath = dir.absoluteFilePath(finalName + ".onnx");
    if (QFile::exists(destOnnxPath)) {
        // 存在冲突，追加时间戳后缀
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        finalName = modelName + "_" + timestamp;
        destOnnxPath = dir.absoluteFilePath(finalName + ".onnx");
        QDV::Logger::info(QString("模型名称冲突，使用新名称: %1").arg(finalName));
    }

    // --- 构建目标labels文件路径 ---
    QString destLabelsPath = dir.absoluteFilePath(finalName + "_labels.json");
    // 如果labels文件和onnx targets冲突（极其罕见），用时间戳做二次消歧
    if (QFile::exists(destLabelsPath) && finalName == modelName) {
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        finalName = modelName + "_" + timestamp;
        destOnnxPath = dir.absoluteFilePath(finalName + ".onnx");
        destLabelsPath = dir.absoluteFilePath(finalName + "_labels.json");
    }

    // --- 复制 ONNX 模型文件 ---
    if (!QFile::exists(onnxPath)) {
        QDV::Logger::error("registerTrainedModel: ONNX源文件不存在: " + onnxPath);
        return false;
    }
    // 如果目标已经存在且是不同的文件，先删除旧的
    if (QFile::exists(destOnnxPath)) {
        QFile::remove(destOnnxPath);
    }
    if (!QFile::copy(onnxPath, destOnnxPath)) {
        QDV::Logger::error("registerTrainedModel: 无法复制ONNX模型到: " + destOnnxPath);
        return false;
    }
    QDV::Logger::info("ONNX模型已复制到: " + destOnnxPath);

    // --- 复制 labels 文件（如果提供且存在） ---
    bool labelsCopied = false;
    if (!labelsPath.isEmpty() && QFile::exists(labelsPath)) {
        if (QFile::exists(destLabelsPath)) {
            QFile::remove(destLabelsPath);
        }
        if (QFile::copy(labelsPath, destLabelsPath)) {
            QDV::Logger::info("Labels文件已复制到: " + destLabelsPath);
            labelsCopied = true;
        } else {
            QDV::Logger::warn("registerTrainedModel: 无法复制labels文件到: " + destLabelsPath);
        }
    }

    // --- 计算文件SHA256用于manifest ---
    auto computeSha256 = [](const QString& filePath) -> QString {
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) return QString();
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&f);
        return hash.result().toHex();
    };

    QString sha256 = computeSha256(destOnnxPath);
    qint64 fileSize = QFileInfo(destOnnxPath).size();

    // --- 更新 manifest.json ---
    QString manifestPath = dir.absoluteFilePath("manifest.json");
    QJsonObject manifestRoot;

    // 尝试读取现有manifest
    QFile manifestFile(manifestPath);
    if (manifestFile.exists() && manifestFile.open(QIODevice::ReadOnly)) {
        QByteArray existingData = manifestFile.readAll();
        manifestFile.close();
        QJsonParseError parseErr;
        QJsonDocument existingDoc = QJsonDocument::fromJson(existingData, &parseErr);
        if (parseErr.error == QJsonParseError::NoError && existingDoc.isObject()) {
            manifestRoot = existingDoc.object();
        } else {
            QDV::Logger::warn("manifest.json解析失败，将重新创建: " + parseErr.errorString());
            manifestRoot = QJsonObject();
            manifestRoot["version"] = "1.0";
            manifestRoot["generated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        }
    } else {
        // 新建manifest
        manifestRoot["version"] = "1.0";
        manifestRoot["generated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        manifestRoot["models"] = QJsonArray();
    }

    // 追加新模型条目
    QJsonArray modelsArray = manifestRoot["models"].toArray();
    QJsonObject newModel;
    newModel["file_name"] = finalName + ".onnx";
    newModel["version"] = "1.0.0";
    newModel["sha256"] = sha256;
    newModel["expected_size"] = fileSize;
    newModel["description"] = QString("训练生成: %1").arg(finalName);
    newModel["registered_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    if (labelsCopied) {
        newModel["labels_file"] = finalName + "_labels.json";
    }
    modelsArray.append(newModel);
    manifestRoot["models"] = modelsArray;

    // 写回manifest
    QFile writeManifest(manifestPath);
    if (!writeManifest.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QDV::Logger::error("registerTrainedModel: 无法写入manifest.json: " + manifestPath);
        return false;
    }
    QJsonDocument doc(manifestRoot);
    writeManifest.write(doc.toJson(QJsonDocument::Indented));
    writeManifest.close();

    QDV::Logger::info(QString("模型注册成功: %1 (onnx=%2)").arg(finalName, destOnnxPath));
    emit modelRegistered(finalName, destOnnxPath);
    return true;
}