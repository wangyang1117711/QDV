#ifndef SCALEIMAGETOOL_H
#define SCALEIMAGETOOL_H

#include "VisionTool.h"

// 灰度缩放算子：对灰度图执行 cv::convertScaleAbs
// dst = saturate_cast<uchar>(src * scale + offset)
class ScaleImageTool : public QDV::VisionTool {
public:
    ScaleImageTool();

    QString type() const override { return "ScaleImage"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    // 灰度缩放系数（范围 0.01~255）
    double m_scale = 1.0;
    // 灰度偏移量（范围 -255~255）
    double m_offset = 0.0;
};

#endif // SCALEIMAGETOOL_H
