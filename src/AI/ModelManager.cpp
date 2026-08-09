#include "AI/ModelManager.h"
#include "AI/InferenceEngine.h"
#include "Core/Logger.h"
#include "Monitoring/TrainingInferenceMonitor.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QCryptographicHash>
#include <QFileInfoList>
#include <QRegularExpression>
#include <QBuffer>
#include <QByteArrayView>

using namespace QDV;

namespace {
// v2.7.3-5：快速文件头校验，拦截被错误重命名为 .onnx 的 PyTorch 检查点。
// ONNX Protobuf 文件通常以变长字段头开始，不会以 ZIP/Pickle 头开头。
bool isValidOnnxFileHeader(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    QByteArray header = f.read(4);
    if (header.size() < 4) {
        return false;
    }
    // ZIP：torch.save() 默认生成的 .pth / .pt 文件头
    if (header.startsWith("PK\x03\x04") || header.startsWith("PK\x05\x06") ||
        header.startsWith("PK\x07\x08")) {
        return false;
    }
    // Python Pickle：老版本 PyTorch .pt 文件头 (0x80 0x00..0x05)
    if (header[0] == '\x80' && header[1] >= '\x00' && header[1] <= '\x05') {
        return false;
    }
    return true;
}
} // namespace

ModelManager* ModelManager::s_instance = nullptr;

ModelManager* ModelManager::instance() {
    if (!s_instance) {
        s_instance = new ModelManager();
    }
    return s_instance;
}

ModelManager::ModelManager(QObject* parent) : QObject(parent) {
    // 初始化 manifest 路径并加载已有 manifest（P0 Task 2.4）
    m_manifestPath = QDir(defaultModelDirectory()).absoluteFilePath("manifest.json");
    loadManifest();

    // v2.7.3：启动时同步默认模型目录中的 .onnx 文件到 manifest。
    // 无论 manifest 是否为空，都会扫描目录；已注册模型会跳过，新模型自动注册，
    // 保证源码 models/ 目录新增模型后运行时模型库能自动感知。
    syncManifestWithDirectory();

    // v2.7.3-3：补全旧条目的 loadable/load_error 字段，确保模型库中兼容性状态一致。
    // 仅对缺失 loadable 字段的条目执行检测，避免每次启动全量重检。
    updateModelLoadability();
}

ModelManager::~ModelManager() {
    unloadAll();
}

QString ModelManager::defaultModelDirectory() const {
    // 动态获取：可执行文件所在目录/models
    // 替代旧硬编码 "E:/anchor/Trae/QDV/models"，支持任意安装位置
    QString dirPath = QCoreApplication::applicationDirPath() + "/models";
    // v2.7.3：确保目录存在，避免首次启动时 manifest 路径失效
    QDir dir(dirPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return dir.absolutePath();
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
    m_lastLoadError.clear();

    if (m_engines.contains(modelId)) {
        touch(modelId);
        if (m_modelInfos.contains(modelId)) {
            m_modelInfos[modelId].accessCount++;
        }
        QDV::Logger::debug("Model already loaded (cache hit): " + modelId);
        return true;
    }

    // v2.7.3-4：若 manifest 已记录该模型不兼容当前后端，直接失败并给出清晰原因，
    // 避免调用 OpenCV DNN 时弹出难以理解的解析错误弹窗。
    QString manifestFileName = QFileInfo(modelPath).fileName();
    for (const QJsonValue& value : manifestModels()) {
        QJsonObject entry = value.toObject();
        if (entry.value("file_name").toString() == manifestFileName) {
            if (entry.contains("loadable") && !entry.value("loadable").toBool(true)) {
                QString reason = entry.value("load_error").toString();
                if (reason.isEmpty()) {
                    reason = QStringLiteral("该模型与当前 OpenCV DNN 后端不兼容");
                }
                m_lastLoadError = QStringLiteral("模型加载被拒绝：%1").arg(reason);
                QDV::Logger::error(m_lastLoadError + " (" + modelPath + ")");
                QDV::TrainingInferenceMonitor::instance()->recordModelLoadStatus(
                    m_engines.size(), m_lastLoadError);
                return false;
            }
            break;
        }
    }

    while (m_engines.size() >= m_cacheSize) {
        evictLRU();
    }

    InferenceEngine* engine = new InferenceEngine(this);

    // 根据模型类型选择预处理参数:
    // 分类模型(ResNet等)使用 ImageNet 标准归一化 (mean + std)
    // YOLO 检测模型使用零 mean + 1/255 scale + 无 std
    QString modelType = autoDetectModelType(modelPath);
    bool isClassification = (modelType == "classification");

    cv::Scalar mean = isClassification
        ? cv::Scalar(InferenceEngine::IMAGENET_MEAN_R,
                     InferenceEngine::IMAGENET_MEAN_G,
                     InferenceEngine::IMAGENET_MEAN_B)
        : cv::Scalar(0, 0, 0);
    cv::Scalar stdNorm = isClassification
        ? cv::Scalar(InferenceEngine::IMAGENET_STD_R,
                     InferenceEngine::IMAGENET_STD_G,
                     InferenceEngine::IMAGENET_STD_B)
        : cv::Scalar(1.0, 1.0, 1.0);

    if (!engine->loadModel(modelPath, inputSize, mean, 1.0 / 255.0, true, stdNorm)) {
        m_lastLoadError = engine->lastError().errorMessage;
        if (m_lastLoadError.isEmpty()) {
            m_lastLoadError = "未知错误（可能是 OpenCV DNN 算子不兼容）";
        }
        QDV::Logger::error("Failed to load model: " + modelPath + ", reason: " + m_lastLoadError);

        // 上报模型加载失败状态到监控器
        QDV::TrainingInferenceMonitor::instance()->recordModelLoadStatus(
            m_engines.size(), m_lastLoadError);

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

    // --- 自动加载类别标签文件 ---
    // 根据模型文件名从 manifest 中查找对应的 labels_file；
    // 若 manifest 未记录，则回退到同目录下的 <base>_labels.json / labels.json。
    // 解析后注入 InferenceEngine，保证推理结果能显示训练时定义的类别名称。
    QString modelFileName = QFileInfo(modelPath).fileName();
    QDir modelDir = QFileInfo(modelPath).absoluteDir();
    QStringList candidateLabelFiles;

    // 1) manifest 中声明的标签文件（优先级最高）
    for (const QJsonValue& value : manifestModels()) {
        QJsonObject entry = value.toObject();
        if (entry.value("file_name").toString() == modelFileName) {
            QString labelsFileName = entry.value("labels_file").toString();
            if (!labelsFileName.isEmpty()) {
                candidateLabelFiles.append(labelsFileName);
            }
            break;
        }
    }

    // 2) manifest 中未记录时的兜底查找
    if (candidateLabelFiles.isEmpty()) {
        QString baseName = QFileInfo(modelPath).completeBaseName();
        candidateLabelFiles << baseName + "_labels.json" << "labels.json";
        QDV::Logger::debug(QString("Manifest 中未找到 %1 的标签记录，尝试回退查找: %2")
                           .arg(modelFileName)
                           .arg(candidateLabelFiles.join(", ")));
    }

    bool labelsLoaded = false;
    for (const QString& labelsFileName : candidateLabelFiles) {
        QString labelsPath = modelDir.absoluteFilePath(labelsFileName);
        if (!QFile::exists(labelsPath)) {
            continue;
        }

        QFile labelsFile(labelsPath);
        if (!labelsFile.open(QIODevice::ReadOnly)) {
            QDV::Logger::warn("无法打开 Labels 文件: " + labelsPath);
            continue;
        }

        QByteArray labelsData = labelsFile.readAll();
        labelsFile.close();
        QJsonParseError labelsParseErr;
        QJsonDocument labelsDoc = QJsonDocument::fromJson(labelsData, &labelsParseErr);
        if (labelsParseErr.error != QJsonParseError::NoError || !labelsDoc.isObject()) {
            QDV::Logger::warn("Labels 文件解析失败: " + labelsPath
                              + ", error: " + labelsParseErr.errorString());
            continue;
        }

        QJsonArray labelsArray = labelsDoc.object().value("labels").toArray();
        QStringList labels;
        labels.reserve(labelsArray.size());
        for (const QJsonValue& labelValue : labelsArray) {
            labels.append(labelValue.toString());
        }

        if (labels.isEmpty()) {
            QDV::Logger::warn("Labels 文件为空数组: " + labelsPath);
            continue;
        }

        engine->setCategoryLabels(labels);
        QDV::Logger::info(QString("已加载类别标签: %1 (%2 类)").arg(labelsPath).arg(labels.size()));
        labelsLoaded = true;
        break;
    }

    if (!labelsLoaded) {
        QDV::Logger::warn(QString("未能为模型加载类别标签: %1，推理结果将使用默认 Class_<id> 名称")
                          .arg(modelPath));
    }

    QDV::Logger::info(QString("Model loaded into cache: %1 (%2/%3)")
                  .arg(modelId).arg(m_engines.size()).arg(m_cacheSize));

    // 上报模型加载状态到监控器
    QDV::TrainingInferenceMonitor::instance()->recordModelLoadStatus(
        m_engines.size(), QString());

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

    // 上报模型加载状态到监控器
    QDV::TrainingInferenceMonitor::instance()->recordModelLoadStatus(
        m_engines.size(), QString());

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

    // 上报模型加载状态到监控器
    QDV::TrainingInferenceMonitor::instance()->recordModelLoadStatus(0, QString());
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
    // 若源文件已在目标位置，跳过复制，避免误删自身
    if (QFileInfo(onnxPath).canonicalFilePath() == QFileInfo(destOnnxPath).canonicalFilePath()) {
        QDV::Logger::info("registerTrainedModel: ONNX 模型已在目标位置，跳过复制: " + destOnnxPath);
    } else {
        // 如果目标已经存在且是不同的文件，先删除旧的
        if (QFile::exists(destOnnxPath)) {
            QFile::remove(destOnnxPath);
        }
        if (!QFile::copy(onnxPath, destOnnxPath)) {
            QDV::Logger::error("registerTrainedModel: 无法复制ONNX模型到: " + destOnnxPath);
            return false;
        }
        QDV::Logger::info("ONNX模型已复制到: " + destOnnxPath);
    }

    // --- 复制 labels 文件（如果提供且存在；同源同目标时跳过） ---
    bool labelsCopied = false;
    if (!labelsPath.isEmpty() && QFile::exists(labelsPath)) {
        if (QFileInfo(labelsPath).canonicalFilePath() == QFileInfo(destLabelsPath).canonicalFilePath()) {
            labelsCopied = true;
            QDV::Logger::info("registerTrainedModel: Labels文件已在目标位置，跳过复制: " + destLabelsPath);
        } else {
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

    // 预检模型加载兼容性
    auto loadability = probeModelLoadability(destOnnxPath);

    // 追加新模型条目
    QJsonArray modelsArray = manifestRoot["models"].toArray();
    QJsonObject newModel;
    newModel["file_name"] = finalName + ".onnx";
    newModel["version"] = "1.0.0";
    newModel["sha256"] = sha256;
    newModel["expected_size"] = fileSize;
    newModel["description"] = QString("训练生成: %1").arg(finalName);
    newModel["registered_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    newModel["loadable"] = loadability.first;
    newModel["load_error"] = loadability.second;
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

// =============================================================================
// P0 Task 2 新增方法实现
// =============================================================================

bool ModelManager::loadManifest() {
    // 确保 manifest 路径已初始化
    if (m_manifestPath.isEmpty()) {
        m_manifestPath = QDir(defaultModelDirectory()).absoluteFilePath("manifest.json");
    }

    QFile file(m_manifestPath);
    if (!file.exists()) {
        // 文件不存在：初始化空 manifest，并由调用方（构造函数）触发目录同步
        m_manifest = QJsonObject();
        m_manifest["version"] = "1.0";
        m_manifest["generated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        m_manifest["models"] = QJsonArray();
        QDV::Logger::info("loadManifest: manifest.json 不存在，已初始化空 manifest，将同步目录中的模型文件");
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        QDV::Logger::error("loadManifest: 无法读取 manifest.json: " + m_manifestPath);
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        QDV::Logger::error("loadManifest: manifest.json 解析失败: " + parseErr.errorString());
        // 解析失败时初始化空 manifest，避免后续操作崩溃
        m_manifest = QJsonObject();
        m_manifest["version"] = "1.0";
        m_manifest["generated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        m_manifest["models"] = QJsonArray();
        return false;
    }

    m_manifest = doc.object();

    // 兼容旧格式：确保存在 models 数组
    if (!m_manifest.contains("models") || !m_manifest["models"].isArray()) {
        m_manifest["models"] = QJsonArray();
    }

    // 兼容旧格式条目：逐条检查 file_name/sha256/expected_size 字段
    QJsonArray models = m_manifest["models"].toArray();
    bool changed = false;
    for (int i = 0; i < models.size(); ++i) {
        QJsonObject m = models[i].toObject();
        // 旧格式字段迁移：若无 sha256 字段则补空，若无 expected_size 则补 0
        if (!m.contains("sha256")) {
            m["sha256"] = QString();
            changed = true;
        }
        if (!m.contains("expected_size")) {
            m["expected_size"] = qint64(0);
            changed = true;
        }
        if (!m.contains("file_name")) {
            // 旧格式可能用 name 字段
            if (m.contains("name")) {
                m["file_name"] = m["name"].toString();
            } else {
                m["file_name"] = QString();
            }
            changed = true;
        }
        models[i] = m;
    }
    if (changed) {
        m_manifest["models"] = models;
    }

    QDV::Logger::info(QString("loadManifest: 成功加载 %1 个模型条目").arg(models.size()));
    return true;
}

bool ModelManager::saveManifest() {
    if (m_manifestPath.isEmpty()) {
        m_manifestPath = QDir(defaultModelDirectory()).absoluteFilePath("manifest.json");
    }

    // 确保目录存在
    QDir dir = QFileInfo(m_manifestPath).absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            QDV::Logger::error("saveManifest: 无法创建目录: " + dir.absolutePath());
            return false;
        }
    }

    QFile file(m_manifestPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QDV::Logger::error("saveManifest: 无法写入 manifest.json: " + m_manifestPath);
        return false;
    }

    QJsonDocument doc(m_manifest);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    QDV::Logger::info("saveManifest: manifest.json 已保存");
    return true;
}

// v2.7.3：启动时同步默认模型目录中的 .onnx 文件到 manifest。
// 修复 v2.7.3-1：按 file_name 精确匹配，避免 addCustomModel 因重命名产生重复副本。
// 例如：磁盘上 model_xxx.onnx 若已被导入为 model_xxx_20260715_123456.onnx，
// 旧逻辑按 baseName 匹配失败，会再次复制并注册；新逻辑按实际文件名匹配，直接跳过。
void ModelManager::syncManifestWithDirectory() {
    QString modelsDir = defaultModelDirectory();
    QDir dir(modelsDir);
    if (!dir.exists()) {
        QDV::Logger::warn("syncManifestWithDirectory: 默认模型目录不存在，无法同步: " + modelsDir);
        return;
    }

    QStringList filters = {"*.onnx"};
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    if (files.isEmpty()) {
        QDV::Logger::info("syncManifestWithDirectory: 默认模型目录中无 .onnx 文件，保持空 manifest");
        return;
    }

    // 收集 manifest 中已注册的 file_name（精确匹配）
    QSet<QString> registeredFileNames;
    for (const QJsonValue& v : m_manifest.value("models").toArray()) {
        QJsonObject m = v.toObject();
        QString fileName = m.value("file_name").toString();
        if (!fileName.isEmpty()) {
            registeredFileNames.insert(fileName.toLower());
        }
    }

    int added = 0;
    int skipped = 0;
    for (const QFileInfo& fi : files) {
        QString fileName = fi.fileName();
        // 精确匹配：磁盘上的文件名已存在于 manifest 中则跳过
        if (registeredFileNames.contains(fileName.toLower())) {
            skipped++;
            continue;
        }
        QString baseName = fi.completeBaseName();
        QDV::Logger::info("syncManifestWithDirectory: 自动注册模型: " + fi.absoluteFilePath());
        if (addCustomModel(fi.absoluteFilePath(), baseName)) {
            added++;
            // 注册成功后，addCustomModel 可能因重命名使用不同 file_name，
            // 为当前 session 后续文件去重，将实际使用的文件名也加入集合
            registeredFileNames.insert(fileName.toLower());
        }
    }

    QDV::Logger::info(QString("syncManifestWithDirectory: 跳过 %1 个已注册，自动注册 %2 个模型")
                      .arg(skipped).arg(added));
}

bool ModelManager::updateModelLoadability(const QString& modelId) {
    QJsonArray models = m_manifest.value("models").toArray();
    if (models.isEmpty()) {
        QDV::Logger::warn("updateModelLoadability: manifest 中无模型条目");
        return true;
    }

    QString modelsDir = defaultModelDirectory();
    bool allOk = true;
    bool changed = false;

    for (int i = 0; i < models.size(); ++i) {
        QJsonObject m = models[i].toObject();
        QString fileName = m.value("file_name").toString();
        QString displayName = m.value("display_name").toString();
        QString id = displayName.isEmpty() ? QFileInfo(fileName).completeBaseName() : displayName;

        // 若指定了 modelId，只更新匹配的条目
        if (!modelId.isEmpty() && id != modelId && fileName != modelId) {
            continue;
        }

        QString fullPath = QDir(modelsDir).absoluteFilePath(fileName);
        if (!QFile::exists(fullPath)) {
            m["loadable"] = false;
            m["load_error"] = QStringLiteral("模型文件不存在: %1").arg(fullPath);
            changed = true;
            allOk = false;
            continue;
        }

        // v2.7.3-3：批量更新时，若条目已存在 loadable 字段且文件存在，则跳过，
        // 避免每次启动都重新检测所有模型，同时保证旧条目（无 loadable）会被补全。
        if (modelId.isEmpty() && m.contains("loadable") && m.contains("load_error")) {
            continue;
        }

        auto probe = probeModelLoadability(fullPath);
        m["loadable"] = probe.first;
        m["load_error"] = probe.second;
        changed = true;

        if (!probe.first && modelId.isEmpty()) {
            allOk = false;
        }
    }

    if (changed) {
        m_manifest["models"] = models;
        if (!saveManifest()) {
            QDV::Logger::error("updateModelLoadability: 保存 manifest 失败");
            return false;
        }
    }

    return allOk;
}

QJsonArray ModelManager::manifestModels() const {
    return m_manifest.value("models").toArray();
}

QString ModelManager::autoDetectModelType(const QString& modelPath) {
    // YOLO 训练产物命名规则：best<YYYYMMDDHHMMSS>.(pt|onnx) 或 last<YYYYMMDDHHMMSS>.(pt|onnx)
    QString fileName = QFileInfo(modelPath).fileName();
    QRegularExpression yoloPattern(QStringLiteral("^(best|last)\\d{14}\\.(pt|onnx)$"),
                                   QRegularExpression::CaseInsensitiveOption);
    if (yoloPattern.match(fileName).hasMatch()) {
        return QStringLiteral("yolo");
    }
    // 文件名包含 "yolo" 也视为 YOLO 模型
    if (fileName.toLower().contains("yolo")) {
        return QStringLiteral("yolo");
    }
    return QStringLiteral("classification");
}

QSize ModelManager::autoDetectInputSize(const QString& modelPath) {
    if (autoDetectModelType(modelPath) == QStringLiteral("yolo")) {
        return QSize(640, 640);
    }
    return QSize(224, 224);
}

// v2.7.2：预检模型是否能被 OpenCV DNN 加载
// 直接调用 cv::dnn::readNetFromONNX 尝试解析，但不放入缓存
QPair<bool, QString> ModelManager::probeModelLoadability(const QString& modelPath) const {
    if (!QFile::exists(modelPath)) {
        return {false, QStringLiteral("模型文件不存在: %1").arg(modelPath)};
    }

    // v2.7.3-5：提前识别被错误重命名的 PyTorch 检查点，给出比 OpenCV 异常更清晰的提示
    if (!isValidOnnxFileHeader(modelPath)) {
        return {false, QStringLiteral("文件不是有效的 ONNX 格式（可能是 PyTorch 检查点被重命名为 .onnx）")};
    }

    try {
        cv::dnn::Net net = cv::dnn::readNetFromONNX(modelPath.toStdString());
        if (net.empty()) {
            return {false, QStringLiteral("OpenCV DNN 无法解析该 ONNX 模型（返回空网络）")};
        }
        return {true, QString()};
    } catch (const cv::Exception& e) {
        QString msg = QStringLiteral("OpenCV DNN 不兼容该 ONNX 模型: %1").arg(QString::fromLocal8Bit(e.what()));
        return {false, msg};
    } catch (const std::exception& e) {
        QString msg = QStringLiteral("模型预检异常: %1").arg(QString::fromLocal8Bit(e.what()));
        return {false, msg};
    }
}

bool ModelManager::addCustomModel(const QString& onnxPath, const QString& displayName,
                                   const QString& labelsPath) {
    // 输入校验
    if (displayName.isEmpty()) {
        QDV::Logger::error("addCustomModel: displayName 不能为空");
        return false;
    }
    if (!QFile::exists(onnxPath)) {
        QDV::Logger::error("addCustomModel: ONNX 源文件不存在: " + onnxPath);
        return false;
    }

    // v2.7.3-5：拒绝被错误重命名为 .onnx 的 PyTorch 检查点（ZIP/Pickle 头），
    // 避免注册后无法加载又难以定位问题。
    if (!isValidOnnxFileHeader(onnxPath)) {
        QDV::Logger::error("addCustomModel: 文件不是有效的 ONNX 格式（可能是 PyTorch 检查点被重命名为 .onnx）: " + onnxPath);
        return false;
    }

    const QString modelsDir = defaultModelDirectory();
    QDir dir(modelsDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            QDV::Logger::error("addCustomModel: 无法创建 models 目录: " + modelsDir);
            return false;
        }
    }

    // v2.7.3-2 修复：如果源文件已经在 models/ 目录内，直接按源文件名注册，
    // 不创建时间戳副本。这种情况常见于 CMake 部署的默认模型或 syncManifestWithDirectory
    // 扫描到的模型；若仍按旧逻辑检查目标文件是否存在，会误以为是命名冲突。
    // Windows 路径不区分大小写，使用 QDir 比较而非字符串比较。
    bool sourceAlreadyInModelsDir = QDir(QFileInfo(onnxPath).absolutePath()) == QDir(modelsDir);

    QString finalName = displayName;
    QString destOnnxPath = dir.absoluteFilePath(finalName + ".onnx");

    if (sourceAlreadyInModelsDir) {
        // 源文件已经在 models/ 目录：使用源文件实际文件名注册
        finalName = QFileInfo(onnxPath).completeBaseName();
        destOnnxPath = onnxPath;
        QDV::Logger::info(QString("addCustomModel: 源文件已在 models/ 目录，直接注册: %1").arg(finalName));
    } else if (QFile::exists(destOnnxPath)) {
        // 命名冲突：追加时间戳后缀
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        finalName = displayName + "_" + timestamp;
        destOnnxPath = dir.absoluteFilePath(finalName + ".onnx");
        QDV::Logger::info(QString("addCustomModel: 模型名称冲突，使用新名称: %1").arg(finalName));
    }

    // 构建 labels 目标路径
    QString destLabelsPath = dir.absoluteFilePath(finalName + "_labels.json");
    if (!sourceAlreadyInModelsDir && QFile::exists(destLabelsPath) && finalName == displayName) {
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        finalName = displayName + "_" + timestamp;
        destOnnxPath = dir.absoluteFilePath(finalName + ".onnx");
        destLabelsPath = dir.absoluteFilePath(finalName + "_labels.json");
    }

    // 复制 ONNX 模型文件（若源文件已在目标位置，则跳过复制，避免误删自身）
    if (QFileInfo(onnxPath).canonicalFilePath() == QFileInfo(destOnnxPath).canonicalFilePath()) {
        QDV::Logger::info("addCustomModel: ONNX 模型已在目标位置，跳过复制: " + destOnnxPath);
    } else {
        if (QFile::exists(destOnnxPath)) {
            QFile::remove(destOnnxPath);
        }
        if (!QFile::copy(onnxPath, destOnnxPath)) {
            QDV::Logger::error("addCustomModel: 无法复制 ONNX 模型到: " + destOnnxPath);
            return false;
        }
        QDV::Logger::info("addCustomModel: ONNX 模型已复制到: " + destOnnxPath);
    }

    // 复制 labels 文件（如果提供且存在；同源同目标时跳过）
    bool labelsCopied = false;
    if (!labelsPath.isEmpty() && QFile::exists(labelsPath)) {
        if (QFileInfo(labelsPath).canonicalFilePath() == QFileInfo(destLabelsPath).canonicalFilePath()) {
            labelsCopied = true;
            QDV::Logger::info("addCustomModel: labels 文件已在目标位置，跳过复制: " + destLabelsPath);
        } else {
            if (QFile::exists(destLabelsPath)) {
                QFile::remove(destLabelsPath);
            }
            if (QFile::copy(labelsPath, destLabelsPath)) {
                QDV::Logger::info("addCustomModel: labels 文件已复制到: " + destLabelsPath);
                labelsCopied = true;
            } else {
                QDV::Logger::warn("addCustomModel: 无法复制 labels 文件到: " + destLabelsPath);
            }
        }
    }

    // 计算文件 SHA256（流式，1MB 缓冲）
    QFile onnxFile(destOnnxPath);
    if (!onnxFile.open(QIODevice::ReadOnly)) {
        QDV::Logger::error("addCustomModel: 无法读取已复制模型计算哈希: " + destOnnxPath);
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const qint64 bufferSize = 1024 * 1024;  // 1MB 缓冲区
    QByteArray buffer;
    buffer.resize(bufferSize);
    while (!onnxFile.atEnd()) {
        qint64 read = onnxFile.read(buffer.data(), bufferSize);
        if (read <= 0) break;
        hash.addData(QByteArrayView(buffer.constData(), read));
    }
    onnxFile.close();
    QString sha256 = hash.result().toHex();
    qint64 fileSize = QFileInfo(destOnnxPath).size();

    // 预检模型加载兼容性（不放入缓存）
    auto loadability = probeModelLoadability(destOnnxPath);

    // 更新 manifest（使用内存中的 m_manifest）
    QJsonArray modelsArray = m_manifest["models"].toArray();
    QJsonObject newModel;
    newModel["file_name"] = finalName + ".onnx";
    newModel["version"] = "1.0.0";
    newModel["sha256"] = sha256;
    newModel["expected_size"] = fileSize;
    newModel["description"] = QString("自定义导入: %1").arg(finalName);
    newModel["registered_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    newModel["display_name"] = finalName;
    newModel["loadable"] = loadability.first;
    newModel["load_error"] = loadability.second;
    if (labelsCopied) {
        newModel["labels_file"] = finalName + "_labels.json";
    }
    modelsArray.append(newModel);
    m_manifest["models"] = modelsArray;

    // 保存 manifest 到磁盘
    if (!saveManifest()) {
        QDV::Logger::error("addCustomModel: 保存 manifest 失败");
        return false;
    }

    QDV::Logger::info(QString("addCustomModel: 模型注册成功: %1 (onnx=%2)").arg(finalName, destOnnxPath));
    emit modelRegistered(finalName, destOnnxPath);
    emit modelsChanged();
    return true;
}

bool ModelManager::removeCustomModelTransactional(const QString& modelId, bool deleteRelatedData) {
    // 在 manifest 中查找对应条目
    QJsonArray models = m_manifest["models"].toArray();
    int targetIndex = -1;
    QJsonObject targetModel;
    QString targetFileName;
    for (int i = 0; i < models.size(); ++i) {
        QJsonObject m = models[i].toObject();
        // 匹配 file_name（去掉 .onnx 后缀）或 display_name
        QString fileName = m.value("file_name").toString();
        QString baseName = fileName;
        if (baseName.endsWith(".onnx", Qt::CaseInsensitive)) {
            baseName = baseName.left(baseName.length() - 5);
        }
        QString displayName = m.value("display_name").toString();
        if (baseName == modelId || displayName == modelId || fileName == modelId) {
            targetIndex = i;
            targetModel = m;
            targetFileName = fileName;
            break;
        }
    }

    if (targetIndex < 0) {
        QDV::Logger::warn("removeCustomModelTransactional: 未找到模型条目: " + modelId);
        return false;
    }

    const QString modelsDir = defaultModelDirectory();
    QDir dir(modelsDir);
    QString onnxPath = dir.absoluteFilePath(targetFileName);

    // 事务性删除：将模型文件 rename 为 .trash 后缀（而非直接删除）
    auto moveToTrash = [&](const QString& filePath) -> bool {
        if (!QFile::exists(filePath)) return true;
        QString trashPath = filePath + ".trash";
        if (QFile::exists(trashPath)) {
            QFile::remove(trashPath);
        }
        if (!QFile::rename(filePath, trashPath)) {
            QDV::Logger::error("removeCustomModelTransactional: 无法重命名到 .trash: " + filePath);
            return false;
        }
        QDV::Logger::info("removeCustomModelTransactional: 文件已移至 .trash: " + trashPath);
        return true;
    };

    if (!moveToTrash(onnxPath)) {
        return false;
    }

    // 同时将 labels 文件移至 .trash（如果存在）
    QString labelsFile = targetModel.value("labels_file").toString();
    if (!labelsFile.isEmpty()) {
        QString labelsPath = dir.absoluteFilePath(labelsFile);
        moveToTrash(labelsPath);
    }

    // 关联数据清理：训练记录、评估报告、日志等（约定与模型同名、扩展名不同）
    if (deleteRelatedData) {
        QString baseName = QFileInfo(targetFileName).completeBaseName();
        QStringList relatedSuffixes = {
            "_train.log", "_eval.json", "_report.json", "_report.txt",
            "_metrics.jsonl", "_history.json", ".log", ".txt"
        };
        for (const QString& suffix : relatedSuffixes) {
            QString relatedPath = dir.absoluteFilePath(baseName + suffix);
            if (QFile::exists(relatedPath)) {
                moveToTrash(relatedPath);
            }
        }
        QDV::Logger::info(QString("removeCustomModelTransactional: 已清理模型 %1 的关联数据")
                          .arg(modelId));
    }

    // 在 manifest 中记录删除操作（deleted_models 数组）
    QJsonArray deletedModels = m_manifest["deleted_models"].toArray();
    QJsonObject deleteRecord;
    deleteRecord["model_id"] = modelId;
    deleteRecord["file_name"] = targetFileName;
    deleteRecord["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    deleteRecord["operator"] = "ModelManager";
    deleteRecord["related_data_deleted"] = deleteRelatedData;
    deletedModels.append(deleteRecord);
    m_manifest["deleted_models"] = deletedModels;

    // 从 models 数组中移除该条目
    models.removeAt(targetIndex);
    m_manifest["models"] = models;

    // 卸载引擎实例（如果已加载）
    unloadModel(modelId);

    // 保存 manifest
    if (!saveManifest()) {
        QDV::Logger::error("removeCustomModelTransactional: 保存 manifest 失败");
        return false;
    }

    QDV::Logger::info("removeCustomModelTransactional: 模型已事务性删除: " + modelId);
    emit modelsChanged();
    return true;
}

bool ModelManager::verifyModelIntegrity(const QString& modelId) {
    // 从 manifest 中查找 modelId 对应的 sha256 和 expected_size
    QJsonArray models = m_manifest["models"].toArray();
    QJsonObject targetModel;
    QString targetFileName;
    for (int i = 0; i < models.size(); ++i) {
        QJsonObject m = models[i].toObject();
        QString fileName = m.value("file_name").toString();
        QString baseName = fileName;
        if (baseName.endsWith(".onnx", Qt::CaseInsensitive)) {
            baseName = baseName.left(baseName.length() - 5);
        }
        QString displayName = m.value("display_name").toString();
        if (baseName == modelId || displayName == modelId || fileName == modelId) {
            targetModel = m;
            targetFileName = fileName;
            break;
        }
    }

    if (targetModel.isEmpty()) {
        QDV::Logger::error("verifyModelIntegrity: manifest 中未找到模型: " + modelId);
        return false;
    }

    QString expectedSha256 = targetModel.value("sha256").toString();
    qint64 expectedSize = targetModel.value("expected_size").toVariant().toLongLong();

    QString onnxPath = QDir(defaultModelDirectory()).absoluteFilePath(targetFileName);
    if (!QFile::exists(onnxPath)) {
        QDV::Logger::error("verifyModelIntegrity: 模型文件不存在: " + onnxPath);
        return false;
    }

    // 比对文件大小
    qint64 actualSize = QFileInfo(onnxPath).size();
    if (expectedSize > 0 && actualSize != expectedSize) {
        QDV::Logger::error(QString("verifyModelIntegrity: 文件大小不匹配 (期望 %1, 实际 %2): %3")
                           .arg(expectedSize).arg(actualSize).arg(onnxPath));
        return false;
    }

    // 流式计算 SHA256（1MB 缓冲区，避免大模型一次性加载）
    QFile file(onnxPath);
    if (!file.open(QIODevice::ReadOnly)) {
        QDV::Logger::error("verifyModelIntegrity: 无法读取模型文件: " + onnxPath);
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const qint64 bufferSize = 1024 * 1024;  // 1MB
    QByteArray buffer;
    buffer.resize(bufferSize);
    while (!file.atEnd()) {
        qint64 read = file.read(buffer.data(), bufferSize);
        if (read <= 0) break;
        hash.addData(QByteArrayView(buffer.constData(), read));
    }
    file.close();
    QString actualSha256 = hash.result().toHex();

    if (expectedSha256.isEmpty()) {
        // manifest 中无哈希记录（旧格式），仅校验大小已通过
        QDV::Logger::warn("verifyModelIntegrity: manifest 无 sha256 记录，仅校验大小通过: " + modelId);
        return true;
    }

    if (actualSha256 != expectedSha256) {
        QDV::Logger::error(QString("verifyModelIntegrity: SHA256 不匹配 (期望 %1, 实际 %2): %3")
                           .arg(expectedSha256).arg(actualSha256).arg(onnxPath));
        return false;
    }

    QDV::Logger::info("verifyModelIntegrity: 完整性校验通过: " + modelId);
    return true;
}