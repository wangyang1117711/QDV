#ifndef AFFINETRANSIMAGETOOL_H
#define AFFINETRANSIMAGETOOL_H

#include "VisionTool.h"

// 仿射变换算子：基于旋转矩阵 + 平移实现图像仿射变换
class AffineTransImageTool : public QDV::VisionTool {
public:
    AffineTransImageTool();

    QString type() const override { return "AffineTransImage"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    // 旋转角度（度）
    double m_angle = 0.0;
    // 缩放比例（有效范围 0.01 ~ 100）
    double m_scale = 1.0;
    // X 方向平移量（像素）
    double m_tx = 0.0;
    // Y 方向平移量（像素）
    double m_ty = 0.0;
};

#endif // AFFINETRANSIMAGETOOL_H
