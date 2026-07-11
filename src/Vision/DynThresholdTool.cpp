#include "DynThresholdTool.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

using namespace QDV;

DynThresholdTool::DynThresholdTool() {
    m_name = "动态阈值";
}

// 将 blockSize 钳制为 [3, 101] 范围内的奇数
static int clampBlockSize(int bs) {
    if (bs < 3) bs = 3;
    if (bs > 101) bs = 101;
    if (bs % 2 == 0) {
        // 偶数调整为相邻奇数（向上取，仍受上界约束）
        bs += 1;
        if (bs > 101) bs = 101;
    }
    return bs;
}

bool DynThresholdTool::configure(const QJsonObject& params) {
    if (params.contains("blockSize")) m_blockSize = params["blockSize"].toInt();
    if (params.contains("C")) m_C = params["C"].toDouble();
    if (params.contains("method")) {
        // 仅接受合法方法，非法值保持默认
        QString method = params["method"].toString();
        if (method == "mean" || method == "gaussian") {
            m_method = method;
        }
    }

    // 钳制邻域尺寸为合法奇数
    m_blockSize = clampBlockSize(m_blockSize);

    return true;
}

bool DynThresholdTool::execute(const cv::Mat& input, ToolResult& result) {
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

    int adaptiveMethod = (m_method == "gaussian")
            ? cv::ADAPTIVE_THRESH_GAUSSIAN_C
            : cv::ADAPTIVE_THRESH_MEAN_C;

    cv::Mat binary;
    cv::adaptiveThreshold(gray, binary, 255, adaptiveMethod,
                          cv::THRESH_BINARY, m_blockSize, m_C);

    int whiteCount = cv::countNonZero(binary);
    result.ok = true;
    result.score = static_cast<double>(whiteCount) / (binary.rows * binary.cols);

    // overlayImage 必须为 BGR 格式：在原图上以绿色半透明高亮二值前景
    if (input.channels() == 1) {
        cv::cvtColor(input, result.overlayImage, cv::COLOR_GRAY2BGR);
    } else {
        result.overlayImage = input.clone();
    }
    cv::Mat coloredBinary;
    cv::cvtColor(binary, coloredBinary, cv::COLOR_GRAY2BGR);
    coloredBinary.setTo(cv::Scalar(0, 255, 0), binary == 255);
    cv::addWeighted(result.overlayImage, 0.7, coloredBinary, 0.3, 0, result.overlayImage);

    result.data["whitePixels"] = whiteCount;
    result.data["blockSize"] = m_blockSize;
    result.data["C"] = m_C;
    result.data["method"] = m_method;

    return true;
}

QJsonObject DynThresholdTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["blockSize"] = m_blockSize;
    obj["C"] = m_C;
    obj["method"] = m_method;
    return obj;
}

bool DynThresholdTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("blockSize")) m_blockSize = data["blockSize"].toInt();
    if (data.contains("C")) m_C = data["C"].toDouble();
    if (data.contains("method")) {
        QString method = data["method"].toString();
        if (method == "mean" || method == "gaussian") {
            m_method = method;
        }
    }

    // 反序列化后同样钳制，保证状态一致
    m_blockSize = clampBlockSize(m_blockSize);

    return true;
}
