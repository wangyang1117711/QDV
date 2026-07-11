#include "ImagePreprocessTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>

using namespace QDV;

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
        // v2.5.0 修复：防御性校验 kernelSize，避免 OpenCV normalizeAnchor 断言失败
        // 形态学核要求 ksize >= 1 且为奇数；偶数或 0/负数会导致
        // cv::getStructuringElement 创建空 kernel → 触发 anchor.inside() 断言
        int ks = params["kernelSize"].toInt();
        if (ks < 1) {
            ks = 1;
            Logger::warn("ImagePreprocessTool: kernelSize < 1，已钳制为 1");
        } else if (ks > 31) {
            ks = 31;
            Logger::warn("ImagePreprocessTool: kernelSize > 31，已钳制为 31");
        } else if (ks % 2 == 0) {
            // 偶数 → 强制 +1 变奇数（形态学核要求奇数）
            ks += 1;
            Logger::warn(QString("ImagePreprocessTool: kernelSize 为偶数 %1，已调整为奇数 %2")
                         .arg(ks - 1).arg(ks));
        }
        m_kernelSize = ks;
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
        // v2.5.0 修复：运行时双保险，确保 kernelSize >= 1（即使 configure 被绕过也安全）
        int ks = m_kernelSize < 1 ? 1 : m_kernelSize;
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT,
                                                   cv::Size(ks, ks));

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

bool ImagePreprocessTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    m_name = data["name"].toString();
    m_denoise = data["denoise"].toBool();
    m_morphology = data["morphology"].toString();
    // v2.5.0 修复：反序列化也走防御性校验（避免旧方案文件 kernelSize=0）
    QJsonObject params;
    params["kernelSize"] = data["kernelSize"].toInt();
    configure(params);
    return true;
}