#include "BlobDetectTool.h"
#include <opencv2/features2d.hpp>

BlobDetectTool::BlobDetectTool() {
    m_name = "斑块检测";
}

bool BlobDetectTool::configure(const QJsonObject& params) {
    if (params.contains("minArea")) {
        m_minArea = params["minArea"].toDouble();
    }
    if (params.contains("maxArea")) {
        m_maxArea = params["maxArea"].toDouble();
    }
    if (params.contains("minCircularity")) {
        m_minCircularity = params["minCircularity"].toDouble();
    }
    if (params.contains("maxCircularity")) {
        m_maxCircularity = params["maxCircularity"].toDouble();
    }
    return true;
}

bool BlobDetectTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }
    
    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }
    
    cv::Ptr<cv::SimpleBlobDetector> detector = cv::SimpleBlobDetector::create();
    
    cv::SimpleBlobDetector::Params params;
    params.minArea = m_minArea;
    params.maxArea = m_maxArea;
    params.minCircularity = m_minCircularity;
    params.maxCircularity = m_maxCircularity;
    
    detector->setParams(params);
    
    std::vector<cv::KeyPoint> keypoints;
    detector->detect(gray, keypoints);
    
    result.overlayImage = input.clone();
    cv::drawKeypoints(input, keypoints, result.overlayImage, 
                      cv::Scalar(0, 0, 255), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
    
    result.ok = !keypoints.empty();
    result.score = static_cast<double>(keypoints.size());
    
    QJsonArray blobsArray;
    for (const cv::KeyPoint& kp : keypoints) {
        QJsonObject blobObj;
        blobObj["x"] = kp.pt.x;
        blobObj["y"] = kp.pt.y;
        blobObj["size"] = kp.size;
        blobsArray.append(blobObj);
    }
    result.data["blobs"] = blobsArray;
    result.data["count"] = keypoints.size();
    
    return true;
}

QJsonObject BlobDetectTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["minArea"] = m_minArea;
    obj["maxArea"] = m_maxArea;
    obj["minCircularity"] = m_minCircularity;
    obj["maxCircularity"] = m_maxCircularity;
    return obj;
}

void BlobDetectTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    m_name = data["name"].toString();
    m_minArea = data["minArea"].toDouble();
    m_maxArea = data["maxArea"].toDouble();
    m_minCircularity = data["minCircularity"].toDouble();
    m_maxCircularity = data["maxCircularity"].toDouble();
}