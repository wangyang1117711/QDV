#include "Vision/AiClassifyTool.h"
#include "Core/Logger.h"
#include <QJsonArray>

using namespace QDV;

AiClassifyTool::AiClassifyTool() {
    m_name = "AI分类";
    // Phase 1: m_engine 由 setInferenceEngine 外部注入，不再 new InferenceEngine
}

AiClassifyTool::~AiClassifyTool() {
    // Phase 1: m_engine 所有权归外部（注入方），不 delete
}

bool AiClassifyTool::configure(const QJsonObject& params) {
    if (params.contains("modelPath")) {
        setModelPath(params["modelPath"].toString());
    }
    if (params.contains("confidenceThreshold")) {
        setConfidenceThreshold(params["confidenceThreshold"].toDouble(0.5));
    }
    if (params.contains("topK")) {
        setTopK(params["topK"].toInt(3));
    }
    if (params.contains("inputWidth") && params.contains("inputHeight")) {
        setInputSize(params["inputWidth"].toInt(224), params["inputHeight"].toInt(224));
    }
    if (params.contains("categoryLabels")) {
        QJsonArray arr = params["categoryLabels"].toArray();
        QStringList labels;
        for (const auto& v : arr) {
            labels.append(v.toString());
        }
        setCategoryLabels(labels);
    }
    m_params = params;

    // Phase 1: 增加 m_engine nullptr 检查（RT-006）
    if (!m_modelPath.isEmpty() && !m_warmedUp && m_engine) {
        Logger::info("AiClassifyTool loading model: " + m_modelPath);
        bool loaded = m_engine->loadModel(m_modelPath,
            QSize(m_inputWidth, m_inputHeight),
            cv::Scalar(0.485, 0.456, 0.406),
            1.0 / 255.0,
            true);
        if (loaded) {
            m_engine->warmUp(3);
            m_warmedUp = true;
            Logger::info("AiClassifyTool model warmed up successfully");
        } else {
            Logger::error("AiClassifyTool failed to load model: " + m_modelPath);
            return false;
        }
    }

    return true;
}

bool AiClassifyTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        Logger::warn("AiClassifyTool: empty input");
        result.ok = false;
        return false;
    }

    // Phase 1 RT-006: 引擎未注入保护
    if (!m_engine) {
        Logger::warn("AiClassifyTool: no inference engine injected");
        result.ok = false;
        result.data["error"] = "No inference engine injected";
        result.data["modelLoaded"] = false;  // v5.3：供 ToolChainVerifier 判断预期跳过
        return false;
    }

    if (m_modelPath.isEmpty()) {
        Logger::warn("AiClassifyTool: no model configured");
        result.ok = false;
        result.data["error"] = "No model configured";
        result.data["modelLoaded"] = false;
        return false;
    }

    if (!m_warmedUp && m_engine) {
        Logger::info("AiClassifyTool: loading model before first inference");
        bool loaded = m_engine->loadModel(m_modelPath,
            QSize(m_inputWidth, m_inputHeight),
            cv::Scalar(0.485, 0.456, 0.406),
            1.0 / 255.0,
            true);
        if (loaded) {
            m_engine->warmUp(3);
            m_warmedUp = true;
        } else {
            result.ok = false;
            result.data["error"] = "Failed to load model";
            result.data["modelLoaded"] = false;
            return false;
        }
    }
    result.data["modelLoaded"] = true;  // v5.3：模型已加载标记

    QJsonObject inferResult;
    bool ok = m_engine->infer(input, inferResult);

    if (!ok) {
        Logger::error("AiClassifyTool: inference failed");
        result.ok = false;
        result.data["error"] = "Inference failed";
        return false;
    }

    double confidence = inferResult["confidence"].toDouble();
    result.score = confidence;
    result.ok = (confidence >= m_confidenceThreshold);

    result.data = inferResult;

    int classIndex = inferResult["class_index"].toInt();
    if (classIndex >= 0 && classIndex < m_categoryLabels.size()) {
        result.data["category_name"] = m_categoryLabels[classIndex];
    } else {
        result.data["category_name"] = inferResult["category"].toString();
    }

    // Phase 1: InferenceMetrics 改用 IInferenceEngine 的 InferenceMetricsLite
    InferenceMetricsLite metrics = m_engine->lastMetrics();
    result.elapsedMs = metrics.totalMs;
    result.data["preprocess_ms"] = metrics.preprocessMs;
    result.data["inference_ms"] = metrics.inferenceMs;
    result.data["postprocess_ms"] = metrics.postprocessMs;
    result.data["backend"] = metrics.backend;
    result.data["confidence_threshold"] = m_confidenceThreshold;
    result.data["pass"] = result.ok;

    m_results["lastClassification"] = result.data["category_name"].toString();
    m_results["lastConfidence"] = confidence;
    m_results["lastPass"] = result.ok;

    return true;
}

QJsonObject AiClassifyTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["modelPath"] = m_modelPath;
    obj["confidenceThreshold"] = m_confidenceThreshold;
    obj["topK"] = m_topK;
    obj["inputWidth"] = m_inputWidth;
    obj["inputHeight"] = m_inputHeight;
    QJsonArray labels;
    for (const auto& label : m_categoryLabels) {
        labels.append(label);
    }
    obj["categoryLabels"] = labels;
    return obj;
}

bool AiClassifyTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("modelPath")) m_modelPath = data["modelPath"].toString();
    if (data.contains("confidenceThreshold")) m_confidenceThreshold = data["confidenceThreshold"].toDouble(0.5);
    if (data.contains("topK")) m_topK = data["topK"].toInt(3);
    if (data.contains("inputWidth")) m_inputWidth = data["inputWidth"].toInt(224);
    if (data.contains("inputHeight")) m_inputHeight = data["inputHeight"].toInt(224);
    if (data.contains("categoryLabels")) {
        QJsonArray arr = data["categoryLabels"].toArray();
        m_categoryLabels.clear();
        for (const auto& v : arr) {
            m_categoryLabels.append(v.toString());
        }
    }

    return configure(data);
}