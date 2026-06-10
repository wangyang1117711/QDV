#ifndef COLORDETECTTOOL_H
#define COLORDETECTTOOL_H

#include "VisionTool.h"

class ColorDetectTool : public QDV::VisionTool {
public:
    ColorDetectTool();
    
    QString type() const override { return "ColorDetect"; }
    
    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;
    
private:
    int m_hMin = 0;
    int m_hMax = 180;
    int m_sMin = 0;
    int m_sMax = 255;
    int m_vMin = 0;
    int m_vMax = 255;
};

#endif // COLORDETECTTOOL_H