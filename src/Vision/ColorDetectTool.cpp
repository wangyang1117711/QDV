#include "ColorDetectTool.h"
#include <opencv2/imgproc.hpp>

using namespace QDV;

ColorDetectTool::ColorDetectTool() {
    m_name = "颜色识别";
}

bool ColorDetectTool::configure(const QJsonObject& params) {
    if (params.contains("hMin")) m_hMin = params["hMin"].toInt();
    if (params.contains("hMax")) m_hMax = params["hMax"].toInt();
    if (params.contains("sMin")) m_sMin = params["sMin"].toInt();
    if (params.contains("sMax")) m_sMax = params["sMax"].toInt();
    if (params.contains("vMin")) m_vMin = params["vMin"].toInt();
    if (params.contains("vMax")) m_vMax = params["vMax"].toInt();
    return true;
}

bool ColorDetectTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }
    
    cv::Mat hsv, mask;
    
    if (input.channels() != 3) {
        result.ok = false;
        return false;
    }
    
    cv::cvtColor(input, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(m_hMin, m_sMin, m_vMin), 
                cv::Scalar(m_hMax, m_sMax, m_vMax), mask);
    
    int pixelCount = cv::countNonZero(mask);
    result.ok = pixelCount > 0;
    result.score = static_cast<double>(pixelCount) / (input.rows * input.cols);
    
    result.overlayImage = input.clone();
    cv::Mat coloredMask;
    cv::cvtColor(mask, coloredMask, cv::COLOR_GRAY2BGR);
    coloredMask.setTo(cv::Scalar(0, 0, 255), mask);
    cv::addWeighted(result.overlayImage, 0.7, coloredMask, 0.3, 0, result.overlayImage);
    
    result.data["pixelCount"] = pixelCount;
    result.data["hMin"] = m_hMin;
    result.data["hMax"] = m_hMax;
    result.data["sMin"] = m_sMin;
    result.data["sMax"] = m_sMax;
    result.data["vMin"] = m_vMin;
    result.data["vMax"] = m_vMax;
    
    return true;
}

QJsonObject ColorDetectTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["hMin"] = m_hMin;
    obj["hMax"] = m_hMax;
    obj["sMin"] = m_sMin;
    obj["sMax"] = m_sMax;
    obj["vMin"] = m_vMin;
    obj["vMax"] = m_vMax;
    return obj;
}

bool ColorDetectTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    m_name = data["name"].toString();
    m_hMin = data["hMin"].toInt();
    m_hMax = data["hMax"].toInt();
    m_sMin = data["sMin"].toInt();
    m_sMax = data["sMax"].toInt();
    m_vMin = data["vMin"].toInt();
    m_vMax = data["vMax"].toInt();
    return true;
}