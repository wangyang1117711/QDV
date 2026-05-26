#include "GeometryMeasureTool.h"
#include <cmath>

bool GeometryMeasureTool::configure(const QJsonObject& params) {
    if (!params.contains("measureType")) {
        return false;
    }

    m_measureType = params["measureType"].toString();
    QStringList validTypes = {"distance", "area", "perimeter", "angle", "circleRadius"};
    if (!validTypes.contains(m_measureType)) {
        return false;
    }

    if (params.contains("minThreshold")) {
        m_minThreshold = params["minThreshold"].toDouble();
    }
    if (params.contains("maxThreshold")) {
        m_maxThreshold = params["maxThreshold"].toDouble();
    }
    if (m_minThreshold >= m_maxThreshold) {
        return false;
    }

    if (params.contains("pixelScale")) {
        m_pixelScale = params["pixelScale"].toDouble();
        if (m_pixelScale <= 0.0) {
            return false;
        }
    }

    return true;
}

bool GeometryMeasureTool::execute(const cv::Mat& input, ToolResult& result) {
    result.overlayImage = input.clone();
    result.elapsedMs = 0;

    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }

    cv::Mat binary;
    cv::adaptiveThreshold(gray, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY, 11, 2);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    if (contours.empty()) {
        result.ok = false;
        result.data["error"] = "No contours found";
        return false;
    }

    std::sort(contours.begin(), contours.end(),
              [](const std::vector<cv::Point>& a, const std::vector<cv::Point>& b) {
                  return cv::contourArea(a) > cv::contourArea(b);
              });

    auto& largest = contours[0];
    double value = 0.0;

    if (m_measureType == "distance") {
        value = measureDistance(largest);
        result.data["distance_px"] = value;
        result.data["distance_mm"] = value * m_pixelScale;
    } else if (m_measureType == "area") {
        value = measureArea(largest);
        result.data["area_px"] = value;
        result.data["area_mm2"] = value * m_pixelScale * m_pixelScale;
    } else if (m_measureType == "perimeter") {
        value = measurePerimeter(largest);
        result.data["perimeter_px"] = value;
        result.data["perimeter_mm"] = value * m_pixelScale;
    } else if (m_measureType == "circleRadius") {
        cv::Point2f center;
        float radius;
        cv::minEnclosingCircle(largest, center, radius);
        value = radius;
        result.data["radius_px"] = value;
        result.data["radius_mm"] = value * m_pixelScale;
        result.data["center_x"] = center.x;
        result.data["center_y"] = center.y;
    }

    result.data["measureType"] = m_measureType.c_str();
    result.data["contour_count"] = static_cast<int>(contours.size());
    result.score = value;
    result.ok = (value >= m_minThreshold && value <= m_maxThreshold);
    result.elapsedMs = 0;

    drawMeasurements(result.overlayImage, result);

    return true;
}

double GeometryMeasureTool::measureDistance(const std::vector<cv::Point>& contour) const {
    cv::RotatedRect rect = cv::minAreaRect(contour);
    return std::max(rect.size.width, rect.size.height);
}

double GeometryMeasureTool::measureArea(const std::vector<cv::Point>& contour) const {
    return cv::contourArea(contour);
}

double GeometryMeasureTool::measurePerimeter(const std::vector<cv::Point>& contour) const {
    return cv::arcLength(contour, true);
}

double GeometryMeasureTool::measureAngle(const cv::Vec4i& line1, const cv::Vec4i& line2) const {
    double dx1 = line1[2] - line1[0];
    double dy1 = line1[3] - line1[1];
    double dx2 = line2[2] - line2[0];
    double dy2 = line2[3] - line2[1];

    double dot = dx1 * dx2 + dy1 * dy2;
    double mag1 = std::sqrt(dx1 * dx1 + dy1 * dy1);
    double mag2 = std::sqrt(dx2 * dx2 + dy2 * dy2);

    if (mag1 < 0.0001 || mag2 < 0.0001) {
        return 0.0;
    }

    double cosAngle = dot / (mag1 * mag2);
    cosAngle = std::max(-1.0, std::min(1.0, cosAngle));
    return std::acos(cosAngle) * 180.0 / CV_PI;
}

void GeometryMeasureTool::drawMeasurements(cv::Mat& image, const ToolResult& result) const {
    std::vector<std::vector<cv::Point>> contours;
    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = image.clone();
    }

    cv::Mat binary;
    cv::adaptiveThreshold(gray, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY, 11, 2);
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    if (contours.empty()) return;

    std::sort(contours.begin(), contours.end(),
              [](const std::vector<cv::Point>& a, const std::vector<cv::Point>& b) {
                  return cv::contourArea(a) > cv::contourArea(b);
              });

    cv::Scalar color = result.ok ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
    cv::drawContours(image, contours, 0, color, 2);

    cv::RotatedRect rect = cv::minAreaRect(contours[0]);
    cv::Point2f vertices[4];
    rect.points(vertices);
    for (int i = 0; i < 4; ++i) {
        cv::line(image, vertices[i], vertices[(i + 1) % 4], cv::Scalar(255, 255, 0), 1);
    }

    cv::Moments m = cv::moments(contours[0]);
    if (m.m00 > 0) {
        int cx = static_cast<int>(m.m10 / m.m00);
        int cy = static_cast<int>(m.m01 / m.m00);
        std::string label = m_measureType.toStdString() + ": " +
                           std::to_string(static_cast<int>(result.score)) + "px";
        cv::putText(image, label, cv::Point(cx - 40, cy - 10),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1);
    }
}

QJsonObject GeometryMeasureTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["measureType"] = m_measureType;
    obj["minThreshold"] = m_minThreshold;
    obj["maxThreshold"] = m_maxThreshold;
    obj["pixelScale"] = m_pixelScale;
    return obj;
}

void GeometryMeasureTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    m_measureType = data["measureType"].toString();
    m_minThreshold = data["minThreshold"].toDouble(0.0);
    m_maxThreshold = data["maxThreshold"].toDouble(1000.0);
    m_pixelScale = data["pixelScale"].toDouble(1.0);
}