#ifndef WATERSHEDTOOL_H
#define WATERSHEDTOOL_H

#include "VisionTool.h"

// 分水岭分割算子：基于距离变换与标记进行图像区域分割
class WatershedTool : public QDV::VisionTool {
public:
    WatershedTool();

    QString type() const override { return "Watershed"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    // 二值化阈值（有效范围 0 ~ 255）
    double m_threshold = 128.0;
    // 形态学核大小（奇数，有效范围 1 ~ 21）
    int m_kernelSize = 3;
};

#endif // WATERSHEDTOOL_H
