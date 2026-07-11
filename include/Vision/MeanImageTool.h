#ifndef MEANIMAGETOOL_H
#define MEANIMAGETOOL_H

#include "VisionTool.h"

// 均值滤波算子：对灰度图执行 cv::blur
class MeanImageTool : public QDV::VisionTool {
public:
    MeanImageTool();

    QString type() const override { return "MeanImage"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    // 卷积核大小（必须为奇数，范围 1~31）
    int m_kernelSize = 3;
};

#endif // MEANIMAGETOOL_H
