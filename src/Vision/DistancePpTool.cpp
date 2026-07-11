#include "DistancePpTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

using namespace QDV;

DistancePpTool::DistancePpTool() {
    m_name = "点到点距离";
}

bool DistancePpTool::configure(const QJsonObject& params) {
    if (params.contains("p1x")) m_p1x = params["p1x"].toDouble();
    if (params.contains("p1y")) m_p1y = params["p1y"].toDouble();
    if (params.contains("p2x")) m_p2x = params["p2x"].toDouble();
    if (params.contains("p2y")) m_p2y = params["p2y"].toDouble();

    if (params.contains("pixelScale")) {
        double s = params["pixelScale"].toDouble();
        // 钳制: 必须 > 0, 否则用默认值
        if (s <= 0.0) {
            Logger::warn("DistancePpTool: invalid pixelScale, fallback to 1.0");
            s = 1.0;
        }
        m_pixelScale = s;
    }

    m_params = params;
    return true;
}

bool DistancePpTool::execute(const cv::Mat& input, ToolResult& result) {
    // 输入检查
    if (input.empty()) {
        Logger::error("DistancePpTool: input image is empty");
        result.ok = false;
        result.data["error"] = "Input image is empty";
        return false;
    }

    // 计算欧氏距离(像素)
    double dx = m_p2x - m_p1x;
    double dy = m_p2y - m_p1y;
    double distancePx = std::sqrt(dx * dx + dy * dy);
    double distanceScaled = distancePx * m_pixelScale;

    // 准备 overlayImage(BGR)
    cv::Mat overlay;
    if (input.channels() == 3) {
        overlay = input.clone();
    } else if (input.channels() == 1) {
        cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
    } else if (input.channels() == 4) {
        cv::cvtColor(input, overlay, cv::COLOR_BGRA2BGR);
    } else {
        overlay = input.clone();
    }

    cv::Point p1(cvRound(m_p1x), cvRound(m_p1y));
    cv::Point p2(cvRound(m_p2x), cvRound(m_p2y));

    // 红色标注两点和连线
    const cv::Scalar red(0, 0, 255);
    const cv::Scalar white(255, 255, 255);

    cv::line(overlay, p1, p2, red, 2, cv::LINE_AA);
    cv::circle(overlay, p1, 5, red, -1, cv::LINE_AA);
    cv::circle(overlay, p2, 5, red, -1, cv::LINE_AA);

    // 距离标签: 标在连线中点上方
    cv::Point mid((p1.x + p2.x) / 2, (p1.y + p2.y) / 2);
    QString label = QString("d=%1 px (%2)")
        .arg(distancePx, 0, 'f', 2)
        .arg(distanceScaled, 0, 'f', 2);
    int baseLine = 0;
    double fontScale = 0.5;
    int thickness = 1;
    cv::Size text = cv::getTextSize(label.toStdString(),
        cv::FONT_HERSHEY_SIMPLEX, fontScale, thickness, &baseLine);
    cv::Point labelOrg(mid.x - text.width / 2,
                       std::max(mid.y - 8, text.height + 2));
    // 标签背景框,提高可读性
    cv::rectangle(overlay,
                  cv::Point(labelOrg.x - 2, labelOrg.y - text.height - 2),
                  cv::Point(labelOrg.x + text.width + 2, labelOrg.y + baseLine + 2),
                  cv::Scalar(0, 0, 0), -1);
    cv::putText(overlay, label.toStdString(), labelOrg,
                cv::FONT_HERSHEY_SIMPLEX, fontScale, white, thickness, cv::LINE_AA);

    result.ok = true;
    result.score = distancePx;
    result.overlayImage = overlay;

    result.data["distance"] = distancePx;             // 像素距离
    result.data["distanceScaled"] = distanceScaled;    // 实际距离(已应用 pixelScale)
    result.data["p1x"] = m_p1x;
    result.data["p1y"] = m_p1y;
    result.data["p2x"] = m_p2x;
    result.data["p2y"] = m_p2y;
    result.data["pixelScale"] = m_pixelScale;

    m_results["lastDistance"] = distancePx;
    m_results["lastDistanceScaled"] = distanceScaled;

    Logger::info(QString("DistancePpTool: d=%1 px (scaled=%2)")
        .arg(distancePx, 0, 'f', 2).arg(distanceScaled, 0, 'f', 2));

    return true;
}

QJsonObject DistancePpTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["p1x"] = m_p1x;
    obj["p1y"] = m_p1y;
    obj["p2x"] = m_p2x;
    obj["p2y"] = m_p2y;
    obj["pixelScale"] = m_pixelScale;
    return obj;
}

bool DistancePpTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    // 使用 contains 检查,避免缺失字段被覆盖为 0
    if (data.contains("p1x")) m_p1x = data["p1x"].toDouble();
    if (data.contains("p1y")) m_p1y = data["p1y"].toDouble();
    if (data.contains("p2x")) m_p2x = data["p2x"].toDouble();
    if (data.contains("p2y")) m_p2y = data["p2y"].toDouble();
    if (data.contains("pixelScale")) {
        double s = data["pixelScale"].toDouble();
        if (s <= 0.0) s = 1.0;
        m_pixelScale = s;
    }
    return true;
}
