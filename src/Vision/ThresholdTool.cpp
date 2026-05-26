#include "ThresholdTool.h"
#include <opencv2/imgproc.hpp>

ThresholdTool::ThresholdTool() {
    m_name = "阈值分割";
}

bool ThresholdTool::configure(const QJsonObject& params) {
    if (params.contains("threshold")) {
        m_threshold = params["threshold"].toDouble();
    }
    if (params.contains("maxValue")) {
        m_maxValue = params["maxValue"].toDouble();
    }
    if (params.contains("method")) {
        m_method = params["method"].toString();
    }
    return true;
}

bool ThresholdTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }
    
    cv::Mat gray, binary;
    
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }
    
    int method = cv::THRESH_BINARY;
    if (m_method == "BINARY_INV") {
        method = cv::THRESH_BINARY_INV;
    } else if (m_method == "TRUNC") {
        method = cv::THRESH_TRUNC;
    } else if (m_method == "TOZERO") {
        method = cv::THRESH_TOZERO;
    } else if (m_method == "TOZERO_INV") {
        method = cv::THRESH_TOZERO_INV;
    }
    
    cv::threshold(gray, binary, m_threshold, m_maxValue, method);
    
    int whiteCount = cv::countNonZero(binary);
    result.ok = whiteCount > 0;
    result.score = static_cast<double>(whiteCount) / (binary.rows * binary.cols);
    
    result.overlayImage = input.clone();
    cv::Mat coloredBinary;
    cv::cvtColor(binary, coloredBinary, cv::COLOR_GRAY2BGR);
    coloredBinary.setTo(cv::Scalar(0, 255, 0), binary == 255);
    cv::addWeighted(result.overlayImage, 0.7, coloredBinary, 0.3, 0, result.overlayImage);
    
    result.data["whitePixels"] = whiteCount;
    result.data["threshold"] = m_threshold;
    result.data["method"] = m_method;
    
    return true;
}

QJsonObject ThresholdTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["threshold"] = m_threshold;
    obj["maxValue"] = m_maxValue;
    obj["method"] = m_method;
    return obj;
}

void ThresholdTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    m_name = data["name"].toString();
    m_threshold = data["threshold"].toDouble();
    m_maxValue = data["maxValue"].toDouble();
    m_method = data["method"].toString();
}