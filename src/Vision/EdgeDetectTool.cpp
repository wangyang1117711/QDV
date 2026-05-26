#include "EdgeDetectTool.h"
#include <opencv2/imgproc.hpp>

EdgeDetectTool::EdgeDetectTool() {
    m_name = "边缘检测";
}

bool EdgeDetectTool::configure(const QJsonObject& params) {
    if (params.contains("lowThreshold")) {
        m_lowThreshold = params["lowThreshold"].toInt();
    }
    if (params.contains("highThreshold")) {
        m_highThreshold = params["highThreshold"].toInt();
    }
    if (params.contains("apertureSize")) {
        m_apertureSize = params["apertureSize"].toInt();
    }
    return true;
}

bool EdgeDetectTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }
    
    cv::Mat gray, edges;
    
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }
    
    cv::Canny(gray, edges, m_lowThreshold, m_highThreshold, m_apertureSize);
    
    result.overlayImage = input.clone();
    result.overlayImage.setTo(cv::Scalar(0, 255, 0), edges);
    
    int edgeCount = cv::countNonZero(edges);
    result.ok = edgeCount > 0;
    result.score = static_cast<double>(edgeCount) / (edges.rows * edges.cols);
    
    result.data["edgeCount"] = edgeCount;
    result.data["lowThreshold"] = m_lowThreshold;
    result.data["highThreshold"] = m_highThreshold;
    
    return true;
}

QJsonObject EdgeDetectTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["lowThreshold"] = m_lowThreshold;
    obj["highThreshold"] = m_highThreshold;
    obj["apertureSize"] = m_apertureSize;
    return obj;
}

void EdgeDetectTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    m_name = data["name"].toString();
    m_lowThreshold = data["lowThreshold"].toInt();
    m_highThreshold = data["highThreshold"].toInt();
    m_apertureSize = data["apertureSize"].toInt();
}