#ifndef IMAGEPREPROCESSTOOL_H
#define IMAGEPREPROCESSTOOL_H

#include "VisionTool.h"

class ImagePreprocessTool : public QDV::VisionTool {
public:
    ImagePreprocessTool();
    
    QString type() const override { return "ImagePreprocess"; }
    
    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;
    
private:
    bool m_denoise = false;
    QString m_morphology = "none";
    int m_kernelSize = 3;
};

#endif // IMAGEPREPROCESSTOOL_H