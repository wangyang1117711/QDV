#ifndef GEOMETRYMEASURETOOL_H
#define GEOMETRYMEASURETOOL_H

#include "VisionTool.h"

class GeometryMeasureTool : public VisionTool {
public:
    GeometryMeasureTool() = default;
    ~GeometryMeasureTool() override = default;

    QString type() const override { return "GeometryMeasure"; }
    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    void deserialize(const QJsonObject& data) override;

private:
    QString m_measureType = "distance";
    double m_minThreshold = 0.0;
    double m_maxThreshold = 1000.0;
    double m_pixelScale = 1.0;

    double measureDistance(const std::vector<cv::Point>& contour) const;
    double measureArea(const std::vector<cv::Point>& contour) const;
    double measurePerimeter(const std::vector<cv::Point>& contour) const;
    double measureAngle(const cv::Vec4i& line1, const cv::Vec4i& line2) const;
    void drawMeasurements(cv::Mat& image, const ToolResult& result) const;
};

#endif // GEOMETRYMEASURETOOL_H