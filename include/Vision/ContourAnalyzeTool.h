#ifndef CONTOURANALYZETOOL_H
#define CONTOURANALYZETOOL_H

#include "VisionTool.h"

class ContourAnalyzeTool : public QDV::VisionTool {
public:
    ContourAnalyzeTool();
    
    QString type() const override { return "ContourAnalyze"; }
    
    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;
    
private:
    double m_minArea = 100.0;      // 与元数据对齐：10 过小，100 适配实际轮廓
    double m_maxArea = 100000.0;   // 与元数据对齐：10000 过小，100000 适配大图
    bool m_filterByArea = true;
};

#endif // CONTOURANALYZETOOL_H