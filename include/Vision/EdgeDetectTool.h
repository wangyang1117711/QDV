#ifndef EDGEDETECTTOOL_H
#define EDGEDETECTTOOL_H

#include "VisionTool.h"

class EdgeDetectTool : public VisionTool {
public:
    EdgeDetectTool();
    
    QString type() const override { return "EdgeDetect"; }
    
    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    void deserialize(const QJsonObject& data) override;
    
private:
    int m_lowThreshold = 50;
    int m_highThreshold = 150;
    int m_apertureSize = 3;
};

#endif // EDGEDETECTTOOL_H