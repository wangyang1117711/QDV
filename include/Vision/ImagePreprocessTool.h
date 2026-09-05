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
    // P 优化：bilateralFilter 参数可配置（默认与原硬编码值一致）
    int m_bilateralD = 9;
    double m_bilateralSigmaColor = 75.0;
    double m_bilateralSigmaSpace = 75.0;
};

#endif // IMAGEPREPROCESSTOOL_H