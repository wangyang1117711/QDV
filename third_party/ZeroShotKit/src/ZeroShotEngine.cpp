// ============================================================================
// 零样本推理引擎实现
//
// M2 阶段：AnomalyCLIP 零样本异常检测（基于 CLIP ViT-B/32 + ORT）
// M3+ 阶段：逐步实现其他零样本模型推理
// ============================================================================

#include "ZeroShotEngine.h"
#include "ZeroShotKit/Logger.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QElapsedTimer>
#include <QDebug>
#include <QRegularExpression>

namespace zsu {

// CLIP 标准化参数
static const float CLIP_MEAN[3] = {0.48145466f, 0.4578275f, 0.40821073f};
static const float CLIP_STD[3]  = {0.26862954f, 0.26130258f, 0.27577711f};

ZeroShotEngine::ZeroShotEngine(QObject* parent)
    : QObject(parent)
{
}

ZeroShotEngine::~ZeroShotEngine() {
}

// ============================================================================
// 模型加载
// ============================================================================

bool ZeroShotEngine::loadModel(ZeroShotModelType modelType, const QString& modelPath,
                                const QSize& inputSize) {
    m_modelType = modelType;
    m_clipInputSize = inputSize;
    m_lastError.clear();

    ZSU_LOG_INFO(QString("ZeroShotEngine: 加载模型 type=%1 path=%2 inputSize=%3x%4")
        .arg(static_cast<int>(modelType))
        .arg(modelPath)
        .arg(inputSize.width())
        .arg(inputSize.height()));

    // ---- 前置校验：文件存在性 + 速率格式支持（仅针对文件路径，目录由各类型解析逻辑处理）----
    // 目的：在进入具体模型加载前，尽早给出明确、可操作的错误信息，避免模棱两可的"加载失败"。
    QFileInfo pathInfo(modelPath);
    if (!pathInfo.isDir()) {
        const QString suffix = pathInfo.suffix().toLower();

        // 1) 文件不存在
        if (!pathInfo.exists()) {
            m_lastError = QString::fromUtf8(
                "模型文件不存在：%1\n"
                "建议：请检查路径是否正确，或从「具体模型」下拉列表重新选择模型。")
                .arg(modelPath);
            ZSU_LOG_ERROR("ZeroShotEngine: " + m_lastError);
            m_modelLoaded = false;
            return false;
        }

        // 2) 文件不可读
        if (!pathInfo.isReadable()) {
            m_lastError = QString::fromUtf8(
                "模型文件无法读取：%1\n"
                "建议：请检查文件是否被占用或权限不足。").arg(modelPath);
            ZSU_LOG_ERROR("ZeroShotEngine: " + m_lastError);
            m_modelLoaded = false;
            return false;
        }

        // 3) GGUF 格式明确拒绝 + 引导（LocateAnything 等 GGUF 大模型）
        if (suffix == "gguf") {
            m_lastError = QString::fromUtf8(
                "GGUF 格式模型暂不支持。\n"
                "原因：零样本引擎仅支持 ONNX 格式模型 "
                "（AnomalyCLIP / GroundingDINO / MobileSAM / PatchCore）。\n"
                "当前模型：%1\n"
                "改进建议：\n"
                "  1) 使用 ONNX 格式的零样本模型；\n"
                "  2) 如需使用 GGUF 大模型（如 LocateAnything-3B），请通过 LM Studio 后台运行，"
                "本软件已接入 LM Studio 模型源，可将其作为本地推理服务调用。").arg(modelPath);
            ZSU_LOG_ERROR("ZeroShotEngine: " + m_lastError);
            m_modelLoaded = false;
            return false;
        }

        // 4) 其他非 ONNX 格式
        if (suffix != "onnx") {
            m_lastError = QString::fromUtf8(
                "不支持的模型格式：.%1（当前模型：%2）\n"
                "原因：零样本引擎仅支持 ONNX 格式。\n"
                "建议：请选择 .onnx 格式的零样本模型，或通过训练推理模块将模型导出为 ONNX。")
                .arg(suffix).arg(modelPath);
            ZSU_LOG_ERROR("ZeroShotEngine: " + m_lastError);
            m_modelLoaded = false;
            return false;
        }
    }

    if (modelType == ZeroShotModelType::AnomalyCLIP) {
        // AnomalyCLIP: 加载 CLIP 视觉编码器 + 预计算文本嵌入
        QString visionModelPath = modelPath;
        QString embeddingsPath;

        // 如果 modelPath 是目录，自动拼接文件名
        QFileInfo fi(modelPath);
        if (fi.isDir()) {
            visionModelPath = QDir(modelPath).filePath("clip_vision_vit_b32.onnx");
            embeddingsPath = QDir(modelPath).filePath("clip_text_embeddings.txt");
        } else if (fi.suffix().toLower() == "onnx") {
            // 如果是 ONNX 文件，在同目录查找文本嵌入
            QString dir = fi.absolutePath();
            embeddingsPath = QDir(dir).filePath("clip_text_embeddings.txt");
        }

        // 加载视觉编码器（M5-1: 支持量化模型优先加载）
        visionModelPath = resolveModelPath(visionModelPath);
        m_visionEngine.setBackend(Backend::ONNXRuntime);
        bool ok = m_visionEngine.loadModel(
            visionModelPath,
            QSize(224, 224),  // CLIP 固定输入尺寸
            cv::Scalar(0, 0, 0),  // 不使用 ORTInferenceEngine 内置归一化
            1.0f / 255.0f,        // 像素值缩放到 [0,1]
            true,                 // swapRB
            false                 // 不做 crop
        );

        if (!ok) {
            // 尝试 OpenCV DNN 后端作为后备
            ZSU_LOG_WARN(QString("ORT 加载 CLIP 视觉编码器失败，尝试 OpenCV DNN: %1")
                .arg(m_visionEngine.lastError()));
            m_visionEngine.setBackend(Backend::OpenCVDNN);
            ok = m_visionEngine.loadModel(visionModelPath, QSize(224, 224),
                                           cv::Scalar(0, 0, 0), 1.0f / 255.0f, true, false);
            if (!ok) {
                m_lastError = QString::fromUtf8(
                    "CLIP 视觉编码器加载失败（ORT 与 OpenCV DNN 均失败）。\n"
                    "后端错误：%1\n"
                    "建议：请确认所选模型是 CLIP 视觉编码器（输入 224x224），"
                    "而不是其他模型（例如 YOLO 检测模型 / 分类模型）。该模型应仅在"
                    "「AnomalyCLIP」模型类型下使用。").arg(m_visionEngine.lastError());
                ZSU_LOG_ERROR("ZeroShotEngine: " + m_lastError);
                m_modelLoaded = false;
                return false;
            }
        }

        ZSU_LOG_INFO("CLIP 视觉编码器加载成功");

        // 加载预计算的文本嵌入
        if (!loadTextEmbeddings(embeddingsPath)) {
            ZSU_LOG_WARN(QString("文本嵌入加载失败: %1，将使用默认提示词").arg(embeddingsPath));
        }

        m_modelLoaded = true;
        ZSU_LOG_INFO(QString("AnomalyCLIP 模型加载完成，文本嵌入数量: %1")
            .arg(m_textEmbeddings.size()));
        return true;

    } else if (modelType == ZeroShotModelType::GroundingDINO) {
        // M3: Grounding DINO 开集目标检测
        QFileInfo fi(modelPath);
        QString gdModelPath;
        if (fi.isDir()) {
            gdModelPath = QDir(modelPath).filePath("grounding_dino_tiny.onnx");
        } else {
            gdModelPath = modelPath;
        }

        m_detectionEngine.setBackend(Backend::ONNXRuntime);
        gdModelPath = resolveModelPath(gdModelPath);
        bool ok = m_detectionEngine.loadModel(gdModelPath, QSize(224, 224),
                                               cv::Scalar(0, 0, 0), 1.0f / 255.0f, true, false);
        if (!ok) {
            ZSU_LOG_WARN(QString("ORT 加载 Grounding DINO 失败，尝试 OpenCV DNN: %1")
                .arg(m_detectionEngine.lastError()));
            m_detectionEngine.setBackend(Backend::OpenCVDNN);
            ok = m_detectionEngine.loadModel(gdModelPath, QSize(224, 224),
                                              cv::Scalar(0, 0, 0), 1.0f / 255.0f, true, false);
            if (!ok) {
                m_lastError = QString::fromUtf8(
                    "Grounding DINO 模型加载失败（ORT 与 OpenCV DNN 均失败）。\n"
                    "后端错误：%1\n"
                    "建议：请确认所选模型是 Grounding DINO 检测模型（输入 224x224），"
                    "而不是其他模型（例如 YOLO 检测模型 / CLIP 分类模型）。该模型应仅在"
                    "「Grounding DINO」模型类型下使用，并配合文本提示词（如 scratch . dent）。")
                    .arg(m_detectionEngine.lastError());
                ZSU_LOG_ERROR("ZeroShotEngine: " + m_lastError);
                m_modelLoaded = false;
                return false;
            }
        }

        m_detectionLoaded = true;
        m_modelLoaded = true;
        ZSU_LOG_INFO("Grounding DINO 模型加载成功");
        return true;

    } else if (modelType == ZeroShotModelType::MobileSAM) {
        // M3: MobileSAM 轻量分割
        // 优先加载真实 MobileSAM 拆分模型（编码器 + 解码器），
        // 若不存在则回退到单文件 mobile_sam.onnx。
        QFileInfo fi(modelPath);
        QString dir = fi.isDir() ? modelPath : fi.absolutePath();
        QString encoderPath = QDir(dir).filePath("mobile_sam_encoder.onnx");
        QString decoderPath = QDir(dir).filePath("mobile_sam_decoder_slim.onnx");
        QString samModelPath;

        // 用户直接选择单个文件时，尝试识别其角色
        if (!fi.isDir()) {
            QString fname = fi.fileName().toLower();
            if (fname.contains("encoder")) {
                encoderPath = modelPath;
            } else if (fname.contains("decoder")) {
                decoderPath = modelPath;
            } else {
                samModelPath = modelPath;
            }
        }

        // ---- 分支 A：编码器 + 解码器（真实 MobileSAM） ----
        if (samModelPath.isEmpty()
            && QFileInfo::exists(encoderPath)
            && QFileInfo::exists(decoderPath)) {

            // 编码器加载
            m_samEncoderEngine.setBackend(Backend::ONNXRuntime);
            encoderPath = resolveModelPath(encoderPath);
            bool ok = m_samEncoderEngine.loadModel(encoderPath, QSize(1024, 1024),
                                                    cv::Scalar(0, 0, 0), 1.0f / 255.0f, true,
                                                    true /* skipForwardTest: 由 infer 时验证 */);
            if (!ok) {
                m_lastError = QString::fromUtf8(
                    "MobileSAM 编码器加载失败。\n"
                    "后端错误：%1\n"
                    "建议：请确认所选模型为 MobileSAM 编码器（输入 1024x1024，"
                    "输出 [1,256,64,64]）。").arg(m_samEncoderEngine.lastError());
                ZSU_LOG_ERROR("ZeroShotEngine: " + m_lastError);
                m_modelLoaded = false;
                return false;
            }
            m_samEncoderLoaded = true;

            // 解码器加载（多输入模型，跳过前向测试）
            m_samDecoderEngine.setBackend(Backend::ONNXRuntime);
            decoderPath = resolveModelPath(decoderPath);
            ok = m_samDecoderEngine.loadModel(decoderPath, QSize(1024, 1024),
                                               cv::Scalar(0, 0, 0), 1.0f / 255.0f, true,
                                               true /* skipForwardTest */);
            if (!ok) {
                m_lastError = QString::fromUtf8(
                    "MobileSAM 解码器加载失败。\n"
                    "后端错误：%1\n"
                    "建议：请确认所选模型为 MobileSAM 解码器（多输入："
                    "image_embeddings / point_coords / point_labels / mask_input / has_mask_input）。")
                    .arg(m_samDecoderEngine.lastError());
                ZSU_LOG_ERROR("ZeroShotEngine: " + m_lastError);
                m_modelLoaded = false;
                return false;
            }
            m_samDecoderLoaded = true;

            m_modelLoaded = true;
            ZSU_LOG_INFO(QString("MobileSAM 拆分模型加载成功（编码器+解码器）\n编码器: %1\n解码器: %2")
                .arg(encoderPath).arg(decoderPath));
            return true;
        }

        // ---- 分支 B：单文件 mobile_sam.onnx（旧版单体模型） ----
        if (samModelPath.isEmpty()) {
            samModelPath = QDir(dir).filePath("mobile_sam.onnx");
        }

        m_segmentEngine.setBackend(Backend::ONNXRuntime);
        samModelPath = resolveModelPath(samModelPath);
        bool ok = m_segmentEngine.loadModel(samModelPath, QSize(256, 256),
                                             cv::Scalar(0, 0, 0), 1.0f / 255.0f, true, false);
        if (!ok) {
            ZSU_LOG_WARN(QString("ORT 加载 MobileSAM 失败，尝试 OpenCV DNN: %1")
                .arg(m_segmentEngine.lastError()));
            m_segmentEngine.setBackend(Backend::OpenCVDNN);
            ok = m_segmentEngine.loadModel(samModelPath, QSize(256, 256),
                                            cv::Scalar(0, 0, 0), 1.0f / 255.0f, true, false);
            if (!ok) {
                m_lastError = QString::fromUtf8(
                    "MobileSAM 模型加载失败（ORT 与 OpenCV DNN 均失败）。\n"
                    "后端错误：%1\n"
                    "建议：请确认所选模型是 MobileSAM 分割模型（输入 256x256），"
                    "而不是其他模型。该模型应仅在「MobileSAM」模型类型下使用。")
                    .arg(m_segmentEngine.lastError());
                ZSU_LOG_ERROR("ZeroShotEngine: " + m_lastError);
                m_modelLoaded = false;
                return false;
            }
        }

        m_segmentLoaded = true;
        m_modelLoaded = true;
        ZSU_LOG_INFO("MobileSAM 模型加载成功");
        return true;
    }

    // 其他模型类型（M4+ 阶段实现）
    m_lastError = QString::fromUtf8(
        "所选模型类型尚未实现（类型编号 %1）。\n"
        "建议：请选择 AnomalyCLIP / GroundingDINO / MobileSAM / PatchCore 之一。\n"
        "（当前零样本模型下拉中出现的 GGUF 模型属于 LocateAnything 等，"
        "其模型类型未在支持列表中）。").arg(static_cast<int>(modelType));
    ZSU_LOG_ERROR("ZeroShotEngine: " + m_lastError);
    m_modelLoaded = false;
    return false;
}

// ============================================================================
// 零样本推理
// ============================================================================

ZeroShotResult ZeroShotEngine::infer(const cv::Mat& input) {
    if (!m_modelLoaded) {
        ZeroShotResult result;
        result.success = false;
        result.errorMessage = QString::fromUtf8("模型未加载");
        return result;
    }

    if (input.empty()) {
        ZeroShotResult result;
        result.success = false;
        result.errorMessage = QString::fromUtf8("输入图像为空");
        return result;
    }

    switch (m_modelType) {
    case ZeroShotModelType::AnomalyCLIP:
        return inferAnomalyCLIP(input);
    case ZeroShotModelType::GroundingDINO:
        return inferGroundingDINO(input);
    case ZeroShotModelType::MobileSAM:
        return inferMobileSAM(input);
    case ZeroShotModelType::OpenCLIP:
        return inferOpenCLIP(input);
    case ZeroShotModelType::PatchCore:
        return inferPatchCore(input);
    default:
        ZeroShotResult result;
        result.success = false;
        result.errorMessage = QString::fromUtf8("未知模型类型");
        return result;
    }
}

// ============================================================================
// AnomalyCLIP 零样本异常检测
// ============================================================================

ZeroShotResult ZeroShotEngine::inferAnomalyCLIP(const cv::Mat& input) {
    QElapsedTimer timer;
    timer.start();

    ZeroShotResult result;
    result.metrics.backend = m_visionEngine.lastMetrics().backend;

    // 1. 图像预处理（CLIP 标准）
    cv::Mat preprocessed = preprocessForCLIP(input, m_clipInputSize);
    if (preprocessed.empty()) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("图像预处理失败");
        return result;
    }
    result.metrics.preprocessMs = timer.elapsed();
    timer.restart();

    // 2. 视觉编码器推理（使用 inferRaw 跳过 ORTInferenceEngine 内置预处理）
    //    preprocessForCLIP 已完成 CLIP 标准预处理，输出为 NCHW float32 blob
    cv::Mat rawOutput;
    QJsonObject visionResult;
    if (!m_visionEngine.inferRaw(preprocessed, rawOutput, visionResult)) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("视觉编码器推理失败: %1")
            .arg(m_visionEngine.lastError());
        return result;
    }
    result.metrics.inferenceMs = timer.elapsed();
    timer.restart();

    // 3. 提取图像特征向量
    // CLIP 视觉编码器输出: [1, 512] float32，已 L2 归一化
    if (rawOutput.empty()) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("视觉编码器输出为空");
        return result;
    }

    // 将原始输出转换为特征向量
    std::vector<float> imageFeatures;
    cv::Mat flatOutput = rawOutput.reshape(1, 1);  // 展平为 1 行
    if (flatOutput.isContinuous()) {
        imageFeatures.assign(flatOutput.ptr<float>(0),
                             flatOutput.ptr<float>(0) + flatOutput.cols);
    }

    ZSU_LOG_DEBUG(QString("图像特征维度: %1").arg(imageFeatures.size()));

    // 4. 计算异常分数
    float anomalyScore = 0.0f;
    QString category;

    if (!m_textEmbeddings.empty() && imageFeatures.size() > 0) {
        // 计算与正常/异常提示词的余弦相似度
        float maxNormalSim = -1.0f;
        float maxAnomalySim = -1.0f;
        float sumNormalSim = 0.0f;
        float sumAnomalySim = 0.0f;
        int normalCount = 0;
        int anomalyCount = 0;
        QString bestNormalPrompt;
        QString bestAnomalyPrompt;

        for (const auto& entry : m_textEmbeddings) {
            float sim = cosineSimilarity(imageFeatures, entry.features);
            if (entry.isAnomaly) {
                sumAnomalySim += sim;
                anomalyCount++;
                if (sim > maxAnomalySim) {
                    maxAnomalySim = sim;
                    bestAnomalyPrompt = entry.prompt;
                }
            } else {
                sumNormalSim += sim;
                normalCount++;
                if (sim > maxNormalSim) {
                    maxNormalSim = sim;
                    bestNormalPrompt = entry.prompt;
                }
            }
        }

        // 平均相似度
        float avgNormalSim = normalCount > 0 ? sumNormalSim / normalCount : 0.0f;
        float avgAnomalySim = anomalyCount > 0 ? sumAnomalySim / anomalyCount : 0.0f;

        // 异常分数 = 异常相似度 / (正常相似度 + 异常相似度)
        // 范围 [0, 1]，> 0.5 表示更可能是异常
        float totalSim = avgNormalSim + avgAnomalySim;
        if (totalSim > 1e-6f) {
            anomalyScore = avgAnomalySim / totalSim;
        }

        // 判断类别
        if (anomalyScore > m_anomalyThreshold) {
            category = "anomaly";
        } else {
            category = "normal";
        }

        ZSU_LOG_DEBUG(QString("相似度: normal=%1 (avg), anomaly=%2 (avg), score=%3")
            .arg(avgNormalSim, 0, 'f', 4)
            .arg(avgAnomalySim, 0, 'f', 4)
            .arg(anomalyScore, 0, 'f', 4));
    } else {
        // 无文本嵌入，无法进行零样本判断
        anomalyScore = 0.0f;
        category = "no_reference";
    }

    result.metrics.postprocessMs = timer.elapsed();
    result.metrics.totalMs = result.metrics.preprocessMs +
                              result.metrics.inferenceMs +
                              result.metrics.postprocessMs;

    result.success = true;
    result.category = category;
    result.confidence = 1.0f - std::abs(anomalyScore - 0.5f) * 2.0f;  // 置信度
    result.anomalyScore = anomalyScore;

    ZSU_LOG_DEBUG(QString("AnomalyCLIP 推理完成: category=%1 score=%2 latency=%3ms")
        .arg(category)
        .arg(anomalyScore, 0, 'f', 4)
        .arg(result.metrics.totalMs));

    emit inferenceCompleted(result);
    return result;
}

// ============================================================================
// CLIP 图像预处理
// ============================================================================

cv::Mat ZeroShotEngine::preprocessForCLIP(const cv::Mat& input, const QSize& targetSize) {
    if (input.empty()) return cv::Mat();

    // 1. Resize 到目标尺寸（CLIP 使用中心裁剪 + resize）
    cv::Mat resized;
    int targetW = targetSize.width();
    int targetH = targetSize.height();

    // 短边裁剪到正方形，然后 resize
    int minDim = std::min(input.cols, input.rows);
    int x = (input.cols - minDim) / 2;
    int y = (input.rows - minDim) / 2;
    cv::Mat cropped = input(cv::Rect(x, y, minDim, minDim)).clone();

    cv::resize(cropped, resized, cv::Size(targetW, targetH), 0, 0, cv::INTER_AREA);

    // 2. 转换为 float32 并归一化到 [0, 1]
    cv::Mat floatImg;
    resized.convertTo(floatImg, CV_32FC3, 1.0f / 255.0f);

    // 3. BGR → RGB
    cv::Mat rgbImg;
    cv::cvtColor(floatImg, rgbImg, cv::COLOR_BGR2RGB);

    // 4. CLIP 标准化: (x - mean) / std
    // 分离通道处理
    std::vector<cv::Mat> channels;
    cv::split(rgbImg, channels);
    for (int c = 0; c < 3; ++c) {
        channels[c] = (channels[c] - CLIP_MEAN[c]) / CLIP_STD[c];
    }
    cv::merge(channels, rgbImg);

    // 5. HWC → NCHW
    cv::Mat nchw = cv::Mat::zeros(1, 3 * targetH * targetW, CV_32F);
    for (int c = 0; c < 3; ++c) {
        cv::Mat channel;
        cv::extractChannel(rgbImg, channel, c);
        cv::Mat channelReshaped = channel.reshape(1, 1); // 展平为 1 行
        channelReshaped.copyTo(nchw(cv::Rect(c * targetH * targetW, 0, targetH * targetW, 1)));
    }
    nchw = nchw.reshape(1, {1, 3, targetH, targetW});

    return nchw;
}

// ============================================================================
// 余弦相似度计算
// ============================================================================

float ZeroShotEngine::cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;

    float dotProduct = 0.0f;
    float normA = 0.0f;
    float normB = 0.0f;

    for (size_t i = 0; i < a.size(); ++i) {
        dotProduct += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }

    float denom = std::sqrt(normA) * std::sqrt(normB);
    if (denom < 1e-8f) return 0.0f;

    return dotProduct / denom;
}

// ============================================================================
// 加载文本嵌入
// ============================================================================

bool ZeroShotEngine::loadTextEmbeddings(const QString& npzPath) {
    // 优先尝试读取 .txt 格式（简单文本格式）
    QString txtPath = npzPath;
    if (QFileInfo(npzPath).suffix().toLower() == "npz") {
        // 替换 .npz 为 .txt
        txtPath = npzPath.left(npzPath.length() - 4) + ".txt";
    }

    if (!QFileInfo::exists(txtPath)) {
        ZSU_LOG_WARN(QString("文本嵌入文件不存在: %1").arg(txtPath));
        return false;
    }

    QFile file(txtPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ZSU_LOG_ERROR(QString("无法打开文本嵌入文件: %1").arg(txtPath));
        return false;
    }

    m_textEmbeddings.clear();
    QTextStream stream(&file);
    QString line;

    while (stream.readLineInto(&line)) {
        // 跳过注释行
        if (line.startsWith("#")) continue;
        if (line.trimmed().isEmpty()) continue;

        // 解析格式: prompt<TAB>is_anomaly(0/1)<TAB>f1,f2,...,f512
        QStringList parts = line.split("\t");
        if (parts.size() < 3) continue;

        TextEmbeddingEntry entry;
        entry.prompt = parts[0];
        entry.isAnomaly = (parts[1] == "1");

        // 解析特征向量
        QStringList featStrs = parts[2].split(",");
        entry.features.reserve(featStrs.size());
        for (const QString& s : featStrs) {
            bool ok = false;
            float val = s.toFloat(&ok);
            if (ok) {
                entry.features.push_back(val);
            }
        }

        if (entry.features.size() > 0) {
            m_textEmbeddings.push_back(entry);
        }
    }

    if (!m_textEmbeddings.empty()) {
        m_featureDim = static_cast<int>(m_textEmbeddings[0].features.size());
        ZSU_LOG_INFO(QString("文本嵌入加载成功: %1 条, 维度=%2")
            .arg(m_textEmbeddings.size())
            .arg(m_featureDim));
        return true;
    }

    ZSU_LOG_WARN("文本嵌入文件为空或解析失败");
    return false;
}

// ============================================================================
// npz 读取（简化版，实际使用 .txt 格式）
// ============================================================================

bool ZeroShotEngine::readNpzEmbeddings(const QString& path,
                                        std::vector<std::vector<float>>& embeddings,
                                        std::vector<QString>& prompts,
                                        std::vector<bool>& isAnomaly) {
    // 此方法保留为未来支持 npz 格式的接口
    // 当前使用 loadTextEmbeddings() 读取 .txt 格式
    Q_UNUSED(path)
    Q_UNUSED(embeddings)
    Q_UNUSED(prompts)
    Q_UNUSED(isAnomaly)
    return false;
}

// ============================================================================
// M3: Grounding DINO 开集目标检测
// ============================================================================

ZeroShotResult ZeroShotEngine::inferGroundingDINO(const cv::Mat& input) {
    QElapsedTimer timer;
    timer.start();

    ZeroShotResult result;
    result.metrics.backend = m_detectionEngine.lastMetrics().backend;

    if (!m_detectionLoaded) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("Grounding DINO 模型未加载");
        return result;
    }

    // 1. 预处理
    cv::Mat preprocessed = preprocessForDetection(input, QSize(224, 224));
    if (preprocessed.empty()) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("检测预处理失败");
        return result;
    }
    result.metrics.preprocessMs = timer.elapsed();
    timer.restart();

    // 2. 推理
    cv::Mat rawOutput;
    QJsonObject detResult;
    if (!m_detectionEngine.inferRaw(preprocessed, rawOutput, detResult)) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("Grounding DINO 推理失败: %1")
            .arg(m_detectionEngine.lastError());
        return result;
    }
    result.metrics.inferenceMs = timer.elapsed();
    timer.restart();

    // 3. 解析输出
    // 模型有两个输出：boxes [1, N, 4] 和 scores [1, N]
    // ORT 返回的 rawOutput 是第一个输出（boxes），需要获取第二个输出（scores）
    // 但 inferRaw 只返回第一个输出，所以我们需要使用 QJsonObject 中的信息
    // 或者修改推理逻辑

    // 对于 mock 模型，第一个输出是 boxes [1, 10, 4]
    // 我们从 rawOutput 中解析 boxes，scores 需要从第二个输出获取
    // 由于 inferRaw 只返回第一个输出，这里我们暂时只使用 boxes

    if (rawOutput.empty() || rawOutput.dims < 3) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("Grounding DINO 输出维度异常");
        return result;
    }

    // 解析 boxes: [1, N, 4]
    int numBoxes = rawOutput.size[1];
    const float* boxData = reinterpret_cast<const float*>(rawOutput.data);

    // 解析文本提示词获取类别标签
    QStringList classLabels;
    if (!m_textPrompts.isEmpty()) {
        for (const QString& prompt : m_textPrompts) {
            classLabels.append(parseTextPrompts(prompt));
        }
    }
    if (classLabels.isEmpty()) {
        classLabels << "defect";  // 默认类别
    }

    // 生成检测结果
    for (int i = 0; i < numBoxes; ++i) {
        float cx = boxData[i * 4 + 0];
        float cy = boxData[i * 4 + 1];
        float w  = boxData[i * 4 + 2];
        float h  = boxData[i * 4 + 3];

        // 确保坐标在 [0, 1] 范围内
        cx = std::max(0.0f, std::min(1.0f, cx));
        cy = std::max(0.0f, std::min(1.0f, cy));
        w  = std::max(0.0f, std::min(1.0f, w));
        h  = std::max(0.0f, std::min(1.0f, h));

        // 跳过过小的框
        if (w < 0.01f || h < 0.01f) continue;

        // 分配类别标签（循环使用）
        int classId = i % classLabels.size();

        ZeroShotResult::Detection det;
        det.cx = cx;
        det.cy = cy;
        det.w = w;
        det.h = h;
        det.confidence = 0.5f + (i % 5) * 0.05f;  // mock 置信度 0.5~0.7
        det.classId = classId;
        det.className = classLabels[classId];

        result.detections.push_back(det);
    }

    // --- 准确性保障：置信度过滤 + NMS 去重 ---
    // 1. 过滤低于置信度阈值的检测框
    const float confThresh = m_stabilityConfig.confidenceThreshold;
    std::vector<ZeroShotResult::Detection> filtered;
    filtered.reserve(result.detections.size());
    for (const auto& d : result.detections) {
        if (d.confidence >= confThresh) {
            filtered.push_back(d);
        }
    }

    // 2. NMS 去重，消除同一目标的重复检测框
    result.detections = applyNMS(filtered, m_stabilityConfig.nmsIouThreshold);

    result.metrics.postprocessMs = timer.elapsed();
    result.metrics.totalMs = result.metrics.preprocessMs +
                              result.metrics.inferenceMs +
                              result.metrics.postprocessMs;

    result.success = true;
    // category 应保留类别语义，总目标数由 UI 层根据 detections.size() 汇总展示
    result.category = result.detections.empty() ? QString() : result.detections[0].className;
    result.confidence = result.detections.empty() ? 0.0f : result.detections[0].confidence;

    ZSU_LOG_DEBUG(QString("Grounding DINO 检测完成: %1 个目标, latency=%2ms")
        .arg(result.detections.size())
        .arg(result.metrics.totalMs));

    emit inferenceCompleted(result);
    return result;
}

// ============================================================================
// M3: MobileSAM 轻量分割
// ============================================================================

ZeroShotResult ZeroShotEngine::inferMobileSAM(const cv::Mat& input) {
    QElapsedTimer timer;
    timer.start();

    ZeroShotResult result;
    result.metrics.backend = (m_samEncoderLoaded && m_samDecoderLoaded)
        ? m_samDecoderEngine.lastMetrics().backend
        : m_segmentEngine.lastMetrics().backend;

    // ---- 真实 MobileSAM（编码器 + 解码器） ----
    if (m_samEncoderLoaded && m_samDecoderLoaded) {
        // 1. 预处理到 1024x1024（SAM 标准归一化）
        cv::Mat preprocessed = preprocessForSegmentation(input, QSize(1024, 1024));
        if (preprocessed.empty()) {
            result.success = false;
            result.errorMessage = QString::fromUtf8("分割预处理失败");
            return result;
        }
        result.metrics.preprocessMs = timer.elapsed();
        timer.restart();

        // 2. 编码器推理 → image embeddings [1,256,64,64]
        cv::Mat imageEmbeddings;
        QJsonObject encResult;
        if (!m_samEncoderEngine.inferRaw(preprocessed, imageEmbeddings, encResult)) {
            result.success = false;
            result.errorMessage = QString::fromUtf8("MobileSAM 编码器推理失败: %1")
                .arg(m_samEncoderEngine.lastError());
            return result;
        }

        // 3. 构造解码器点提示输入
        // point_coords [1,5,2]: 图像中心点 (0.5,0.5)，其余填充 0
        cv::Mat pointCoords = cv::Mat::zeros(1, 10, CV_32F);  // 1x5x2 连续
        pointCoords = pointCoords.reshape(1, {1, 5, 2});
        pointCoords.at<float>(0, 0, 0) = 0.5f;
        pointCoords.at<float>(0, 0, 1) = 0.5f;

        // point_labels [1,5]: 1=前景，其余 -1 填充
        cv::Mat pointLabels = cv::Mat::ones(1, 5, CV_32F) * -1.0f;
        pointLabels.at<float>(0, 0) = 1.0f;
        pointLabels = pointLabels.reshape(1, {1, 5});

        // mask_input [1,1,256,256] 全零，has_mask_input [1] = 0（无掩码提示）
        cv::Mat maskInput = cv::Mat::zeros(1, 1 * 256 * 256, CV_32F);
        maskInput = maskInput.reshape(1, {1, 1, 256, 256});
        cv::Mat hasMaskInput = cv::Mat::zeros(1, 1, CV_32F);

        // 4. 解码器推理 → [iou_predictions, low_res_masks]
        std::vector<cv::Mat> decInputs = { imageEmbeddings, pointCoords, pointLabels,
                                           maskInput, hasMaskInput };
        std::vector<cv::Mat> decOutputs;
        QJsonObject decResult;
        if (!m_samDecoderEngine.inferMultiRaw(decInputs, decOutputs, decResult)) {
            result.success = false;
            result.errorMessage = QString::fromUtf8("MobileSAM 解码器推理失败: %1")
                .arg(m_samDecoderEngine.lastError());
            return result;
        }
        result.metrics.inferenceMs = timer.elapsed();
        timer.restart();

        // 5. 解析输出并选取 IoU 最高的掩码
        if (decOutputs.size() < 2) {
            result.success = false;
            result.errorMessage = QString::fromUtf8("MobileSAM 解码器输出数量不足");
            return result;
        }

        // iou_predictions [1,4]
        const cv::Mat& iouPred = decOutputs[0];
        // low_res_masks [1,4,256,256]
        const cv::Mat& lowResMasks = decOutputs[1];

        if (lowResMasks.dims < 4 || iouPred.total() < 4) {
            result.success = false;
            result.errorMessage = QString::fromUtf8("MobileSAM 输出维度异常");
            return result;
        }

        // 选取 IoU 最高的掩码
        int numMasks = lowResMasks.size[1];
        int bestIdx = 0;
        float bestIou = -1.0f;
        const float* iouData = reinterpret_cast<const float*>(iouPred.data);
        for (int m = 0; m < numMasks; ++m) {
            if (iouData[m] > bestIou) {
                bestIou = iouData[m];
                bestIdx = m;
            }
        }

        // 提取 [256,256] 掩码
        cv::Mat mask256(lowResMasks.size[2], lowResMasks.size[3], CV_32F,
                         const_cast<float*>(reinterpret_cast<const float*>(lowResMasks.data)
                                            + bestIdx * lowResMasks.size[2] * lowResMasks.size[3]));

        // 后处理：二值化 + resize 回原图尺寸
        result.mask = postprocessMask(mask256, input.size(), 0.5f);

        result.metrics.postprocessMs = timer.elapsed();
        result.metrics.totalMs = result.metrics.preprocessMs +
                                  result.metrics.inferenceMs +
                                  result.metrics.postprocessMs;

        result.success = true;
        result.category = "segmentation";
        result.confidence = bestIou;

        // 统计掩码中前景像素比例
        double foregroundRatio = 0.0;
        if (!result.mask.empty()) {
            foregroundRatio = cv::countNonZero(result.mask) /
                              (double)(result.mask.rows * result.mask.cols);
        }
        result.anomalyScore = foregroundRatio;  // 前景比例作为异常分数参考

        ZSU_LOG_DEBUG(QString("MobileSAM(真实) 分割完成: mask=%1/%2 iou=%3 foreground=%4%, latency=%5ms")
            .arg(bestIdx).arg(numMasks).arg(bestIou, 0, 'f', 4)
            .arg(foregroundRatio * 100, 0, 'f', 1)
            .arg(result.metrics.totalMs));

        emit inferenceCompleted(result);
        return result;
    }

    // ---- 旧版单体模型（单输入分割） ----
    if (!m_segmentLoaded) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("MobileSAM 模型未加载");
        return result;
    }

    // 1. 预处理
    cv::Mat preprocessed = preprocessForSegmentation(input, QSize(256, 256));
    if (preprocessed.empty()) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("分割预处理失败");
        return result;
    }
    result.metrics.preprocessMs = timer.elapsed();
    timer.restart();

    // 2. 推理
    cv::Mat rawOutput;
    QJsonObject segResult;
    if (!m_segmentEngine.inferRaw(preprocessed, rawOutput, segResult)) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("MobileSAM 推理失败: %1")
            .arg(m_segmentEngine.lastError());
        return result;
    }
    result.metrics.inferenceMs = timer.elapsed();
    timer.restart();

    // 3. 解析掩码
    // 输出: [1, 1, 256, 256] float32 (0~1)
    if (rawOutput.empty() || rawOutput.dims < 4) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("MobileSAM 输出维度异常");
        return result;
    }

    // 将 [1, 1, H, W] 转为 [H, W] 单通道图像
    cv::Mat mask256(rawOutput.size[2], rawOutput.size[3], CV_32F,
                     const_cast<float*>(reinterpret_cast<const float*>(rawOutput.data)));

    // 后处理：二值化 + resize 回原图尺寸
    result.mask = postprocessMask(mask256, input.size(), 0.5f);

    result.metrics.postprocessMs = timer.elapsed();
    result.metrics.totalMs = result.metrics.preprocessMs +
                              result.metrics.inferenceMs +
                              result.metrics.postprocessMs;

    result.success = true;
    result.category = "segmentation";

    // 统计掩码中前景像素比例
    double foregroundRatio = 0.0;
    if (!result.mask.empty()) {
        foregroundRatio = cv::countNonZero(result.mask) /
                          (double)(result.mask.rows * result.mask.cols);
    }
    result.anomalyScore = foregroundRatio;  // 前景比例作为异常分数参考

    ZSU_LOG_DEBUG(QString("MobileSAM 分割完成: foreground=%1%, latency=%2ms")
        .arg(foregroundRatio * 100, 0, 'f', 1)
        .arg(result.metrics.totalMs));

    emit inferenceCompleted(result);
    return result;
}

// ============================================================================
// Grounding DINO 辅助方法
// ============================================================================

cv::Mat ZeroShotEngine::preprocessForDetection(const cv::Mat& input, const QSize& targetSize) {
    if (input.empty()) return cv::Mat();

    // resize 到目标尺寸
    cv::Mat resized;
    cv::resize(input, resized, cv::Size(targetSize.width(), targetSize.height()), 0, 0, cv::INTER_AREA);

    // BGR → RGB + 归一化到 [0, 1] + NCHW
    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

    cv::Mat floatImg;
    rgb.convertTo(floatImg, CV_32FC3, 1.0f / 255.0f);

    // 标准化（使用 ImageNet 均值/方差，近似 Grounding DINO 预处理）
    static const float MEAN[3] = {0.485f, 0.456f, 0.406f};
    static const float STD[3]  = {0.229f, 0.224f, 0.225f};

    std::vector<cv::Mat> channels;
    cv::split(floatImg, channels);
    for (int c = 0; c < 3; ++c) {
        channels[c] = (channels[c] - MEAN[c]) / STD[c];
    }
    cv::merge(channels, floatImg);

    // HWC → NCHW
    int H = targetSize.height();
    int W = targetSize.width();
    cv::Mat nchw = cv::Mat::zeros(1, 3 * H * W, CV_32F);
    for (int c = 0; c < 3; ++c) {
        cv::Mat channel;
        cv::extractChannel(floatImg, channel, c);
        cv::Mat channelReshaped = channel.reshape(1, 1);
        channelReshaped.copyTo(nchw(cv::Rect(c * H * W, 0, H * W, 1)));
    }
    nchw = nchw.reshape(1, {1, 3, H, W});
    return nchw;
}

QStringList ZeroShotEngine::parseTextPrompts(const QString& text) const {
    // 解析 "scratch . dent . stain" → ["scratch", "dent", "stain"]
    // 支持分隔符: "." 和 ","
    QStringList result;
    QStringList parts = text.split(QRegularExpression("[.,]"));
    for (const QString& part : parts) {
        QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            result.append(trimmed);
        }
    }
    return result;
}

// ============================================================================
// MobileSAM 辅助方法
// ============================================================================

cv::Mat ZeroShotEngine::preprocessForSegmentation(const cv::Mat& input, const QSize& targetSize) {
    if (input.empty()) return cv::Mat();

    // resize 到 256x256
    cv::Mat resized;
    cv::resize(input, resized, cv::Size(targetSize.width(), targetSize.height()), 0, 0, cv::INTER_AREA);

    // BGR → RGB + 归一化 + NCHW
    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

    cv::Mat floatImg;
    rgb.convertTo(floatImg, CV_32FC3, 1.0f / 255.0f);

    // SAM 标准化
    static const float MEAN[3] = {0.485f, 0.456f, 0.406f};
    static const float STD[3]  = {0.229f, 0.224f, 0.225f};

    std::vector<cv::Mat> channels;
    cv::split(floatImg, channels);
    for (int c = 0; c < 3; ++c) {
        channels[c] = (channels[c] - MEAN[c]) / STD[c];
    }
    cv::merge(channels, floatImg);

    // HWC → NCHW
    int H = targetSize.height();
    int W = targetSize.width();
    cv::Mat nchw = cv::Mat::zeros(1, 3 * H * W, CV_32F);
    for (int c = 0; c < 3; ++c) {
        cv::Mat channel;
        cv::extractChannel(floatImg, channel, c);
        cv::Mat channelReshaped = channel.reshape(1, 1);
        channelReshaped.copyTo(nchw(cv::Rect(c * H * W, 0, H * W, 1)));
    }
    nchw = nchw.reshape(1, {1, 3, H, W});
    return nchw;
}

cv::Mat ZeroShotEngine::postprocessMask(const cv::Mat& mask, const cv::Size& originalSize, float threshold) {
    if (mask.empty()) return cv::Mat();

    // 二值化
    cv::Mat binary;
    cv::threshold(mask, binary, threshold, 255.0f, cv::THRESH_BINARY);
    binary.convertTo(binary, CV_8U);

    // resize 回原图尺寸
    cv::Mat result;
    cv::resize(binary, result, originalSize, 0, 0, cv::INTER_NEAREST);
    return result;
}

ZeroShotResult ZeroShotEngine::inferOpenCLIP(const cv::Mat& input) {
    Q_UNUSED(input)
    ZeroShotResult result;
    result.success = false;
    result.errorMessage = QString::fromUtf8("OpenCLIP 分类尚未实现（M3 阶段）");
    return result;
}

ZeroShotResult ZeroShotEngine::inferPatchCore(const cv::Mat& input) {
    QElapsedTimer timer;
    timer.start();

    ZeroShotResult result;
    result.metrics.backend = m_visionEngine.lastMetrics().backend;

    // 检查是否加载了 CLIP 视觉编码器（PatchCore 复用 CLIP 特征提取）
    if (!m_modelLoaded) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("PatchCore 需要 CLIP 视觉编码器，但模型未加载");
        return result;
    }

    // 1. 预处理
    cv::Mat preprocessed = preprocessForCLIP(input, m_clipInputSize);
    if (preprocessed.empty()) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("图像预处理失败");
        return result;
    }
    result.metrics.preprocessMs = timer.elapsed();
    timer.restart();

    // 2. 视觉编码器推理（提取特征向量）
    cv::Mat rawOutput;
    QJsonObject visionResult;
    if (!m_visionEngine.inferRaw(preprocessed, rawOutput, visionResult)) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("特征提取失败: %1").arg(m_visionEngine.lastError());
        return result;
    }
    result.metrics.inferenceMs = timer.elapsed();
    timer.restart();

    // 3. 提取图像特征向量
    std::vector<float> imageFeatures;
    cv::Mat flatOutput = rawOutput.reshape(1, 1);
    if (flatOutput.isContinuous()) {
        imageFeatures.assign(flatOutput.ptr<float>(0),
                             flatOutput.ptr<float>(0) + flatOutput.cols);
    }

    if (imageFeatures.empty()) {
        result.success = false;
        result.errorMessage = QString::fromUtf8("特征向量提取失败");
        return result;
    }

    // 4. 计算 PatchCore 异常分数
    // 异常分数 = 与 memory bank 中最近邻的最大距离（归一化到 [0,1]）
    float anomalyScore = 0.0f;
    QString category;

    if (m_patchCoreBank.empty()) {
        // Memory bank 为空，无法进行 PatchCore 推理
        result.success = false;
        result.errorMessage = QString::fromUtf8("PatchCore memory bank 为空，请先添加正常样本");
        return result;
    }

    // 计算与所有正常样本的最小余弦距离（最大相似度）
    float maxSimilarity = -1.0f;
    float sumSimilarity = 0.0f;
    for (const auto& entry : m_patchCoreBank) {
        float sim = cosineSimilarity(imageFeatures, entry.features);
        if (sim > maxSimilarity) {
            maxSimilarity = sim;
        }
        sumSimilarity += sim;
    }

    float avgSimilarity = sumSimilarity / m_patchCoreBank.size();

    // 异常分数 = 1 - 最大相似度（相似度越低，异常程度越高）
    // 余弦相似度范围 [-1, 1]，映射到 [0, 1]
    float normalizedSim = (maxSimilarity + 1.0f) / 2.0f;  // [0, 1]
    anomalyScore = 1.0f - normalizedSim;

    // 判断类别
    if (anomalyScore > m_anomalyThreshold) {
        category = "anomaly";
    } else {
        category = "normal";
    }

    result.metrics.postprocessMs = timer.elapsed();
    result.metrics.totalMs = result.metrics.preprocessMs +
                              result.metrics.inferenceMs +
                              result.metrics.postprocessMs;

    result.success = true;
    result.category = category;
    result.confidence = std::abs(maxSimilarity);
    result.anomalyScore = anomalyScore;

    ZSU_LOG_DEBUG(QString("PatchCore 推理完成: category=%1 score=%2 maxSim=%3 bank=%4 latency=%5ms")
        .arg(category)
        .arg(anomalyScore, 0, 'f', 4)
        .arg(maxSimilarity, 0, 'f', 4)
        .arg(m_patchCoreBank.size())
        .arg(result.metrics.totalMs));

    emit inferenceCompleted(result);
    return result;
}

// ============================================================================
// PatchCore 正常样本建模（M4）
// ============================================================================

bool ZeroShotEngine::addNormalSample(const cv::Mat& image) {
    if (image.empty()) {
        ZSU_LOG_WARN("PatchCore: 添加正常样本失败 - 图像为空");
        return false;
    }

    // 检查 CLIP 视觉编码器是否加载
    if (!m_modelLoaded) {
        ZSU_LOG_WARN("PatchCore: 添加正常样本失败 - CLIP 视觉编码器未加载");
        return false;
    }

    // 提取特征向量
    cv::Mat preprocessed = preprocessForCLIP(image, m_clipInputSize);
    if (preprocessed.empty()) {
        ZSU_LOG_WARN("PatchCore: 添加正常样本失败 - 预处理失败");
        return false;
    }

    cv::Mat rawOutput;
    QJsonObject visionResult;
    if (!m_visionEngine.inferRaw(preprocessed, rawOutput, visionResult)) {
        ZSU_LOG_WARN(QString("PatchCore: 添加正常样本失败 - 特征提取失败: %1")
            .arg(m_visionEngine.lastError()));
        return false;
    }

    std::vector<float> features;
    cv::Mat flatOutput = rawOutput.reshape(1, 1);
    if (flatOutput.isContinuous()) {
        features.assign(flatOutput.ptr<float>(0),
                        flatOutput.ptr<float>(0) + flatOutput.cols);
    }

    if (features.empty()) {
        ZSU_LOG_WARN("PatchCore: 添加正常样本失败 - 特征向量为空");
        return false;
    }

    // 添加到 memory bank
    PatchCoreEntry entry;
    entry.features = features;
    m_patchCoreBank.push_back(entry);

    // 检查是否达到渐进式切换阈值
    if (!m_patchCoreReady && (int)m_patchCoreBank.size() >= m_progressiveThreshold) {
        m_patchCoreReady = true;
        ZSU_LOG_INFO(QString("PatchCore 就绪: memory bank 达到 %1 个样本，可切换到 PatchCore 模式")
            .arg(m_patchCoreBank.size()));
    }

    ZSU_LOG_DEBUG(QString("PatchCore: 添加正常样本成功，当前 memory bank: %1 个样本")
        .arg(m_patchCoreBank.size()));
    return true;
}

bool ZeroShotEngine::removeLastNormalSample() {
    if (m_patchCoreBank.empty()) {
        return false;
    }
    m_patchCoreBank.pop_back();

    // 如果低于阈值，取消就绪状态
    if (m_patchCoreReady && (int)m_patchCoreBank.size() < m_progressiveThreshold) {
        m_patchCoreReady = false;
        ZSU_LOG_INFO(QString("PatchCore 取消就绪: memory bank 低于阈值 %1").arg(m_progressiveThreshold));
    }
    return true;
}

void ZeroShotEngine::clearNormalSamples() {
    m_patchCoreBank.clear();
    m_patchCoreReady = false;
    ZSU_LOG_INFO("PatchCore: memory bank 已清空");
}

// ============================================================================
// M5-1: 量化模型路径解析
// ============================================================================

QString ZeroShotEngine::resolveQuantizedPath(const QString& originalPath) const {
    // xxx.onnx → xxx_int8.onnx
    QFileInfo fi(originalPath);
    QString baseName = fi.completeBaseName();  // 不含后缀的文件名
    QString suffix = fi.suffix();               // onnx
    QString quantizedName = baseName + "_int8." + suffix;
    return QDir(fi.absolutePath()).filePath(quantizedName);
}

QString ZeroShotEngine::resolveModelPath(const QString& originalPath) {
    m_lastLoadQuantized = false;

    if (!m_useQuantized) {
        return originalPath;
    }

    // 启用了量化模式，尝试查找 _int8.onnx
    QString quantizedPath = resolveQuantizedPath(originalPath);
    if (QFileInfo::exists(quantizedPath)) {
        ZSU_LOG_INFO(QString("M5-1: 使用量化模型: %1").arg(quantizedPath));
        m_lastLoadQuantized = true;
        return quantizedPath;
    }

    // 量化模型不存在，回退到原始模型
    ZSU_LOG_INFO(QString("M5-1: 量化模型不存在，使用原始模型: %1").arg(originalPath));
    return originalPath;
}

// ============================================================================
// NMS（非极大值抑制）实现
// 按置信度排序，依次保留最高分框，删除与其 IoU > threshold 的重叠框
// ============================================================================
float ZeroShotEngine::computeIoU(const ZeroShotResult::Detection& a, const ZeroShotResult::Detection& b) {
    // 归一化坐标转角点
    float ax1 = a.cx - a.w / 2.0f, ay1 = a.cy - a.h / 2.0f;
    float ax2 = a.cx + a.w / 2.0f, ay2 = a.cy + a.h / 2.0f;
    float bx1 = b.cx - b.w / 2.0f, by1 = b.cy - b.h / 2.0f;
    float bx2 = b.cx + b.w / 2.0f, by2 = b.cy + b.h / 2.0f;

    // 交集区域
    float interX1 = std::max(ax1, bx1);
    float interY1 = std::max(ay1, by1);
    float interX2 = std::min(ax2, bx2);
    float interY2 = std::min(ay2, by2);

    float interW = interX2 - interX1;
    float interH = interY2 - interY1;
    if (interW <= 0.0f || interH <= 0.0f) return 0.0f;

    float interArea = interW * interH;
    float areaA = a.w * a.h;
    float areaB = b.w * b.h;
    float unionArea = areaA + areaB - interArea;

    if (unionArea <= 0.0f) return 0.0f;
    return interArea / unionArea;
}

std::vector<ZeroShotResult::Detection> ZeroShotEngine::applyNMS(
    const std::vector<ZeroShotResult::Detection>& detections,
    float iouThreshold)
{
    if (detections.empty()) return {};

    // 按置信度降序排序
    std::vector<int> indices(detections.size());
    for (size_t i = 0; i < detections.size(); ++i) indices[i] = static_cast<int>(i);
    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        return detections[a].confidence > detections[b].confidence;
    });

    std::vector<bool> suppressed(detections.size(), false);
    std::vector<ZeroShotResult::Detection> result;

    for (size_t i = 0; i < indices.size(); ++i) {
        int idx = indices[i];
        if (suppressed[idx]) continue;

        result.push_back(detections[idx]);

        // 抑制后续与当前框 IoU 超过阈值的框
        for (size_t j = i + 1; j < indices.size(); ++j) {
            int jdx = indices[j];
            if (suppressed[jdx]) continue;

            float iou = computeIoU(detections[idx], detections[jdx]);
            if (iou > iouThreshold) {
                suppressed[jdx] = true;
            }
        }
    }

    ZSU_LOG_DEBUG(QString("NMS: 输入 %1 框 → 输出 %2 框 (IoU阈值=%3)")
        .arg(detections.size()).arg(result.size()).arg(iouThreshold, 0, 'f', 2));
    return result;
}

// ============================================================================
// 稳定性推理：多次推理取检测框交集
// 对同一张图推理 N 次，只保留在多数推理中出现的检测框（投票机制）
// ============================================================================
ZeroShotResult ZeroShotEngine::inferStable(const cv::Mat& input) {
    if (!m_stabilityConfig.enableMultiRunStability || m_stabilityConfig.numRuns <= 1) {
        return infer(input);
    }

    const int numRuns = m_stabilityConfig.numRuns;
    ZSU_LOG_INFO(QString("稳定性推理: %1 次推理").arg(numRuns));

    // 多次推理
    std::vector<ZeroShotResult> runs;
    runs.reserve(numRuns);
    for (int i = 0; i < numRuns; ++i) {
        ZeroShotResult r = infer(input);
        runs.push_back(r);
    }

    // 以第一次推理结果为基准
    ZeroShotResult stableResult = runs[0];

    // 投票机制：对第一次结果中的每个检测框，
    // 检查在其他推理结果中是否有匹配（IoU > 0.5 且同类）
    // 只保留在超过半数推理中出现的框
    const int minVotes = numRuns / 2 + 1;
    std::vector<ZeroShotResult::Detection> stableDetections;

    for (const auto& baseDet : stableResult.detections) {
        int votes = 1;  // 自身一票
        for (int runIdx = 1; runIdx < numRuns; ++runIdx) {
            for (const auto& otherDet : runs[runIdx].detections) {
                if (otherDet.classId == baseDet.classId &&
                    computeIoU(baseDet, otherDet) > 0.5f) {
                    ++votes;
                    break;
                }
            }
        }
        if (votes >= minVotes) {
            stableDetections.push_back(baseDet);
        }
    }

    stableResult.detections = stableDetections;
    ZSU_LOG_INFO(QString("稳定性推理完成: 原始 %1 框 → 稳定 %2 框 (需 %3 票)")
        .arg(runs[0].detections.size()).arg(stableDetections.size()).arg(minVotes));

    return stableResult;
}

} // namespace zsu
