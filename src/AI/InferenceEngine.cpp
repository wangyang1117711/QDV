#include "AI/InferenceEngine.h"
#include <QFileInfo>
#include <QJsonObject>

InferenceEngine::InferenceEngine(QObject* parent)
    : QObject(parent), m_modelLoaded(false) {
}

InferenceEngine::~InferenceEngine() {
    unloadModel();
}

bool InferenceEngine::loadModel(const QString& modelPath) {
    if (m_modelLoaded) {
        unloadModel();
    }

    QFileInfo fileInfo(modelPath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return false;
    }

    m_modelPath = modelPath;
    m_modelLoaded = true;

    emit modelLoaded(true);
    return true;
}

bool InferenceEngine::unloadModel() {
    m_modelPath.clear();
    m_modelLoaded = false;
    return true;
}

bool InferenceEngine::infer(const cv::Mat& input, cv::Mat& output, QJsonObject& result) {
    if (!m_modelLoaded) {
        return false;
    }

    if (input.empty()) {
        return false;
    }

    output = input.clone();

    result["model"] = m_modelPath;
    result["input_size"] = QString("%1x%2").arg(input.cols).arg(input.rows);
    result["status"] = "success";

    emit inferenceCompleted(true);
    return true;
}