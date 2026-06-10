#ifndef THRESHOLDTOOL_H
#define THRESHOLDTOOL_H

#include "VisionTool.h"

class ThresholdTool : public QDV::VisionTool {
public:
    ThresholdTool();
    
    QString type() const override { return "Threshold"; }
    
    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;
    
private:
    double m_threshold = 128.0;
    double m_maxValue = 255.0;
    QString m_method = "BINARY";
};

#endif // THRESHOLDTOOL_H