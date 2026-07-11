#include "AreaCenterTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

using namespace QDV;

namespace {
constexpr double kThresholdMin = 0.0;
constexpr double kThresholdMax = 255.0;
}

AreaCenterTool::AreaCenterTool() {
    m_name = "面积中心点";
}

bool AreaCenterTool::configure(const QJsonObject& params) {
    if (params.contains("threshold")) {
        const double v = params["threshold"].toDouble();
        if (v < kThresholdMin || v > kThresholdMax) {
            Logger::warn(QString("AreaCenterTool: threshold %1 超出范围 [%2,%3]，已钳制")
                .arg(v).arg(kThresholdMin).arg(kThresholdMax));
            m_threshold = std::clamp(v, kThresholdMin, kThresholdMax);
        } else {
            m_threshold = v;
        }
    }
    if (params.contains("mode")) {
        const QString v = params["mode"].toString();
        if (v == "region" || v == "largest" || v == "moments") {
            m_mode = v;
        } else {
            Logger::warn(QString("AreaCenterTool: mode '%1' 非法，回退为 'region'").arg(v));
            m_mode = "region";
        }
    }
    return true;
}

bool AreaCenterTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // 转灰度
    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }

    // 二值化
    cv::Mat binary;
    cv::threshold(gray, binary, m_threshold, 255, cv::THRESH_BINARY);

    // overlay 必须为 BGR
    cv::Mat overlay;
    if (input.channels() == 1) {
        cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
    } else {
        overlay = input.clone();
    }

    double area = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    int count = 0;

    if (m_mode == "largest") {
        // 取面积最大的外轮廓
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        if (contours.empty()) {
            result.ok = false;
            result.data["error"] = "no foreground";
            result.overlayImage = overlay;
            return false;
        }
        int maxIdx = 0;
        double maxArea = 0.0;
        for (int i = 0; i < static_cast<int>(contours.size()); ++i) {
            const double a = cv::contourArea(contours[i]);
            if (a > maxArea) {
                maxArea = a;
                maxIdx = i;
            }
        }
        area = maxArea;
        const cv::Moments mu = cv::moments(contours[maxIdx]);
        if (mu.m00 > 1e-9) {
            cx = mu.m10 / mu.m00;
            cy = mu.m01 / mu.m00;
        }
        count = 1;
    } else {
        // region / moments 模式：对整个二值前景计算
        const cv::Moments mu = cv::moments(binary, true);
        area = mu.m00;
        if (mu.m00 > 1e-9) {
            cx = mu.m10 / mu.m00;
            cy = mu.m01 / mu.m00;
        }
        count = cv::countNonZero(binary);
    }

    // 无前景（面积 < 1）：算子执行失败
    if (area < 1.0) {
        Logger::warn("AreaCenterTool: 二值化后无前景目标");
        result.ok = false;
        result.data["error"] = "no target";
        result.data["area"] = area;
        result.overlayImage = overlay;
        return false;
    }

    // 绘制十字线（绿色）
    const int xi = static_cast<int>(std::round(cx));
    const int yi = static_cast<int>(std::round(cy));
    const int armLen = 20;
    cv::line(overlay, cv::Point(xi - armLen, yi), cv::Point(xi + armLen, yi),
             cv::Scalar(0, 255, 0), 2);
    cv::line(overlay, cv::Point(xi, yi - armLen), cv::Point(xi, yi + armLen),
             cv::Scalar(0, 255, 0), 2);
    cv::circle(overlay, cv::Point(xi, yi), 3, cv::Scalar(0, 255, 0), -1);

    // 文字标注
    const QString txt = QString("area=%1 center=(%2,%3)")
        .arg(area, 0, 'f', 1).arg(cx, 0, 'f', 1).arg(cy, 0, 'f', 1);
    cv::putText(overlay, txt.toStdString(), cv::Point(10, 25),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);

    result.overlayImage = overlay;
    result.ok = true;
    result.data["area"] = area;
    result.data["row"] = cy;        // 行 = y
    result.data["column"] = cx;     // 列 = x
    result.data["count"] = count;
    return true;
}

QJsonObject AreaCenterTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["threshold"] = m_threshold;
    obj["mode"] = m_mode;
    return obj;
}

bool AreaCenterTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("threshold")) {
        const double v = data["threshold"].toDouble();
        m_threshold = std::clamp(v, kThresholdMin, kThresholdMax);
    }
    if (data.contains("mode")) {
        const QString v = data["mode"].toString();
        if (v == "region" || v == "largest" || v == "moments") {
            m_mode = v;
        }
    }
    return true;
}
