#include "ImagePreprocessTool.h"
#include <opencv2/imgproc.hpp>

ImagePreprocessTool::ImagePreprocessTool() {
    m_name = "图像预处理";
}

bool ImagePreprocessTool::configure(const QJsonObject& params) {
    if (params.contains("denoise")) {
        m_denoise = params["denoise"].toBool();
    }
    if (params.contains("morphology")) {
        m_morphology = params["morphology"].toString();
    }
    if (params.contains("kernelSize")) {
        m_kernelSize = params["kernelSize"].toInt();
    }
    return true;
}

bool ImagePreprocessTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }
    
    cv::Mat output = input.clone();
    
    if (m_denoise) {
        cv::bilateralFilter(output, output, 9, 75, 75);
    }
    
    if (m_morphology != "none") {
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, 
                                                   cv::Size(m_kernelSize, m_kernelSize));
        
        if (m_morphology == "open") {
            cv::morphologyEx(output, output, cv::MORPH_OPEN, kernel);
        } else if (m_morphology == "close") {
            cv::morphologyEx(output, output, cv::MORPH_CLOSE, kernel);
        } else if (m_morphology == "erode") {
            cv::erode(output, output, kernel);
        } else if (m_morphology == "dilate") {
            cv::dilate(output, output, kernel);
        }
    }
    
    result.ok = true;
    result.score = 1.0;
    result.overlayImage = output;
    
    result.data["denoise"] = m_denoise;
    result.data["morphology"] = m_morphology;
    result.data["kernelSize"] = m_kernelSize;
    
    return true;
}

QJsonObject ImagePreprocessTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["denoise"] = m_denoise;
    obj["morphology"] = m_morphology;
    obj["kernelSize"] = m_kernelSize;
    return obj;
}

void ImagePreprocessTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    m_name = data["name"].toString();
    m_denoise = data["denoise"].toBool();
    m_morphology = data["morphology"].toString();
    m_kernelSize = data["kernelSize"].toInt();
}