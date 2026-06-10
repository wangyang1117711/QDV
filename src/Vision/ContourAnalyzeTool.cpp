#include "ContourAnalyzeTool.h"
#include <opencv2/imgproc.hpp>
#include <QJsonArray>

using namespace QDV;

ContourAnalyzeTool::ContourAnalyzeTool() {
    m_name = "轮廓分析";
}

bool ContourAnalyzeTool::configure(const QJsonObject& params) {
    if (params.contains("minArea")) {
        m_minArea = params["minArea"].toDouble();
    }
    if (params.contains("maxArea")) {
        m_maxArea = params["maxArea"].toDouble();
    }
    if (params.contains("filterByArea")) {
        m_filterByArea = params["filterByArea"].toBool();
    }
    return true;
}

bool ContourAnalyzeTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }
    
    cv::Mat gray, binary;
    
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
        cv::threshold(gray, binary, 128, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
    } else {
        gray = input.clone();
        cv::threshold(gray, binary, 128, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
    }
    
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    std::vector<std::vector<cv::Point>> filteredContours;
    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        if (!m_filterByArea || (area >= m_minArea && area <= m_maxArea)) {
            filteredContours.push_back(contour);
        }
    }
    
    result.overlayImage = input.clone();
    cv::drawContours(result.overlayImage, filteredContours, -1, cv::Scalar(0, 255, 0), 2);
    
    result.ok = !filteredContours.empty();
    result.score = static_cast<double>(filteredContours.size());
    
    QJsonArray contoursArray;
    for (const auto& contour : filteredContours) {
        QJsonObject contourObj;
        contourObj["area"] = cv::contourArea(contour);
        contourObj["perimeter"] = cv::arcLength(contour, true);
        contoursArray.append(contourObj);
    }
    result.data["contours"] = contoursArray;
    result.data["count"] = static_cast<int>(filteredContours.size());
    
    return true;
}

QJsonObject ContourAnalyzeTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["minArea"] = m_minArea;
    obj["maxArea"] = m_maxArea;
    obj["filterByArea"] = m_filterByArea;
    return obj;
}

bool ContourAnalyzeTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    m_name = data["name"].toString();
    m_minArea = data["minArea"].toDouble();
    m_maxArea = data["maxArea"].toDouble();
    m_filterByArea = data["filterByArea"].toBool();
    return true;
}