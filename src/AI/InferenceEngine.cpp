#include "AI/InferenceEngine.h"
#include "Core/Logger.h"
#include <QFileInfo>
#include <QElapsedTimer>
#include <QJsonArray>
#include <cmath>

using namespace QDV;

InferenceEngine::InferenceEngine(QObject* parent)
    : QObject(parent), m_modelLoaded(false) {
}

InferenceEngine::~InferenceEngine() {
    unloadModel();
}

void InferenceEngine::setBackend(Backend backend) {
    if (m_modelLoaded) {
        unloadModel();
    }
    m_backend = backend;
}

QStringList InferenceEngine::availableBackends() {
    QStringList backends;
    backends << "OpenCV DNN (cv::dnn::Net)";
#ifdef HAS_ONNX_RUNTIME
    backends << "ONNX Runtime";
#endif
    return backends;
}

bool InferenceEngine::loadModel(const QString& modelPath,
                                 const QSize& inputSize,
                                 const cv::Scalar& mean, double scale, bool swapRB) {
    m_lastError.clear();

    if (!QFileInfo::exists(modelPath)) {
        m_lastError.set(4, "ModelNotFound", "Model file not found: " + modelPath);
        Logger::error("InferenceEngine: " + m_lastError.errorMessage);
        emit inferenceError(m_lastError.errorCode, m_lastError.errorType, m_lastError.errorMessage);
        return false;
    }

    m_inputSize = inputSize;
    m_mean = mean;
    m_scale = scale;
    m_swapRB = swapRB;
    m_modelPath = modelPath;

    try {
        m_net = cv::dnn::readNetFromONNX(modelPath.toStdString());

        if (m_net.empty()) {
            m_lastError.set(5, "ModelLoadFailed", "Failed to read ONNX model: " + modelPath);
            Logger::error("InferenceEngine: " + m_lastError.errorMessage);
            emit inferenceError(m_lastError.errorCode, m_lastError.errorType, m_lastError.errorMessage);
            return false;
        }

        m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        m_modelLoaded = true;
    } catch (const cv::Exception& e) {
        m_lastError.set(6, "ModelLoadException",
            QString("OpenCV exception loading model: %1").arg(e.what()));
        Logger::error("InferenceEngine: " + m_lastError.errorMessage);
        emit inferenceError(m_lastError.errorCode, m_lastError.errorType, m_lastError.errorMessage);
        return false;
    } catch (const std::exception& e) {
        m_lastError.set(7, "ModelLoadException",
            QString("Exception loading model: %1").arg(e.what()));
        Logger::error("InferenceEngine: " + m_lastError.errorMessage);
        emit inferenceError(m_lastError.errorCode, m_lastError.errorType, m_lastError.errorMessage);
        return false;
    }

    m_lastError.clear();
    Logger::info("Model loaded: " + modelPath);
    emit modelLoaded(true);
    return true;
}

bool InferenceEngine::unloadModel() {
    m_net = cv::dnn::Net();
    m_modelLoaded = false;
    m_modelPath.clear();
    m_lastError.clear();
    m_inferenceCache.clear();
    Logger::info("Model unloaded");
    return true;
}

cv::Mat InferenceEngine::preprocess(const cv::Mat& input) {
    QElapsedTimer timer;
    timer.start();

    cv::Mat resized;
    cv::resize(input, resized, cv::Size(m_inputSize.width(), m_inputSize.height()));

    cv::Mat blob;
    if (input.channels() == 3 && m_swapRB) {
        cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);
    }

    if (input.channels() == 3) {
        cv::Size cvSize(m_inputSize.width(), m_inputSize.height());
        blob = cv::dnn::blobFromImage(resized, m_scale, cvSize, m_mean, m_swapRB, false);
    } else {
        std::vector<cv::Mat> channels = { resized };
        blob = cv::dnn::blobFromImages(channels);
    }

    m_lastMetrics.preprocessMs = timer.elapsed();
    return blob;
}

QJsonObject InferenceEngine::postprocess(const cv::Mat& output, const cv::Mat& preprocessed) {
    QElapsedTimer timer;
    timer.start();

    QJsonObject result;
    result["input_size"] = QString("%1x%2")
        .arg(preprocessed.size[3]).arg(preprocessed.size[2]);

    if (output.dims == 2 && output.cols > 0) {
        cv::Mat softmax;
        cv::exp(output, softmax);
        double sum = cv::sum(softmax)[0];
        if (sum > 0) {
            softmax /= sum;
        }

        double minVal, maxVal;
        cv::Point minLoc, maxLoc;
        cv::minMaxLoc(softmax, &minVal, &maxVal, &minLoc, &maxLoc);

        QString categoryId = QString("Class_%1").arg(maxLoc.x);
        result["category"] = categoryId;
        result["confidence"] = formatConfidence(maxVal);
        result["class_index"] = maxLoc.x;

        if (maxLoc.x >= 0 && maxLoc.x < m_categoryLabels.size()) {
            result["category_name"] = m_categoryLabels[maxLoc.x];
        } else {
            result["category_name"] = categoryId;
        }

        QList<Prediction> topKPreds = extractTopK(softmax, 5);
        QJsonArray top;
        for (const Prediction& pred : topKPreds) {
            QJsonObject item;
            item["class"] = pred.categoryId;
            item["name"] = pred.categoryName;
            item["confidence"] = pred.confidence;
            item["rank"] = pred.rank;
            top.append(item);
        }
        result["top5"] = top;

        result["pass"] = maxVal >= 0.5;
    } else if (output.dims == 4) {
        result["output_shape"] = QString("%1x%2x%3x%4")
            .arg(output.size[0]).arg(output.size[1])
            .arg(output.size[2]).arg(output.size[3]);
        result["category"] = "detection_output";
        result["confidence"] = 0.0;
        result["note"] = "raw_tensor_output";
        result["pass"] = false;
    }

    m_lastMetrics.postprocessMs = timer.elapsed();
    return result;
}

bool InferenceEngine::runOpenCVDNN(const cv::Mat& blob, cv::Mat& output) {
    QElapsedTimer timer;
    timer.start();

    try {
        m_net.setInput(blob);
        output = m_net.forward();
    } catch (const cv::Exception& e) {
        Logger::error(QString("OpenCV DNN forward error: %1").arg(e.what()));
        return false;
    }

    m_lastMetrics.inferenceMs = timer.elapsed();
    return !output.empty();
}

bool InferenceEngine::runONNXRuntime(const cv::Mat& input, cv::Mat& output) {
    Q_UNUSED(input)
    Q_UNUSED(output)
    Logger::warn("ONNX Runtime backend not yet integrated — using OpenCV DNN as fallback");
    return false;
}

bool InferenceEngine::infer(const cv::Mat& input, cv::Mat& output, QJsonObject& result) {
    if (!m_modelLoaded) {
        Logger::warn("Inference attempted without loaded model");
        return false;
    }

    if (input.empty()) {
        Logger::warn("Inference attempted with empty input");
        return false;
    }

    QElapsedTimer totalTimer;
    totalTimer.start();

    cv::Mat blob = preprocess(input);

    cv::Mat rawOutput;
    bool ok = false;
    if (m_backend == BackendOpenCVDNN) {
        ok = runOpenCVDNN(blob, rawOutput);
    } else {
        ok = runONNXRuntime(blob, rawOutput);
    }

    if (!ok) {
        emit inferenceCompleted(false);
        return false;
    }

    result = postprocess(rawOutput, blob);
    output = rawOutput.clone();

    m_lastMetrics.totalMs = totalTimer.elapsed();
    result["latency_ms"] = m_lastMetrics.totalMs;
    result["preprocess_ms"] = m_lastMetrics.preprocessMs;
    result["inference_ms"] = m_lastMetrics.inferenceMs;
    result["postprocess_ms"] = m_lastMetrics.postprocessMs;
    result["backend"] = availableBackends().value(m_backend);
    result["status"] = "success";

    emit inferenceCompleted(true);
    return true;
}

bool InferenceEngine::infer(const cv::Mat& input, QJsonObject& result) {
    cv::Mat output;
    return infer(input, output, result);
}

bool InferenceEngine::inferBatch(const QList<cv::Mat>& inputs, QList<QJsonObject>& results) {
    if (!m_modelLoaded || inputs.isEmpty()) return false;

    results.clear();
    results.reserve(inputs.size());

    for (int i = 0; i < inputs.size(); ++i) {
        QJsonObject result;
        bool ok = infer(inputs[i], result);
        results.append(result);
        emit progressUpdated(i + 1, inputs.size());
        if (!ok) {
            Logger::warn(QString("Batch inference failed at image %1/%2").arg(i + 1).arg(inputs.size()));
        }
    }

    return true;
}

bool InferenceEngine::warmUp(int iterations) {
    if (!m_modelLoaded) {
        m_lastError.set(1, "NoModel", "No model loaded for warm-up");
        Logger::warn("InferenceEngine: warm-up failed - no model loaded");
        return false;
    }

    m_lastError.clear();

    cv::Mat dummy(m_inputSize.height(), m_inputSize.width(), CV_8UC3, cv::Scalar(128, 128, 128));
    QJsonObject result;

    try {
        for (int i = 0; i < iterations; ++i) {
            if (!infer(dummy, result)) {
                m_lastError.set(2, "WarmUpFailed",
                    QString("Warm-up iteration %1/%2 failed").arg(i + 1).arg(iterations));
                return false;
            }
        }
    } catch (const std::exception& e) {
        m_lastError.set(3, "WarmUpException", QString("Exception during warm-up: %1").arg(e.what()));
        Logger::error("InferenceEngine: warm-up exception - " + QString(e.what()));
        return false;
    }

    m_lastError.clear();
    Logger::info(QString("Model warm-up complete: %1 iterations, avg %2ms")
                  .arg(iterations)
                  .arg(m_lastMetrics.totalMs));

    return true;
}

void InferenceEngine::setInferenceMode(InferenceMode mode) {
    m_inferenceMode = mode;
    Logger::info(QString("InferenceEngine: mode switched to %1")
        .arg(mode == SingleMode ? "Single" : (mode == BatchMode ? "Batch" : "Realtime")));
}

void InferenceEngine::setCategoryLabels(const QStringList& labels) {
    m_categoryLabels = labels;
}

double InferenceEngine::formatConfidence(double confidence) const {
    return std::round(confidence * 10000.0) / 10000.0;
}

QList<Prediction> InferenceEngine::extractTopK(const cv::Mat& softmax, int k) {
    QList<Prediction> topPredictions;

    if (softmax.empty() || softmax.cols == 0) return topPredictions;

    cv::Mat flat = softmax.reshape(1, 1);
    cv::Mat sortedIdx;
    cv::sortIdx(flat, sortedIdx, cv::SORT_EVERY_ROW + cv::SORT_DESCENDING);

    k = std::min(k, sortedIdx.cols);
    for (int i = 0; i < k; ++i) {
        int classIdx = sortedIdx.at<int>(0, i);
        float confidence = flat.at<float>(0, classIdx);

        Prediction pred;
        pred.categoryId = QString("Class_%1").arg(classIdx);
        pred.confidence = formatConfidence(confidence);
        pred.rank = i + 1;

        if (classIdx >= 0 && classIdx < m_categoryLabels.size()) {
            pred.categoryName = m_categoryLabels[classIdx];
        } else {
            pred.categoryName = pred.categoryId;
        }

        topPredictions.append(pred);
    }

    return topPredictions;
}

RecognitionResult InferenceEngine::inferWithResult(const cv::Mat& input, const QString& imageId) {
    RecognitionResult result;
    result.imageId = imageId;
    result.modelName = QFileInfo(m_modelPath).fileName();

    QString cacheKey = imageId.isEmpty()
        ? QString::number(reinterpret_cast<quintptr>(&input))
        : imageId;

    if (m_inferenceMode == SingleMode) {
        RecognitionResult* cached = m_inferenceCache.object(cacheKey);
        if (cached) {
            Logger::info("InferenceEngine: cache hit for " + cacheKey);
            return *cached;
        }
    }

    m_lastError.clear();

    QJsonObject jsonResult;
    bool ok = infer(input, jsonResult);

    result.inferenceTimeMs = m_lastMetrics.totalMs;

    if (!ok) {
        m_lastError.set(10, "InferenceFailed", "Inference execution returned failure");
        emit inferenceError(m_lastError.errorCode, m_lastError.errorType, m_lastError.errorMessage);
        return result;
    }

    QJsonArray top5 = jsonResult["top5"].toArray();
    for (int i = 0; i < top5.size(); ++i) {
        QJsonObject item = top5[i].toObject();
        Prediction pred;
        pred.categoryId = item["class"].toString();
        pred.categoryName = item.contains("name") ? item["name"].toString() : pred.categoryId;
        pred.confidence = item["confidence"].toDouble();
        pred.rank = i + 1;
        result.predictions.append(pred);
    }

    if (!result.predictions.isEmpty()) {
        result.predictions[0].categoryId = jsonResult["category"].toString();
        result.predictions[0].confidence = jsonResult["confidence"].toDouble();
        if (!result.predictions[0].categoryId.isEmpty() && !m_categoryLabels.isEmpty()) {
            int idx = result.predictions[0].categoryId.remove("Class_").toInt();
            if (idx >= 0 && idx < m_categoryLabels.size()) {
                result.predictions[0].categoryName = m_categoryLabels[idx];
            }
        }
    }

    if (m_inferenceMode == SingleMode) {
        m_inferenceCache.insert(cacheKey, new RecognitionResult(result));
    }

    return result;
}