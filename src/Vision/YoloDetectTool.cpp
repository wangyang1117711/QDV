#include "Vision/YoloDetectTool.h"
#include "Core/Logger.h"
#include <QJsonArray>
#include <opencv2/imgproc.hpp>
#include <cmath>

using namespace QDV;

YoloDetectTool::YoloDetectTool() {
    m_name = "YOLO检测";
    // m_engine 由 setInferenceEngine 外部注入
}

YoloDetectTool::~YoloDetectTool() {
    // m_engine 所有权归外部，不 delete
}

bool YoloDetectTool::configure(const QJsonObject& params) {
    if (params.contains("modelPath")) {
        setModelPath(params["modelPath"].toString());
    }
    if (params.contains("confidenceThreshold")) {
        setConfidenceThreshold(params["confidenceThreshold"].toDouble(0.25));
    }
    if (params.contains("iouThreshold")) {
        setIoUThreshold(params["iouThreshold"].toDouble(0.45));
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

    // 加载模型 + 预热
    if (!m_modelPath.isEmpty() && !m_warmedUp && m_engine) {
        Logger::info("YoloDetectTool loading model: " + m_modelPath);
        bool loaded = m_engine->loadModel(m_modelPath,
            QSize(m_inputWidth, m_inputHeight),
            cv::Scalar(0, 0, 0),       // YOLO 通常无 mean
            1.0 / 255.0,                // scale
            true);                       // swapRB
        if (loaded) {
            m_engine->warmUp(3);
            m_warmedUp = true;
            Logger::info("YoloDetectTool model warmed up successfully");
        } else {
            Logger::error("YoloDetectTool failed to load model: " + m_modelPath);
            return false;
        }
    }

    return true;
}

bool YoloDetectTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        Logger::warn("YoloDetectTool: empty input");
        result.ok = false;
        return false;
    }

    // 引擎未注入保护
    if (!m_engine) {
        Logger::warn("YoloDetectTool: no inference engine injected");
        result.ok = false;
        result.data["error"] = "No inference engine injected";
        result.data["modelLoaded"] = false;
        return false;
    }

    if (m_modelPath.isEmpty()) {
        Logger::warn("YoloDetectTool: no model configured");
        result.ok = false;
        result.data["error"] = "No model configured";
        result.data["modelLoaded"] = false;
        return false;
    }

    // 首次执行时加载模型（如果 configure 未加载）
    if (!m_warmedUp && m_engine) {
        Logger::info("YoloDetectTool: loading model before first inference");
        bool loaded = m_engine->loadModel(m_modelPath,
            QSize(m_inputWidth, m_inputHeight),
            cv::Scalar(0, 0, 0),
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
    result.data["modelLoaded"] = true;

    // 通过 IInferenceEngine::detect 接口执行检测推理
    QJsonObject detectResult;
    bool ok = m_engine->detect(input, m_confThreshold, m_iouThreshold, detectResult);

    if (!ok) {
        Logger::error("YoloDetectTool: detection failed");
        result.ok = false;
        result.data["error"] = "Detection failed";
        return false;
    }

    // 解析检测结果：detections 数组（每项含 classId/className/confidence/bbox[x,y,w,h]）
    QJsonArray detections = detectResult["detections"].toArray();
    int numDetections = detections.size();
    double maxConfidence = 0.0;
    for (const auto& det : detections) {
        double conf = det.toObject()["confidence"].toDouble();
        if (conf > maxConfidence) maxConfidence = conf;
    }

    result.data = detectResult;
    result.data["num_detections"] = numDetections;
    result.data["max_confidence"] = maxConfidence;
    result.data["confidence_threshold"] = m_confThreshold;
    result.data["iou_threshold"] = m_iouThreshold;
    result.data["pass"] = numDetections > 0;

    // 指标
    InferenceMetricsLite metrics = m_engine->lastMetrics();
    result.elapsedMs = metrics.totalMs;
    result.data["preprocess_ms"] = metrics.preprocessMs;
    result.data["inference_ms"] = metrics.inferenceMs;
    result.data["postprocess_ms"] = metrics.postprocessMs;
    result.data["backend"] = metrics.backend;

    // 设置 score 为最大置信度
    result.score = maxConfidence;
    result.ok = true;  // 检测执行成功（即使无检测结果）

    // 绘制检测框到 overlayImage（绿色边界框 + 标签 + 置信度）
    if (numDetections > 0) {
        cv::Mat overlay = input.clone();
        for (const auto& det : detections) {
            QJsonObject d = det.toObject();
            int classId = d["classId"].toInt();
            double conf = d["confidence"].toDouble();
            QJsonArray bbox = d["bbox"].toArray();
            // bbox 格式: [x, y, w, h]（左上角坐标 + 宽高）
            int x = static_cast<int>(bbox[0].toDouble());
            int y = static_cast<int>(bbox[1].toDouble());
            int w = static_cast<int>(bbox[2].toDouble());
            int h = static_cast<int>(bbox[3].toDouble());

            // 钳制到图像边界
            x = std::max(0, std::min(x, overlay.cols - 1));
            y = std::max(0, std::min(y, overlay.rows - 1));
            w = std::min(w, overlay.cols - x);
            h = std::min(h, overlay.rows - y);

            // 绿色边界框
            cv::rectangle(overlay, cv::Point(x, y), cv::Point(x + w, y + h),
                          cv::Scalar(0, 255, 0), 2);

            // 标签文字
            QString className = d["className"].toString();
            if (className.isEmpty()) {
                if (classId >= 0 && classId < m_categoryLabels.size()) {
                    className = m_categoryLabels[classId];
                } else {
                    className = QString("Class_%1").arg(classId);
                }
            }
            QString label = QString("%1: %2%").arg(className)
                .arg(QString::number(conf * 100, 'f', 1));
            std::string labelStr = label.toStdString();

            int baseline = 0;
            double fontScale = 0.5;
            int thickness = 1;
            cv::Size textSize = cv::getTextSize(labelStr, cv::FONT_HERSHEY_SIMPLEX,
                                                 fontScale, thickness, &baseline);

            // 标签背景 + 文字
            int textY = std::max(y - textSize.height - 4, 0);
            cv::rectangle(overlay, cv::Point(x, textY),
                          cv::Point(x + textSize.width + 4, textY + textSize.height + 4),
                          cv::Scalar(0, 255, 0), cv::FILLED);
            cv::putText(overlay, labelStr, cv::Point(x + 2, textY + textSize.height),
                        cv::FONT_HERSHEY_SIMPLEX, fontScale, cv::Scalar(0, 0, 0), thickness);
        }
        result.overlayImage = overlay;
    }

    // 记录最近检测结果
    m_results["lastDetectionCount"] = numDetections;
    m_results["lastMaxConfidence"] = maxConfidence;

    return true;
}

void YoloDetectTool::drawDetections(cv::Mat& overlay, const QJsonArray& detections,
                                     const QStringList& labels, double scaleX, double scaleY) {
    // 为不同类别分配颜色
    static const cv::Scalar colors[] = {
        cv::Scalar(0, 0, 255),     // 红
        cv::Scalar(0, 255, 0),     // 绿
        cv::Scalar(255, 0, 0),     // 蓝
        cv::Scalar(0, 255, 255),   // 黄
        cv::Scalar(255, 0, 255),   // 紫
        cv::Scalar(255, 255, 0),   // 青
        cv::Scalar(128, 0, 0),     // 深蓝
        cv::Scalar(0, 128, 0),     // 深绿
    };
    const int numColors = sizeof(colors) / sizeof(colors[0]);

    for (int i = 0; i < detections.size(); ++i) {
        QJsonObject det = detections[i].toObject();
        int classId = det["class_id"].toInt();
        double conf = det["confidence"].toDouble();
        double cx = det["cx"].toDouble() * scaleX;
        double cy = det["cy"].toDouble() * scaleY;
        double w = det["w"].toDouble() * scaleX;
        double h = det["h"].toDouble() * scaleY;

        // 计算左上角
        int x1 = static_cast<int>(cx - w / 2.0);
        int y1 = static_cast<int>(cy - h / 2.0);
        int x2 = static_cast<int>(cx + w / 2.0);
        int y2 = static_cast<int>(cy + h / 2.0);

        // 钳制到图像边界
        x1 = std::max(0, std::min(x1, overlay.cols - 1));
        y1 = std::max(0, std::min(y1, overlay.rows - 1));
        x2 = std::max(0, std::min(x2, overlay.cols - 1));
        y2 = std::max(0, std::min(y2, overlay.rows - 1));

        // 绘制矩形框
        cv::Scalar color = colors[classId % numColors];
        cv::rectangle(overlay, cv::Point(x1, y1), cv::Point(x2, y2), color, 2);

        // 绘制标签
        QString label;
        if (classId >= 0 && classId < labels.size()) {
            label = QString("%1: %2%").arg(labels[classId])
                .arg(QString::number(conf * 100, 'f', 1));
        } else {
            label = QString("Class_%1: %2%").arg(classId)
                .arg(QString::number(conf * 100, 'f', 1));
        }
        std::string labelStr = label.toStdString();

        int baseline = 0;
        double fontScale = 0.5;
        int thickness = 1;
        cv::Size textSize = cv::getTextSize(labelStr, cv::FONT_HERSHEY_SIMPLEX, fontScale, thickness, &baseline);

        // 标签背景
        int textY = std::max(y1 - textSize.height - 4, 0);
        cv::rectangle(overlay, cv::Point(x1, textY), 
                      cv::Point(x1 + textSize.width + 4, textY + textSize.height + 4),
                      color, cv::FILLED);
        
        // 标签文字
        cv::putText(overlay, labelStr, cv::Point(x1 + 2, textY + textSize.height),
                    cv::FONT_HERSHEY_SIMPLEX, fontScale, cv::Scalar(0, 0, 0), thickness);
    }
}

QJsonObject YoloDetectTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["modelPath"] = m_modelPath;
    obj["confidenceThreshold"] = m_confThreshold;
    obj["iouThreshold"] = m_iouThreshold;
    obj["inputWidth"] = m_inputWidth;
    obj["inputHeight"] = m_inputHeight;
    QJsonArray labels;
    for (const auto& label : m_categoryLabels) {
        labels.append(label);
    }
    obj["categoryLabels"] = labels;
    return obj;
}

bool YoloDetectTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("modelPath")) m_modelPath = data["modelPath"].toString();
    if (data.contains("confidenceThreshold")) m_confThreshold = data["confidenceThreshold"].toDouble(0.25);
    if (data.contains("iouThreshold")) m_iouThreshold = data["iouThreshold"].toDouble(0.45);
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
