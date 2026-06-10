#include "AI/VisionClassifier.h"
#include "Core/Logger.h"
#include <chrono>
#include <algorithm>
#include <QJsonArray>

namespace QDV {

VisionClassifier::VisionClassifier() 
    : m_engine(std::make_unique<InferenceEngine>()) {
}

VisionClassifier::~VisionClassifier() {
}

bool VisionClassifier::loadModel(const std::string& onnxPath, const std::vector<std::string>& labels) {
    Logger::info(QString("VisionClassifier: Loading model from %1").arg(QString::fromStdString(onnxPath)));
    
    m_labels = labels;
    
    bool loaded = m_engine->loadModel(
        QString::fromStdString(onnxPath),
        QSize(224, 224),
        cv::Scalar(0.485, 0.456, 0.406),
        1.0 / 255.0,
        true
    );
    
    m_modelLoaded = loaded;
    
    if (loaded) {
        Logger::info("VisionClassifier: Model loaded successfully");
        warmup(3);
    } else {
        Logger::error("VisionClassifier: Failed to load model");
    }
    
    return loaded;
}

std::vector<ClassificationResult> VisionClassifier::classify(const cv::Mat& image, const ClassifyParams& params) {
    std::vector<ClassificationResult> results;
    
    if (!m_modelLoaded) {
        Logger::warn("VisionClassifier: No model loaded");
        return results;
    }
    
    if (image.empty()) {
        Logger::warn("VisionClassifier: Empty input image");
        return results;
    }
    
    auto t0 = std::chrono::high_resolution_clock::now();
    
    QJsonObject inferResult;
    bool success = m_engine->infer(image, inferResult);
    
    auto t1 = std::chrono::high_resolution_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    
    if (!success) {
        Logger::error("VisionClassifier: Inference failed");
        return results;
    }
    
    InferenceMetrics metrics = m_engine->lastMetrics();
    
    if (inferResult.contains("top5")) {
        QJsonArray top5 = inferResult["top5"].toArray();
        int k = std::min(params.topK, static_cast<int>(top5.size()));
        
        for (int i = 0; i < k; ++i) {
            QJsonObject item = top5[i].toObject();
            ClassificationResult result;
            result.classId = item["class"].toString().toInt();
            result.confidence = item["confidence"].toDouble();
            result.preprocessMs = metrics.preprocessMs;
            result.inferenceMs = metrics.inferenceMs;
            
            if (result.classId >= 0 && result.classId < static_cast<int>(m_labels.size())) {
                result.className = m_labels[result.classId];
            } else {
                result.className = "Class_" + std::to_string(result.classId);
            }
            
            results.push_back(result);
        }
    } else {
        // Fallback to single result
        ClassificationResult result;
        result.classId = inferResult["class_index"].toInt();
        result.confidence = inferResult["confidence"].toDouble();
        result.preprocessMs = metrics.preprocessMs;
        result.inferenceMs = metrics.inferenceMs;
        
        if (result.classId >= 0 && result.classId < static_cast<int>(m_labels.size())) {
            result.className = m_labels[result.classId];
        } else {
            result.className = "Class_" + std::to_string(result.classId);
        }
        
        results.push_back(result);
    }
    
    return results;
}

void VisionClassifier::warmup(int iterations) {
    if (!m_modelLoaded) {
        return;
    }
    
    Logger::info(QString("VisionClassifier: Warming up model with %1 iterations").arg(iterations));
    
    cv::Mat dummy(224, 224, CV_8UC3, cv::Scalar(128, 128, 128));
    
    for (int i = 0; i < iterations; ++i) {
        QJsonObject result;
        m_engine->infer(dummy, result);
    }
    
    Logger::info("VisionClassifier: Warmup complete");
}

bool VisionClassifier::isLoaded() const {
    return m_modelLoaded;
}

const std::vector<std::string>& VisionClassifier::getLabels() const {
    return m_labels;
}

} // namespace QDV
