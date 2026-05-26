#ifndef CONTOURANALYZETOOL_H
#define CONTOURANALYZETOOL_H

#include "VisionTool.h"

class ContourAnalyzeTool : public VisionTool {
public:
    ContourAnalyzeTool();
    
    QString type() const override { return "ContourAnalyze"; }
    
    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    void deserialize(const QJsonObject& data) override;
    
private:
    double m_minArea = 10.0;
    double m_maxArea = 10000.0;
    bool m_filterByArea = true;
};

#endif // CONTOURANALYZETOOL_H