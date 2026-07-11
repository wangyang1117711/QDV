#include "PolarTransImageTool.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

using namespace QDV;

PolarTransImageTool::PolarTransImageTool() {
    m_name = "极坐标变换";
}

bool PolarTransImageTool::configure(const QJsonObject& params) {
    if (params.contains("centerX")) m_centerX = params["centerX"].toDouble();
    if (params.contains("centerY")) m_centerY = params["centerY"].toDouble();
    if (params.contains("radius")) m_radius = params["radius"].toDouble();
    if (params.contains("mode")) {
        // 仅接受合法模式，非法值保持默认
        QString mode = params["mode"].toString();
        if (mode == "linear" || mode == "logarithmic") {
            m_mode = mode;
        }
    }
    return true;
}

bool PolarTransImageTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // 解析极坐标中心：-1 表示使用图像中心
    cv::Point2f center;
    center.x = (m_centerX < 0.0) ? (input.cols / 2.0f) : static_cast<float>(m_centerX);
    center.y = (m_centerY < 0.0) ? (input.rows / 2.0f) : static_cast<float>(m_centerY);

    // 解析最大半径：-1 表示使用 min(宽, 高) / 2
    double maxRadius = (m_radius < 0.0)
            ? (std::min(input.cols, input.rows) / 2.0)
            : m_radius;
    if (maxRadius <= 0.0) {
        result.ok = false;
        result.data["error"] = QStringLiteral("无效的变换半径");
        return false;
    }

    // 输出尺寸：宽度对应半径，高度对应 360 度角度分辨率
    cv::Size dSize(static_cast<int>(std::round(maxRadius)), 360);

    int flag = (m_mode == "logarithmic")
            ? cv::WARP_POLAR_LOG
            : cv::WARP_POLAR_LINEAR;

    cv::Mat output;
    cv::warpPolar(input, output, dSize, center, maxRadius, flag);

    // overlayImage 必须为 BGR 格式
    if (output.channels() == 1) {
        cv::cvtColor(output, result.overlayImage, cv::COLOR_GRAY2BGR);
    } else {
        result.overlayImage = output.clone();
    }

    result.ok = !output.empty();
    result.score = result.ok ? 1.0 : 0.0;
    result.data["centerX"] = static_cast<double>(center.x);
    result.data["centerY"] = static_cast<double>(center.y);
    result.data["radius"] = maxRadius;
    result.data["mode"] = m_mode;
    result.data["outputWidth"] = output.cols;
    result.data["outputHeight"] = output.rows;

    return result.ok;
}

QJsonObject PolarTransImageTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["centerX"] = m_centerX;
    obj["centerY"] = m_centerY;
    obj["radius"] = m_radius;
    obj["mode"] = m_mode;
    return obj;
}

bool PolarTransImageTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("centerX")) m_centerX = data["centerX"].toDouble();
    if (data.contains("centerY")) m_centerY = data["centerY"].toDouble();
    if (data.contains("radius")) m_radius = data["radius"].toDouble();
    if (data.contains("mode")) {
        QString mode = data["mode"].toString();
        if (mode == "linear" || mode == "logarithmic") {
            m_mode = mode;
        }
    }
    return true;
}
