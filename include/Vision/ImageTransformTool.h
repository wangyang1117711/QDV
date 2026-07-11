#ifndef IMAGETRANSFORMTOOL_H
#define IMAGETRANSFORMTOOL_H

#include "VisionTool.h"

class ImageTransformTool : public QDV::VisionTool {
public:
    ImageTransformTool() = default;
    ~ImageTransformTool() override = default;

    QString type() const override { return "ImageTransform"; }
    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    QString m_transformType = "resize";
    double m_angle = 0.0;
    double m_scaleX = 1.0;
    double m_scaleY = 1.0;
    int m_flipCode = 0;
    int m_targetWidth = 640;    // 与元数据对齐：100 是开发占位值，640 是标准宽度
    int m_targetHeight = 480;   // 与元数据对齐：100 是开发占位值，480 是标准高度
};

#endif // IMAGETRANSFORMTOOL_H