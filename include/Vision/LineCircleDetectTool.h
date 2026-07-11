#ifndef LINECIRCLEDETECTTOOL_H
#define LINECIRCLEDETECTTOOL_H

#include "VisionTool.h"

class LineCircleDetectTool : public QDV::VisionTool {
public:
    LineCircleDetectTool() = default;
    ~LineCircleDetectTool() override = default;

    QString type() const override { return "LineCircleDetect"; }
    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    QString m_detectType = "lineP";   // 与元数据对齐：lineP (HoughLinesP) 能检测线段，更实用
    double m_rho = 1.0;
    double m_theta = CV_PI / 180.0;
    int m_threshold = 100;
    double m_minLineLength = 50.0;
    double m_maxLineGap = 10.0;
    double m_minRadius = 20.0;
    double m_maxRadius = 200.0;
    double m_dp = 1.0;                // 与元数据对齐：1.0 是标准累加器分辨率比
    double m_minDist = 50.0;
    double m_param1 = 150.0;
    double m_param2 = 30.0;
};

#endif // LINECIRCLEDETECTTOOL_H