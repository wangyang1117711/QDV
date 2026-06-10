#ifndef BLOBDETECTTOOL_H
#define BLOBDETECTTOOL_H

#include "VisionTool.h"

class BlobDetectTool : public QDV::VisionTool {
public:
    BlobDetectTool();
    
    QString type() const override { return "BlobDetect"; }
    
    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;
    
private:
    double m_minArea = 10.0;
    double m_maxArea = 1000.0;
    double m_minCircularity = 0.8;
    double m_maxCircularity = 1.0;
};

#endif // BLOBDETECTTOOL_H