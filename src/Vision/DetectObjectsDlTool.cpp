#include "Vision/DetectObjectsDlTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QJsonArray>
#include <algorithm>
#include <vector>

using namespace QDV;

DetectObjectsDlTool::DetectObjectsDlTool() {
    m_name = "DL目标检测";
}

DetectObjectsDlTool::~DetectObjectsDlTool() {
    // m_engine 所有权归外部（注入方），不 delete
}

bool DetectObjectsDlTool::configure(const QJsonObject& params) {
    if (params.contains("modelPath")) {
        setModelPath(params["modelPath"].toString());
    }
    if (params.contains("confidenceThreshold")) {
        setConfidenceThreshold(params["confidenceThreshold"].toDouble(0.25));
    }
    if (params.contains("nmsThreshold")) {
        setNmsThreshold(params["nmsThreshold"].toDouble(0.45));
    }
    if (params.contains("inputWidth") && params.contains("inputHeight")) {
        setInputSize(params["inputWidth"].toInt(640), params["inputHeight"].toInt(640));
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
    return true;
}

bool DetectObjectsDlTool::loadModel() {
    if (m_modelLoaded) return true;
    if (m_modelPath.isEmpty()) return false;

    if (!QFileInfo::exists(m_modelPath)) {
        Logger::error("DetectObjectsDlTool: model file not found: " + m_modelPath);
        return false;
    }

    try {
        m_net = cv::dnn::readNetFromONNX(m_modelPath.toStdString());
        if (m_net.empty()) {
            Logger::error("DetectObjectsDlTool: failed to read ONNX model: " + m_modelPath);
            return false;
        }
        m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        m_modelLoaded = true;
        Logger::info("DetectObjectsDlTool: model loaded: " + m_modelPath);
    } catch (const cv::Exception& e) {
        Logger::error(QString("DetectObjectsDlTool: OpenCV exception loading model: %1").arg(e.what()));
        return false;
    } catch (const std::exception& e) {
        Logger::error(QString("DetectObjectsDlTool: exception loading model: %1").arg(e.what()));
        return false;
    }
    return true;
}

bool DetectObjectsDlTool::execute(const cv::Mat& input, ToolResult& result) {
    // 线程安全：防止 m_modelLoaded 检查-设置竞态与 m_net 并发推理冲突
    QMutexLocker locker(&m_execMutex);

    if (input.empty()) {
        Logger::warn("DetectObjectsDlTool: empty input");
        result.ok = false;
        return false;
    }

    if (m_modelPath.isEmpty()) {
        Logger::warn("DetectObjectsDlTool: no model configured");
        result.ok = false;
        result.data["error"] = "No model configured";
        result.data["modelLoaded"] = false;
        return false;
    }

    // 优先通过 IInferenceEngine::detect 接口推理
    if (m_engine) {
        result.data["modelLoaded"] = true;

        QJsonObject detectResult;
        bool ok = m_engine->detect(input, m_confidenceThreshold, m_nmsThreshold, detectResult);
        if (!ok) {
            Logger::error("DetectObjectsDlTool: detection failed via engine");
            result.ok = false;
            result.data["error"] = "Detection failed";
            return false;
        }

        // 解析 detections 数组（classId/className/confidence/bbox[x,y,w,h]）
        QJsonArray detections = detectResult["detections"].toArray();
        int numDetections = detections.size();
        double maxConfidence = 0.0;
        for (const auto& det : detections) {
            double conf = det.toObject()["confidence"].toDouble();
            if (conf > maxConfidence) maxConfidence = conf;
        }

        // 指标
        InferenceMetricsLite metrics = m_engine->lastMetrics();
        result.elapsedMs = metrics.totalMs;

        // 准备 overlayImage（用于绘制边界框 + 标签 + 置信度）
        if (input.channels() == 1) {
            cv::cvtColor(input, result.overlayImage, cv::COLOR_GRAY2BGR);
        } else {
            result.overlayImage = input.clone();
        }

        // 绘制检测框（绿色）并填充 result.data["detections"]
        for (const auto& det : detections) {
            QJsonObject d = det.toObject();
            int classId = d["classId"].toInt();
            double conf = d["confidence"].toDouble();
            QJsonArray bbox = d["bbox"].toArray();
            int x = static_cast<int>(bbox[0].toDouble());
            int y = static_cast<int>(bbox[1].toDouble());
            int w = static_cast<int>(bbox[2].toDouble());
            int h = static_cast<int>(bbox[3].toDouble());

            // 绿色边界框
            cv::rectangle(result.overlayImage, cv::Point(x, y),
                          cv::Point(x + w, y + h), cv::Scalar(0, 255, 0), 2);

            // 标签文本
            QString className = d["className"].toString();
            if (className.isEmpty()) {
                if (classId >= 0 && classId < m_categoryLabels.size()) {
                    className = m_categoryLabels[classId];
                } else {
                    className = QString("class_%1").arg(classId);
                }
            }
            QString label = QString("%1 %2").arg(className).arg(conf, 0, 'f', 2);
            std::string labelStr = label.toStdString();

            int baseLine = 0;
            cv::Size labelSize = cv::getTextSize(labelStr,
                cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
            int top = std::max(y, labelSize.height);
            cv::rectangle(result.overlayImage,
                cv::Point(x, top - labelSize.height),
                cv::Point(x + labelSize.width, top + baseLine),
                cv::Scalar(0, 255, 0), cv::FILLED);
            cv::putText(result.overlayImage, labelStr,
                cv::Point(x, top), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                cv::Scalar(0, 0, 0), 1);
        }

        result.data["detections"] = detections;
        result.data["detectionCount"] = numDetections;
        result.data["elapsedMs"] = result.elapsedMs;
        result.data["confidenceThreshold"] = m_confidenceThreshold;
        result.data["nmsThreshold"] = m_nmsThreshold;
        result.data["preprocess_ms"] = metrics.preprocessMs;
        result.data["inference_ms"] = metrics.inferenceMs;
        result.data["postprocess_ms"] = metrics.postprocessMs;
        result.data["backend"] = metrics.backend;
        result.score = maxConfidence;
        result.ok = true;

        // typed ports 填充（与 operators.json outputs: detections 保持一致）
        result.ports["detections"] = detections.toVariantList();

        m_results["lastDetectionCount"] = numDetections;
        m_results["lastMaxConfidence"] = maxConfidence;

        return true;
    }

    // Fallback: m_engine 未注入时，使用 cv::dnn::Net 直接推理
    Logger::warn("DetectObjectsDlTool: no inference engine injected, fallback to cv::dnn::Net");

    // 懒加载模型（首次执行时加载）
    if (!m_modelLoaded && !loadModel()) {
        result.ok = false;
        result.data["error"] = "Failed to load model";
        result.data["modelLoaded"] = false;
        return false;
    }
    result.data["modelLoaded"] = true;

    QElapsedTimer timer;
    timer.start();

    // 预处理：resize + normalize + swapRB
    cv::Mat blob = cv::dnn::blobFromImage(input, 1.0 / 255.0,
        cv::Size(m_inputWidth, m_inputHeight),
        cv::Scalar(0, 0, 0), true, false, CV_32F);

    // 推理
    m_net.setInput(blob);
    cv::Mat output = m_net.forward();

    // 后处理：解析检测框 + NMS + 过滤低置信度
    std::vector<int> classIds;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;
    postprocess(output, input.size(), classIds, confidences, boxes);

    result.elapsedMs = timer.elapsed();

    // 绘制检测框到 overlayImage
    if (input.channels() == 1) {
        cv::cvtColor(input, result.overlayImage, cv::COLOR_GRAY2BGR);
    } else {
        result.overlayImage = input.clone();
    }

    // 构建检测结果 JSON 并绘制
    QJsonArray detectionsArray;
    for (size_t i = 0; i < boxes.size(); ++i) {
        const cv::Rect& box = boxes[i];
        int classId = classIds[i];
        float conf = confidences[i];

        // 绘制检测框（绿色矩形）
        cv::rectangle(result.overlayImage, box, cv::Scalar(0, 255, 0), 2);

        // 生成标签文本
        QString label;
        if (classId < m_categoryLabels.size()) {
            label = m_categoryLabels[classId];
        } else {
            label = QString("class_%1").arg(classId);
        }
        label += QString(" %1").arg(conf, 0, 'f', 2);

        // 绘制标签背景和文字
        int baseLine = 0;
        cv::Size labelSize = cv::getTextSize(label.toStdString(),
            cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
        int top = std::max(box.y, labelSize.height);
        cv::rectangle(result.overlayImage,
            cv::Point(box.x, top - labelSize.height),
            cv::Point(box.x + labelSize.width, top + baseLine),
            cv::Scalar(0, 255, 0), cv::FILLED);
        cv::putText(result.overlayImage, label.toStdString(),
            cv::Point(box.x, top), cv::FONT_HERSHEY_SIMPLEX, 0.5,
            cv::Scalar(0, 0, 0), 1);

        // 检测结果 JSON
        QJsonObject det;
        det["x"] = box.x;
        det["y"] = box.y;
        det["w"] = box.width;
        det["h"] = box.height;
        det["classId"] = classId;
        det["confidence"] = (double)conf;
        if (classId < m_categoryLabels.size()) {
            det["className"] = m_categoryLabels[classId];
        }
        detectionsArray.append(det);
    }

    result.data["detections"] = detectionsArray;
    result.data["detectionCount"] = (int)boxes.size();
    result.data["elapsedMs"] = result.elapsedMs;
    result.data["confidenceThreshold"] = m_confidenceThreshold;
    result.data["nmsThreshold"] = m_nmsThreshold;
    result.score = boxes.empty() ? 0.0 : (double)confidences[0];
    result.ok = true;

    // typed ports 填充（与 operators.json outputs: detections 保持一致）
    result.ports["detections"] = detectionsArray.toVariantList();

    m_results["lastDetectionCount"] = (int)boxes.size();
    m_results["lastMaxConfidence"] = result.score;

    return true;
}

void DetectObjectsDlTool::postprocess(const cv::Mat& output, const cv::Size& origSize,
                                       std::vector<int>& classIds,
                                       std::vector<float>& confidences,
                                       std::vector<cv::Rect>& boxes) {
    // YOLOv5 输出格式: [1, num_detections, 5+num_classes]
    // 每行: [cx, cy, w, h, obj_conf, class_conf_0, ..., class_conf_n]
    // 兼容 [1, 1, num_dets, 5+nc] 的 4D 导出格式
    if (output.dims < 3) return;

    // 自适应维度：3D [1, dets, data] 或 4D [1, 1, dets, data]
    int detDim, dataDim;
    if (output.dims == 3) {
        detDim = 1;
        dataDim = 2;
    } else {
        detDim = output.dims - 1;
        dataDim = output.dims;
    }

    int numDetections = output.size[detDim];
    int numData = output.size[dataDim];
    int numClasses = numData - 5;
    if (numClasses <= 0) return;

    const float* data = (const float*)output.data;
    float xRatio = (float)origSize.width / (float)m_inputWidth;
    float yRatio = (float)origSize.height / (float)m_inputHeight;

    for (int i = 0; i < numDetections; ++i) {
        const float* row = data + i * numData;
        float objConf = row[4];
        if (objConf < (float)m_confidenceThreshold) continue;

        // 找到最大类别概率
        float maxClassConf = 0.0f;
        int maxClassId = 0;
        for (int j = 0; j < numClasses; ++j) {
            float classConf = row[5 + j];
            if (classConf > maxClassConf) {
                maxClassConf = classConf;
                maxClassId = j;
            }
        }

        float confidence = objConf * maxClassConf;
        if (confidence < (float)m_confidenceThreshold) continue;

        // cx, cy, w, h -> x, y, w, h（左上角坐标 + 尺寸）
        float cx = row[0];
        float cy = row[1];
        float w = row[2];
        float h = row[3];

        int left = (int)((cx - w * 0.5f) * xRatio);
        int top = (int)((cy - h * 0.5f) * yRatio);
        int width = (int)(w * xRatio);
        int height = (int)(h * yRatio);

        // 裁剪到图像范围内
        left = std::max(0, std::min(left, origSize.width - 1));
        top = std::max(0, std::min(top, origSize.height - 1));
        width = std::min(width, origSize.width - left);
        height = std::min(height, origSize.height - top);

        classIds.push_back(maxClassId);
        confidences.push_back(confidence);
        boxes.emplace_back(left, top, width, height);
    }

    // NMS 去除重叠框
    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences,
        (float)m_confidenceThreshold, (float)m_nmsThreshold, indices);

    // 只保留 NMS 后的检测框
    std::vector<int> filteredClassIds;
    std::vector<float> filteredConfidences;
    std::vector<cv::Rect> filteredBoxes;
    filteredClassIds.reserve(indices.size());
    filteredConfidences.reserve(indices.size());
    filteredBoxes.reserve(indices.size());
    for (int idx : indices) {
        filteredClassIds.push_back(classIds[idx]);
        filteredConfidences.push_back(confidences[idx]);
        filteredBoxes.push_back(boxes[idx]);
    }
    classIds = std::move(filteredClassIds);
    confidences = std::move(filteredConfidences);
    boxes = std::move(filteredBoxes);
}

QList<PortDescriptor> DetectObjectsDlTool::outputPorts() const {
    QList<PortDescriptor> ports;
    PortDescriptor det;
    det.name   = "detections";
    det.cnName = QStringLiteral("检测结果");
    det.type   = PortType::Points;
    det.dir    = PortDirection::Out;
    det.desc   = QStringLiteral("目标检测结果列表（每项含 classId/className/confidence/bbox）");
    ports << det;
    return ports;
}

QList<PortDescriptor> DetectObjectsDlTool::inputPorts() const {
    QList<PortDescriptor> ports;
    PortDescriptor img;
    img.name   = "image";
    img.cnName = QStringLiteral("输入图像");
    img.type   = PortType::Image;
    img.dir    = PortDirection::In;
    img.desc   = QStringLiteral("待检测的输入图像（可由上游算子提供）");
    ports << img;
    return ports;
}

QJsonObject DetectObjectsDlTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["modelPath"] = m_modelPath;
    obj["confidenceThreshold"] = m_confidenceThreshold;
    obj["nmsThreshold"] = m_nmsThreshold;
    obj["inputWidth"] = m_inputWidth;
    obj["inputHeight"] = m_inputHeight;
    QJsonArray labels;
    for (const auto& label : m_categoryLabels) {
        labels.append(label);
    }
    obj["categoryLabels"] = labels;
    return obj;
}

bool DetectObjectsDlTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("modelPath")) m_modelPath = data["modelPath"].toString();
    if (data.contains("confidenceThreshold")) m_confidenceThreshold = data["confidenceThreshold"].toDouble(0.25);
    if (data.contains("nmsThreshold")) m_nmsThreshold = data["nmsThreshold"].toDouble(0.45);
    if (data.contains("inputWidth")) m_inputWidth = data["inputWidth"].toInt(640);
    if (data.contains("inputHeight")) m_inputHeight = data["inputHeight"].toInt(640);
    if (data.contains("categoryLabels")) {
        QJsonArray arr = data["categoryLabels"].toArray();
        m_categoryLabels.clear();
        for (const auto& v : arr) {
            m_categoryLabels.append(v.toString());
        }
    }

    return configure(data);
}
