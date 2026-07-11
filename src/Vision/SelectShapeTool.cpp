#include "SelectShapeTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <QJsonArray>
#include <algorithm>

using namespace QDV;

namespace {
constexpr double kThresholdMin = 0.0;
constexpr double kThresholdMax = 255.0;
}

SelectShapeTool::SelectShapeTool() {
    m_name = "形状选择";
}

bool SelectShapeTool::configure(const QJsonObject& params) {
    if (params.contains("threshold")) {
        const double v = params["threshold"].toDouble();
        if (v < kThresholdMin || v > kThresholdMax) {
            Logger::warn(QString("SelectShapeTool: threshold %1 超出有效范围 [%2,%3]，已钳制")
                .arg(v).arg(kThresholdMin).arg(kThresholdMax));
            m_threshold = std::clamp(v, kThresholdMin, kThresholdMax);
        } else {
            m_threshold = v;
        }
    }
    if (params.contains("featureType")) {
        m_featureType = params["featureType"].toString();
    }
    if (params.contains("minValue")) {
        m_minValue = params["minValue"].toDouble();
    }
    if (params.contains("maxValue")) {
        m_maxValue = params["maxValue"].toDouble();
    }
    if (m_minValue > m_maxValue) {
        Logger::warn(QString("SelectShapeTool: minValue(%1) > maxValue(%2)，已交换")
            .arg(m_minValue).arg(m_maxValue));
        std::swap(m_minValue, m_maxValue);
    }
    return true;
}

// 计算轮廓的形状特征值
// 各特征定义（参考 HALCON select_shape）：
//   area           : 轮廓面积（像素）
//   width          : 外接矩形宽度
//   height         : 外接矩形高度
//   ratio          : 长宽比 = max(w,h)/min(w,h)，范围 [1, ∞)
//   rectangularity : 矩形度 = area / (w*h)，范围 (0, 1]
//   circularity    : 圆形度 = 4π·area / perimeter²，范围 [0, 1]
//   compactness    : 紧凑度 = perimeter² / (4π·area)，范围 [1, ∞)，与 circularity 互为倒数
double SelectShapeTool::computeFeature(const std::vector<cv::Point>& contour,
                                        double area,
                                        const QString& featureType) {
    if (featureType == "area") {
        return area;
    }
    const cv::Rect bbox = cv::boundingRect(contour);
    const double w = static_cast<double>(bbox.width);
    const double h = static_cast<double>(bbox.height);
    if (featureType == "width")  return w;
    if (featureType == "height") return h;
    if (featureType == "ratio") {
        const double mn = std::min(w, h);
        if (mn < 1e-6) return 0.0;
        return std::max(w, h) / mn;
    }
    if (featureType == "rectangularity") {
        const double wh = w * h;
        if (wh < 1e-6) return 0.0;
        return area / wh;
    }
    if (featureType == "circularity" || featureType == "compactness") {
        const double perimeter = cv::arcLength(contour, true);
        if (perimeter < 1e-6 || area < 1e-6) return 0.0;
        const double c = 4.0 * CV_PI * area / (perimeter * perimeter);
        if (featureType == "circularity") {
            return std::clamp(c, 0.0, 1.0);
        } else {
            // compactness = 1/circularity，范围 [1, ∞)
            return c > 1e-6 ? (1.0 / c) : 1e9;
        }
    }
    return -1.0;  // 未知特征类型
}

bool SelectShapeTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    cv::Mat gray, binary;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }

    // 二值化
    cv::threshold(gray, binary, m_threshold, 255, cv::THRESH_BINARY);

    // 提取连通域轮廓
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // overlay 必须为 BGR
    cv::Mat overlay;
    if (input.channels() == 1) {
        cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
    } else {
        overlay = input.clone();
    }

    // 按特征筛选轮廓
    std::vector<std::vector<cv::Point>> filteredContours;
    std::vector<cv::Rect> bboxes;

    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        const double feature = computeFeature(contour, area, m_featureType);
        if (feature < 0) continue;  // 无效特征类型

        if (feature < m_minValue || feature > m_maxValue) {
            continue;
        }

        filteredContours.push_back(contour);
        bboxes.push_back(cv::boundingRect(contour));
    }

    // 绘制符合条件的轮廓（绿色）及外接矩形（蓝色）
    cv::drawContours(overlay, filteredContours, -1, cv::Scalar(0, 255, 0), 2);
    for (const auto& bbox : bboxes) {
        cv::rectangle(overlay, bbox, cv::Scalar(255, 128, 0), 1);
    }

    result.overlayImage = overlay;
    result.ok = true;
    result.score = static_cast<double>(filteredContours.size());

    result.data["count"] = static_cast<int>(filteredContours.size());
    result.data["threshold"] = m_threshold;
    result.data["featureType"] = m_featureType;

    return true;
}

QJsonObject SelectShapeTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["threshold"] = m_threshold;
    obj["featureType"] = m_featureType;
    obj["minValue"] = m_minValue;
    obj["maxValue"] = m_maxValue;
    return obj;
}

bool SelectShapeTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) {
        m_id = data["id"].toString();
    }
    if (data.contains("name")) {
        m_name = data["name"].toString();
    }
    if (data.contains("threshold")) {
        const double v = data["threshold"].toDouble();
        m_threshold = std::clamp(v, kThresholdMin, kThresholdMax);
    }
    if (data.contains("featureType")) {
        m_featureType = data["featureType"].toString();
    }
    if (data.contains("minValue")) {
        m_minValue = data["minValue"].toDouble();
    }
    if (data.contains("maxValue")) {
        m_maxValue = data["maxValue"].toDouble();
    }
    if (m_minValue > m_maxValue) {
        std::swap(m_minValue, m_maxValue);
    }
    return true;
}
