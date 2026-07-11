#ifndef EDGESSUBPIXTOOL_H
#define EDGESSUBPIXTOOL_H

#include "VisionTool.h"

// 亚像素边缘检测算子：Canny 提取边缘后，沿梯度方向用抛物线插值
// 细化边缘到亚像素精度，并在 overlay 上用绿色标注边缘点。
class EdgesSubPixTool : public QDV::VisionTool {
public:
    EdgesSubPixTool();

    QString type() const override { return "EdgesSubPix"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    double m_lowThreshold = 50.0;   // Canny 低阈值，范围 [0, 255]
    double m_highThreshold = 150.0; // Canny 高阈值，范围 [0, 255]
    int m_apertureSize = 3;         // Sobel 孔径，仅 3/5/7
};

#endif // EDGESSUBPIXTOOL_H
