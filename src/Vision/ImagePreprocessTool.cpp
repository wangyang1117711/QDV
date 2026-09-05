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
    // P 优化：bilateralFilter 参数可配（默认 d=9 / sigmaColor=75 / sigmaSpace=75）
    // 软边场景（金属件/纹理件）建议 sigmaColor=50~80，sigmaSpace 同步
    if (params.contains("bilateralD")) {
        int d = params["bilateralD"].toInt();
        if (d < 1) d = 1;
        if (d % 2 == 0) d += 1;       // bilateral d 要求正奇数
        if (d > 31) d = 31;
        m_bilateralD = d;
    }
    if (params.contains("bilateralSigmaColor")) {
        double s = params["bilateralSigmaColor"].toDouble();
        if (s < 1.0) s = 1.0;
        if (s > 200.0) s = 200.0;
        m_bilateralSigmaColor = s;
    }
    if (params.contains("bilateralSigmaSpace")) {
        double s = params["bilateralSigmaSpace"].toDouble();
        if (s < 1.0) s = 1.0;
        if (s > 200.0) s = 200.0;
        m_bilateralSigmaSpace = s;
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
        // P 优化：bilateral 参数可配置（默认 d=9/σc=75/σs=75，向后兼容）
        cv::bilateralFilter(output, output, m_bilateralD,
                            m_bilateralSigmaColor, m_bilateralSigmaSpace);
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
    // 输出实际使用的 bilateral 参数，便于追溯
    result.data["bilateralD"] = m_bilateralD;
    result.data["bilateralSigmaColor"] = m_bilateralSigmaColor;
    result.data["bilateralSigmaSpace"] = m_bilateralSigmaSpace;

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
    // P 优化：序列化 bilateral 参数（往返一致）
    obj["bilateralD"] = m_bilateralD;
    obj["bilateralSigmaColor"] = m_bilateralSigmaColor;
    obj["bilateralSigmaSpace"] = m_bilateralSigmaSpace;
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
    if (data.contains("bilateralD"))             params["bilateralD"]             = data["bilateralD"];
    if (data.contains("bilateralSigmaColor"))    params["bilateralSigmaColor"]    = data["bilateralSigmaColor"];
    if (data.contains("bilateralSigmaSpace"))    params["bilateralSigmaSpace"]    = data["bilateralSigmaSpace"];
    configure(params);
    return true;
}