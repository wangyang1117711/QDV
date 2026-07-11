#include "AngleLlTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

using namespace QDV;

AngleLlTool::AngleLlTool() {
    m_name = "线到线角度";
}

bool AngleLlTool::configure(const QJsonObject& params) {
    if (params.contains("line1P1x")) m_line1P1x = params["line1P1x"].toDouble();
    if (params.contains("line1P1y")) m_line1P1y = params["line1P1y"].toDouble();
    if (params.contains("line1P2x")) m_line1P2x = params["line1P2x"].toDouble();
    if (params.contains("line1P2y")) m_line1P2y = params["line1P2y"].toDouble();
    if (params.contains("line2P1x")) m_line2P1x = params["line2P1x"].toDouble();
    if (params.contains("line2P1y")) m_line2P1y = params["line2P1y"].toDouble();
    if (params.contains("line2P2x")) m_line2P2x = params["line2P2x"].toDouble();
    if (params.contains("line2P2y")) m_line2P2y = params["line2P2y"].toDouble();

    m_params = params;
    return true;
}

bool AngleLlTool::execute(const cv::Mat& input, ToolResult& result) {
    // 输入检查
    if (input.empty()) {
        Logger::error("AngleLlTool: input image is empty");
        result.ok = false;
        result.data["error"] = "Input image is empty";
        return false;
    }

    // 方向向量
    double dx1 = m_line1P2x - m_line1P1x;
    double dy1 = m_line1P2y - m_line1P1y;
    double dx2 = m_line2P2x - m_line2P1x;
    double dy2 = m_line2P2y - m_line2P1y;

    // 退化检查: 任一直线长度为 0 时无法定义方向
    double len1 = std::sqrt(dx1 * dx1 + dy1 * dy1);
    double len2 = std::sqrt(dx2 * dx2 + dy2 * dy2);
    if (len1 < 1e-9 || len2 < 1e-9) {
        Logger::warn("AngleLlTool: degenerate line (zero length)");
        result.ok = false;
        result.data["error"] = "Degenerate line (zero length)";
        return false;
    }

    // 用 atan2 求各自方向角(弧度), 再求差值并归一化到 [0,180]
    double a1 = std::atan2(dy1, dx1);
    double a2 = std::atan2(dy2, dx2);
    double diff = a1 - a2;
    // 归一化到 [-pi, pi]
    while (diff > CV_PI)  diff -= 2.0 * CV_PI;
    while (diff < -CV_PI) diff += 2.0 * CV_PI;
    // 取绝对值, 再保证 <= pi (180°)
    double angleRad = std::fabs(diff);
    if (angleRad > CV_PI) angleRad = 2.0 * CV_PI - angleRad;
    double angleDeg = angleRad * 180.0 / CV_PI;

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

    cv::Point l1p1(cvRound(m_line1P1x), cvRound(m_line1P1y));
    cv::Point l1p2(cvRound(m_line1P2x), cvRound(m_line1P2y));
    cv::Point l2p1(cvRound(m_line2P1x), cvRound(m_line2P1y));
    cv::Point l2p2(cvRound(m_line2P2x), cvRound(m_line2P2y));

    // 两条直线用不同颜色: 线1=绿色, 线2=蓝色
    const cv::Scalar color1(0, 255, 0);   // 绿
    const cv::Scalar color2(255, 0, 0);   // 蓝
    const cv::Scalar white(255, 255, 255);

    cv::line(overlay, l1p1, l1p2, color1, 2, cv::LINE_AA);
    cv::line(overlay, l2p1, l2p2, color2, 2, cv::LINE_AA);
    // 端点标记
    cv::circle(overlay, l1p1, 4, color1, -1, cv::LINE_AA);
    cv::circle(overlay, l1p2, 4, color1, -1, cv::LINE_AA);
    cv::circle(overlay, l2p1, 4, color2, -1, cv::LINE_AA);
    cv::circle(overlay, l2p2, 4, color2, -1, cv::LINE_AA);

    // 角度标签: 标在两线中点的平均位置
    cv::Point mid1((l1p1.x + l1p2.x) / 2, (l1p1.y + l1p2.y) / 2);
    cv::Point mid2((l2p1.x + l2p2.x) / 2, (l2p1.y + l2p2.y) / 2);
    cv::Point labelPos((mid1.x + mid2.x) / 2, (mid1.y + mid2.y) / 2);

    QString label = QString("angle=%1°").arg(angleDeg, 0, 'f', 2);
    int baseLine = 0;
    double fontScale = 0.6;
    int thickness = 1;
    cv::Size text = cv::getTextSize(label.toStdString(),
        cv::FONT_HERSHEY_SIMPLEX, fontScale, thickness, &baseLine);
    cv::Point labelOrg(labelPos.x - text.width / 2,
                       std::max(labelPos.y - 8, text.height + 2));
    // 标签背景框, 提高可读性
    cv::rectangle(overlay,
                  cv::Point(labelOrg.x - 3, labelOrg.y - text.height - 2),
                  cv::Point(labelOrg.x + text.width + 3, labelOrg.y + baseLine + 2),
                  cv::Scalar(0, 0, 0), -1);
    cv::putText(overlay, label.toStdString(), labelOrg,
                cv::FONT_HERSHEY_SIMPLEX, fontScale, white, thickness, cv::LINE_AA);

    result.ok = true;
    result.score = angleDeg;
    result.overlayImage = overlay;

    result.data["angle"] = angleDeg;     // 角度(度)
    result.data["angleRad"] = angleRad;  // 角度(弧度)

    m_results["lastAngle"] = angleDeg;
    m_results["lastAngleRad"] = angleRad;

    Logger::info(QString("AngleLlTool: angle=%1° (%2 rad)")
        .arg(angleDeg, 0, 'f', 2).arg(angleRad, 0, 'f', 4));

    return true;
}

QJsonObject AngleLlTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["line1P1x"] = m_line1P1x;
    obj["line1P1y"] = m_line1P1y;
    obj["line1P2x"] = m_line1P2x;
    obj["line1P2y"] = m_line1P2y;
    obj["line2P1x"] = m_line2P1x;
    obj["line2P1y"] = m_line2P1y;
    obj["line2P2x"] = m_line2P2x;
    obj["line2P2y"] = m_line2P2y;
    return obj;
}

bool AngleLlTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    // 使用 contains 检查,避免缺失字段被覆盖为 0
    if (data.contains("line1P1x")) m_line1P1x = data["line1P1x"].toDouble();
    if (data.contains("line1P1y")) m_line1P1y = data["line1P1y"].toDouble();
    if (data.contains("line1P2x")) m_line1P2x = data["line1P2x"].toDouble();
    if (data.contains("line1P2y")) m_line1P2y = data["line1P2y"].toDouble();
    if (data.contains("line2P1x")) m_line2P1x = data["line2P1x"].toDouble();
    if (data.contains("line2P1y")) m_line2P1y = data["line2P1y"].toDouble();
    if (data.contains("line2P2x")) m_line2P2x = data["line2P2x"].toDouble();
    if (data.contains("line2P2y")) m_line2P2y = data["line2P2y"].toDouble();
    return true;
}
