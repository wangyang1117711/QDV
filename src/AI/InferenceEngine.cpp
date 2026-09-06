#include "AI/InferenceEngine.h"
#include "AI/InferenceCache.h"
#include "Core/Logger.h"
#include "Monitoring/TrainingInferenceMonitor.h"
#include <QFileInfo>
#include <QElapsedTimer>
#include <QJsonArray>
#include <cmath>
#include <algorithm>
#include <vector>

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
                                 const cv::Scalar& mean, double scale, bool swapRB,
                                 const cv::Scalar& std) {
    m_lastError.clear();

    // P0-4b（0906 优化）：同路径短路 —— 预览链每次执行都重建工具对象并重新
    // configure → loadModel；若模型路径与预处理参数均未变化，直接复用已加载的
    // cv::dnn::Net，避免每次预览重付 readNetFromONNX + warmUp（0.3~2s/次）。
    // 注意：预处理参数变化仅影响推理时的 blob 构造，不必重载模型文件本身。
    if (m_modelLoaded && m_modelPath == modelPath) {
        m_inputSize = inputSize;
        m_mean = mean;
        m_scale = scale;
        m_swapRB = swapRB;
        m_std = std;
        return true;
    }

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
    m_std = std;
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
        m_warmUpDone = false;  // P0-4b：新模型需重新 warmUp
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
    m_warmUpDone = false;  // P0-4b：卸载后重置 warmUp 状态
    m_modelPath.clear();
    m_lastError.clear();
    m_inferenceCache.clear();
    Logger::info("Model unloaded");
    return true;
}

cv::Mat InferenceEngine::preprocess(const cv::Mat& input) {
    QElapsedTimer timer;
    timer.start();

    // 1) 统一 resize 到模型输入尺寸
    cv::Mat resized;
    cv::resize(input, resized, cv::Size(m_inputSize.width(), m_inputSize.height()));

    // 2) 统一转换为 3 通道 BGR：分类/检测模型均要求 3 通道输入
    // 单通道（如 ReadImage 通道分离 G）或 4 通道（BGRA）均需转换
    cv::Mat bgr;
    if (resized.channels() == 1) {
        cv::cvtColor(resized, bgr, cv::COLOR_GRAY2BGR);
    } else if (resized.channels() == 4) {
        cv::cvtColor(resized, bgr, cv::COLOR_BGRA2BGR);
    } else {
        bgr = resized;
    }

    // 3) blobFromImage 一步完成: resize + swapRB(BGR→RGB) + scale(÷255) + mean 减法
    // 修复: 不再手动 cvtColor(BGR2RGB)，避免与 swapRB=true 双重交换导致最终仍为 BGR
    cv::Size cvSize(m_inputSize.width(), m_inputSize.height());
    cv::Mat blob = cv::dnn::blobFromImage(bgr, m_scale, cvSize, m_mean, m_swapRB, false);

    // 4) 补全 std 归一化: 训练时为 (pixel/255 - mean) / std，推理必须一致
    // blob 为 NCHW [1, 3, H, W] float32，逐通道除以 std
    if (m_std[0] != 1.0 || m_std[1] != 1.0 || m_std[2] != 1.0) {
        int h = blob.size[2];
        int w = blob.size[3];
        for (int c = 0; c < 3; c++) {
            // 构造指向第 c 个通道 H×W 区域的 Mat 头，原地除法
            cv::Mat channel(h, w, CV_32F, blob.ptr<float>(0, c));
            channel /= static_cast<float>(m_std[c]);
        }
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
        // === 分类分支：dims=2 [1, nc] ===
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
        result["model_type"] = "classification";
        result["pass"] = maxVal >= 0.5;

    } else if (output.dims == 3) {
        // === YOLO 检测分支：dims=3 ===
        // YOLOv8: [1, 4+nc, anchors]  (第二维 4+nc，box 回归在前，类别在后)
        // YOLOv5: [1, anchors, 5+nc]  (第二维是 anchors，最后一维 5+nc)
        int dim1 = output.size[1];
        int dim2 = output.size[2];

        // 判别依据：YOLOv8 的第二维(4+nc)通常 < 第三维(anchors)
        //          YOLOv5 的第二维(anchors)通常 > 第三维(5+nc)
        // 如果 dim2 > dim1，大概率是 YOLOv8 [1, 4+nc, anchors]
        // 如果 dim1 > dim2，大概率是 YOLOv5 [1, anchors, 5+nc]
        if (dim2 > dim1) {
            // YOLOv8: [1, 4+nc, anchors]
            int numClasses = dim1 - 4;
            if (numClasses > 0) {
                result = postprocessYoloV8(output, numClasses);
            }
        } else {
            // YOLOv5: [1, anchors, 5+nc]
            int numClasses = dim2 - 5;
            if (numClasses > 0) {
                result = postprocessYoloV5(output, numClasses);
            }
        }

    } else if (output.dims == 4) {
        result["output_shape"] = QString("%1x%2x%3x%4")
            .arg(output.size[0]).arg(output.size[1])
            .arg(output.size[2]).arg(output.size[3]);
        result["category"] = "detection_output";
        result["confidence"] = 0.0;
        result["note"] = "raw_tensor_output";
        result["model_type"] = "unknown";
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
#ifdef HAS_ONNX_RUNTIME
    // ONNX Runtime 实现
    // 注意：当前为占位实现，实际集成需要：
    // 1. 链接 onnxruntime 库
    // 2. 创建 Ort::Env 和 Ort::Session
    // 3. 创建输入 tensor
    // 4. 运行推理
    // 5. 转换输出为 cv::Mat

    QElapsedTimer timer;
    timer.start();

    try {
        // TODO: 实现 ONNX Runtime 推理
        // 当前为占位，实际集成时替换
        Logger::warn("ONNX Runtime backend not yet implemented - using OpenCV DNN as fallback");
        return runOpenCVDNN(input, output);
    } catch (const std::exception& e) {
        Logger::error(QString("ONNX Runtime error: %1").arg(e.what()));
        return false;
    }
#else
    Q_UNUSED(input)
    Q_UNUSED(output)
    Logger::warn("ONNX Runtime backend not compiled - using OpenCV DNN as fallback");
    return false;
#endif
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

    // === 第二层缓存：推理结果缓存查询（InferenceCache）===
    QString resultCacheKey;
    if (m_resultCache) {
        qint64 modelMtime = QFileInfo(m_modelPath).lastModified().toMSecsSinceEpoch();
        resultCacheKey = InferenceCache::generateKey(
            m_modelPath, modelMtime, m_inputSize, m_confThreshold, input);
        if (m_resultCache->lookup(resultCacheKey, result)) {
            output = cv::Mat();  // 缓存命中，无 raw output
            emit inferenceCompleted(true);
            return true;
        }
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
        // 将最后一次错误信息写入 result，供上游算子/UI 展示
        if (m_lastError.hasError && !m_lastError.errorMessage.isEmpty()) {
            result["error"] = m_lastError.errorMessage;
        } else {
            result["error"] = QStringLiteral("推理执行失败");
        }
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

    // 上报推理延迟到监控器
    QDV::TrainingInferenceMonitor::instance()->recordInferenceLatency(m_lastMetrics.totalMs);

    // === 第二层缓存：推理结果写入（InferenceCache）===
    if (m_resultCache && !resultCacheKey.isEmpty()) {
        m_resultCache->insert(resultCacheKey, result);
    }

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

    // P0-4b（0906 优化）：当前模型已 warmUp 则直接返回成功 —— 预览链每次执行
    // 重建工具对象导致 warmUp 请求风暴，这里在引擎侧去重（同模型只 warm 一次）。
    // 模型重载（loadModel 不同路径）会重置 m_warmUpDone。
    if (m_warmUpDone) {
        return true;
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
    m_warmUpDone = true;
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

QJsonObject InferenceEngine::postprocessYoloV8(const cv::Mat& output, int numClasses) {
    // YOLOv8 输出格式：[1, 4+nc, anchors]
    // 前 4 维是 bbox（cx, cy, w, h），已是像素坐标（无需解码 anchor）
    // 后 nc 维是类别概率（已就绪，无需 sigmoid）
    // 转置为 [anchors, 4+nc] 方便逐行处理
    cv::Mat transposed;
    cv::transpose(output.reshape(1, output.size[1]), transposed);
    // transposed: [anchors, 4+nc]

    int numAnchors = transposed.rows;
    std::vector<cv::Rect2d> boxes;
    std::vector<double> scores;
    std::vector<int> classIds;
    double maxConfidence = 0.0;

    for (int i = 0; i < numAnchors; ++i) {
        float* row = transposed.ptr<float>(i);
        float cx = row[0];
        float cy = row[1];
        float w = row[2];
        float h = row[3];

        // 找最大类别概率
        float maxScore = 0.0f;
        int maxClassId = 0;
        for (int c = 0; c < numClasses; ++c) {
            float score = row[4 + c];
            if (score > maxScore) {
                maxScore = score;
                maxClassId = c;
            }
        }

        if (maxScore > maxConfidence) {
            maxConfidence = maxScore;
        }

        // 置信度过滤
        if (maxScore >= m_confThreshold) {
            cv::Rect2d box(cx - w / 2.0, cy - h / 2.0, w, h);
            boxes.push_back(box);
            scores.push_back(maxScore);
            classIds.push_back(maxClassId);
        }
    }

    // NMS 抑制重复框
    std::vector<int> indices = nms(boxes, scores, m_iouThreshold);

    // 构建结果 JSON
    QJsonObject result;
    result["model_type"] = "yolov8";
    result["num_detections"] = static_cast<int>(indices.size());
    result["max_confidence"] = formatConfidence(maxConfidence);

    QJsonArray detections;
    for (int idx : indices) {
        QJsonObject det;
        det["class_id"] = classIds[idx];
        det["confidence"] = formatConfidence(scores[idx]);
        det["cx"] = boxes[idx].x + boxes[idx].width / 2.0;
        det["cy"] = boxes[idx].y + boxes[idx].height / 2.0;
        det["w"] = boxes[idx].width;
        det["h"] = boxes[idx].height;

        // 类别名（有标签则用标签，否则用 Class_<id>）
        if (classIds[idx] >= 0 && classIds[idx] < m_categoryLabels.size()) {
            det["class_name"] = m_categoryLabels[classIds[idx]];
        } else {
            det["class_name"] = QString("Class_%1").arg(classIds[idx]);
        }

        detections.append(det);
    }
    result["detections"] = detections;
    result["pass"] = !indices.empty();

    return result;
}

QJsonObject InferenceEngine::postprocessYoloV5(const cv::Mat& output, int numClasses) {
    // YOLOv5 输出格式：[1, anchors, 5+nc]
    // 每行：[cx, cy, w, h, obj_conf, cls_conf_0, cls_conf_1, ...]
    // 最终置信度 = sigmoid(obj_conf) * sigmoid(cls_conf)
    // bbox 已是像素坐标
    cv::Mat mat = output.reshape(1, output.size[1]);
    // mat: [anchors, 5+nc]

    int numAnchors = mat.rows;
    std::vector<cv::Rect2d> boxes;
    std::vector<double> scores;
    std::vector<int> classIds;
    double maxConfidence = 0.0;

    auto sigmoid = [](float x) {
        return 1.0f / (1.0f + std::exp(-x));
    };

    for (int i = 0; i < numAnchors; ++i) {
        float* row = mat.ptr<float>(i);
        float cx = row[0];
        float cy = row[1];
        float w = row[2];
        float h = row[3];
        float objConf = sigmoid(row[4]);

        // 找最大类别概率
        float maxClsScore = 0.0f;
        int maxClassId = 0;
        for (int c = 0; c < numClasses; ++c) {
            float clsScore = sigmoid(row[5 + c]);
            if (clsScore > maxClsScore) {
                maxClsScore = clsScore;
                maxClassId = c;
            }
        }

        float finalScore = objConf * maxClsScore;
        if (finalScore > maxConfidence) {
            maxConfidence = finalScore;
        }

        // 置信度过滤
        if (finalScore >= m_confThreshold) {
            cv::Rect2d box(cx - w / 2.0, cy - h / 2.0, w, h);
            boxes.push_back(box);
            scores.push_back(finalScore);
            classIds.push_back(maxClassId);
        }
    }

    // NMS 抑制重复框
    std::vector<int> indices = nms(boxes, scores, m_iouThreshold);

    // 构建结果 JSON（格式与 YOLOv8 一致）
    QJsonObject result;
    result["model_type"] = "yolov5";
    result["num_detections"] = static_cast<int>(indices.size());
    result["max_confidence"] = formatConfidence(maxConfidence);

    QJsonArray detections;
    for (int idx : indices) {
        QJsonObject det;
        det["class_id"] = classIds[idx];
        det["confidence"] = formatConfidence(scores[idx]);
        det["cx"] = boxes[idx].x + boxes[idx].width / 2.0;
        det["cy"] = boxes[idx].y + boxes[idx].height / 2.0;
        det["w"] = boxes[idx].width;
        det["h"] = boxes[idx].height;

        if (classIds[idx] >= 0 && classIds[idx] < m_categoryLabels.size()) {
            det["class_name"] = m_categoryLabels[classIds[idx]];
        } else {
            det["class_name"] = QString("Class_%1").arg(classIds[idx]);
        }

        detections.append(det);
    }
    result["detections"] = detections;
    result["pass"] = !indices.empty();

    return result;
}

double InferenceEngine::computeIoU(const cv::Rect2d& a, const cv::Rect2d& b) {
    // 计算两个矩形的交并比（IoU）
    double interX1 = std::max(a.x, b.x);
    double interY1 = std::max(a.y, b.y);
    double interX2 = std::min(a.x + a.width, b.x + b.width);
    double interY2 = std::min(a.y + a.height, b.y + b.height);

    double interW = std::max(0.0, interX2 - interX1);
    double interH = std::max(0.0, interY2 - interY1);
    double interArea = interW * interH;

    double aArea = a.width * a.height;
    double bArea = b.width * b.height;
    double unionArea = aArea + bArea - interArea;

    if (unionArea <= 0.0) return 0.0;
    return interArea / unionArea;
}

std::vector<int> InferenceEngine::nms(const std::vector<cv::Rect2d>& boxes,
                                       const std::vector<double>& scores,
                                       double iouThreshold) {
    // 非极大值抑制：按分数降序保留，抑制 IoU 超过阈值的重复框
    std::vector<int> indices;
    if (boxes.empty()) return indices;

    // 按分数降序排序索引
    std::vector<int> order(scores.size());
    for (size_t i = 0; i < scores.size(); ++i) order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(),
              [&scores](int a, int b) { return scores[a] > scores[b]; });

    std::vector<bool> suppressed(scores.size(), false);
    for (size_t i = 0; i < order.size(); ++i) {
        int idx = order[i];
        if (suppressed[idx]) continue;
        indices.push_back(idx);

        for (size_t j = i + 1; j < order.size(); ++j) {
            int idx2 = order[j];
            if (suppressed[idx2]) continue;
            if (computeIoU(boxes[idx], boxes[idx2]) > iouThreshold) {
                suppressed[idx2] = true;
            }
        }
    }

    return indices;
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

// ===== v5.4.0: 检测/分割推理接口实现 =====

bool InferenceEngine::detect(const cv::Mat& input, double confThreshold, double iouThreshold,
                              QJsonObject& result) {
    if (!m_modelLoaded) {
        m_lastError.set(2001, "ModelNotLoaded", "模型未加载，请先调用 loadModel()");
        emit inferenceError(m_lastError.errorCode, m_lastError.errorType, m_lastError.errorMessage);
        return false;
    }
    if (input.empty()) {
        m_lastError.set(2002, "EmptyInput", "输入图像为空");
        emit inferenceError(m_lastError.errorCode, m_lastError.errorType, m_lastError.errorMessage);
        return false;
    }

    m_confThreshold = confThreshold;
    m_iouThreshold = iouThreshold;

    QElapsedTimer totalTimer;
    totalTimer.start();

    // 预处理
    QElapsedTimer preTimer;
    preTimer.start();
    cv::Mat blob = cv::dnn::blobFromImage(input, m_scale, cv::Size(m_inputSize.width(), m_inputSize.height()), m_mean, m_swapRB, false, CV_32F);
    // std 归一化（如果配置了）
    if (m_std[0] != 1.0 || m_std[1] != 1.0 || m_std[2] != 1.0) {
        // blobFromImage 已做 (pixel - mean) * scale，这里再除以 std
        cv::Mat channels[3];
        cv::split(blob, channels);
        for (int c = 0; c < 3; ++c) {
            channels[c] /= m_std[c];
        }
        cv::merge(channels, 3, blob);
    }
    m_lastMetrics.preprocessMs = preTimer.elapsed();

    // 推理
    QElapsedTimer inferTimer;
    inferTimer.start();
    cv::Mat output;
    if (!runOpenCVDNN(blob, output)) {
        m_lastError.set(2003, "InferenceFailed", "OpenCV DNN 推理失败");
        emit inferenceError(m_lastError.errorCode, m_lastError.errorType, m_lastError.errorMessage);
        return false;
    }
    m_lastMetrics.inferenceMs = inferTimer.elapsed();

    // 后处理（自动识别 YOLOv5/v8 格式）
    QElapsedTimer postTimer;
    postTimer.start();
    int numClasses = m_categoryLabels.size();
    if (numClasses == 0) numClasses = 80;  // COCO 默认 80 类

    // 根据输出形状判断 YOLO 版本
    // YOLOv8: [1, 4+nc, anchors]  -- dims=3, size[1]=4+nc
    // YOLOv5: [1, anchors, 5+nc]  -- dims=3, size[2]=5+nc
    if (output.dims == 3) {
        if (output.size[1] == 4 + numClasses) {
            // YOLOv8 格式
            result = postprocessYoloV8(output, numClasses);
        } else if (output.size[2] == 5 + numClasses) {
            // YOLOv5 格式
            result = postprocessYoloV5(output, numClasses);
        } else {
            // 未知格式，尝试 YOLOv5 兜底
            result = postprocessYoloV5(output, numClasses);
        }
    } else if (output.dims == 2) {
        // 分类输出误用 detect，返回空检测结果
        QJsonArray emptyArr;
        result["detections"] = emptyArr;
        result["numDetections"] = 0;
        result["warning"] = "模型输出为 2D（分类格式），无检测结果";
    } else {
        QJsonArray emptyArr;
        result["detections"] = emptyArr;
        result["numDetections"] = 0;
        result["warning"] = QString("未知输出维度: %1").arg(output.dims);
    }

    m_lastMetrics.postprocessMs = postTimer.elapsed();
    m_lastMetrics.totalMs = totalTimer.elapsed();
    m_lastMetrics.backend = "OpenCV_DNN";

    emit inferenceCompleted(true);
    return true;
}

bool InferenceEngine::segment(const cv::Mat& input, cv::Mat& mask, cv::Mat& overlay,
                               QJsonObject& result) {
    if (!m_modelLoaded) {
        m_lastError.set(2001, "ModelNotLoaded", "模型未加载，请先调用 loadModel()");
        emit inferenceError(m_lastError.errorCode, m_lastError.errorType, m_lastError.errorMessage);
        return false;
    }
    if (input.empty()) {
        m_lastError.set(2002, "EmptyInput", "输入图像为空");
        emit inferenceError(m_lastError.errorCode, m_lastError.errorType, m_lastError.errorMessage);
        return false;
    }

    QElapsedTimer totalTimer;
    totalTimer.start();

    // 预处理
    QElapsedTimer preTimer;
    preTimer.start();
    cv::Mat blob = cv::dnn::blobFromImage(input, m_scale, cv::Size(m_inputSize.width(), m_inputSize.height()), m_mean, m_swapRB, false, CV_32F);
    if (m_std[0] != 1.0 || m_std[1] != 1.0 || m_std[2] != 1.0) {
        cv::Mat channels[3];
        cv::split(blob, channels);
        for (int c = 0; c < 3; ++c) {
            channels[c] /= m_std[c];
        }
        cv::merge(channels, 3, blob);
    }
    m_lastMetrics.preprocessMs = preTimer.elapsed();

    // 推理
    QElapsedTimer inferTimer;
    inferTimer.start();
    cv::Mat output;
    if (!runOpenCVDNN(blob, output)) {
        m_lastError.set(2003, "InferenceFailed", "OpenCV DNN 推理失败");
        emit inferenceError(m_lastError.errorCode, m_lastError.errorType, m_lastError.errorMessage);
        return false;
    }
    m_lastMetrics.inferenceMs = inferTimer.elapsed();

    // 后处理
    QElapsedTimer postTimer;
    postTimer.start();
    result = postprocessSegment(output, mask, overlay, input);
    m_lastMetrics.postprocessMs = postTimer.elapsed();

    m_lastMetrics.totalMs = totalTimer.elapsed();
    m_lastMetrics.backend = "OpenCV_DNN";

    emit inferenceCompleted(true);
    return true;
}

QJsonObject InferenceEngine::postprocessSegment(const cv::Mat& output, cv::Mat& mask, cv::Mat& overlay,
                                                  const cv::Mat& originalInput) {
    QJsonObject result;

    // 分割模型输出通常是 [1, numClasses, H, W] 或 [1, 1, H, W]
    // 对每个像素取 argmax 得到类别索引
    cv::Mat resizedOutput;

    if (output.dims == 4) {
        // [1, C, H, W] 格式
        int numClasses = output.size[1];
        int outH = output.size[2];
        int outW = output.size[3];

        // 重塑为 [C, H*W] 便于 argmax
        cv::Mat reshaped = output.reshape(1, numClasses);
        cv::Mat argmax = cv::Mat::zeros(outH, outW, CV_8UC1);

        for (int y = 0; y < outH; ++y) {
            for (int x = 0; x < outW; ++x) {
                int maxIdx = 0;
                float maxVal = -1e30f;
                for (int c = 0; c < numClasses; ++c) {
                    float val = reshaped.at<float>(c, y * outW + x);
                    if (val > maxVal) {
                        maxVal = val;
                        maxIdx = c;
                    }
                }
                argmax.at<uchar>(y, x) = static_cast<uchar>(maxIdx);
            }
        }

        // 调整到原始图像尺寸
        cv::resize(argmax, mask, originalInput.size(), 0, 0, cv::INTER_NEAREST);
    } else if (output.dims == 3) {
        // [1, H, W] 格式（单通道分割）
        int outH = output.size[1];
        int outW = output.size[2];
        cv::Mat single = cv::Mat(outH, outW, CV_32FC1);
        for (int y = 0; y < outH; ++y) {
            for (int x = 0; x < outW; ++x) {
                single.at<float>(y, x) = output.at<float>(0, y, x);
            }
        }
        single.convertTo(single, CV_8UC1, 1.0);
        cv::resize(single, mask, originalInput.size(), 0, 0, cv::INTER_NEAREST);
    } else {
        // 未知格式，返回空掩膜
        mask = cv::Mat::zeros(originalInput.size(), CV_8UC1);
        result["warning"] = QString("未知分割输出维度: %1").arg(output.dims);
    }

    // 生成彩色叠加图
    overlay = originalInput.clone();
    if (!mask.empty()) {
        // 为每个类别生成随机颜色
        int numClasses = 21;  // VOC 默认 21 类
        cv::Mat coloredMask = cv::Mat::zeros(mask.size(), CV_8UC3);
        for (int i = 0; i < numClasses; ++i) {
            cv::Vec3b color(
                static_cast<uchar>((i * 50) % 256),
                static_cast<uchar>((i * 100 + 50) % 256),
                static_cast<uchar>((i * 150 + 100) % 256)
            );
            cv::Mat classMask = (mask == i);
            coloredMask.setTo(color, classMask);
        }

        // 半透明叠加
        cv::addWeighted(overlay, 0.6, coloredMask, 0.4, 0, overlay);
    }

    // 统计各类别像素数
    QJsonObject classStats;
    if (!mask.empty()) {
        for (int i = 0; i < 21; ++i) {
            int count = cv::countNonZero(mask == i);
            if (count > 0) {
                QString label = (i < m_categoryLabels.size())
                    ? m_categoryLabels[i]
                    : QString("class_%1").arg(i);
                classStats[label] = count;
            }
        }
    }

    result["classStats"] = classStats;
    result["maskWidth"] = mask.cols;
    result["maskHeight"] = mask.rows;

    return result;
}