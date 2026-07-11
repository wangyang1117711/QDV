#ifndef MEDIANIMAGETOOL_H
#define MEDIANIMAGETOOL_H

#include "VisionTool.h"

// 中值滤波算子：对灰度图执行 cv::medianBlur
class MedianImageTool : public QDV::VisionTool {
public:
    MedianImageTool();

    QString type() const override { return "MedianImage"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    // 卷积核大小（必须为奇数，范围 1~31）
    int m_kernelSize = 3;
};

#endif // MEDIANIMAGETOOL_H
