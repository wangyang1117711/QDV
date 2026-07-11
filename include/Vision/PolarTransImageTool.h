#ifndef POLARTRANSIMAGETOOL_H
#define POLARTRANSIMAGETOOL_H

#include "VisionTool.h"

// 极坐标变换算子：将图像从笛卡尔坐标映射到极坐标空间
class PolarTransImageTool : public QDV::VisionTool {
public:
    PolarTransImageTool();

    QString type() const override { return "PolarTransImage"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    // 极坐标中心 X（-1 表示使用图像中心）
    double m_centerX = -1.0;
    // 极坐标中心 Y（-1 表示使用图像中心）
    double m_centerY = -1.0;
    // 最大半径（-1 表示使用 min(宽, 高) / 2）
    double m_radius = -1.0;
    // 变换模式：linear（线性）/ logarithmic（对数）
    QString m_mode = "linear";
};

#endif // POLARTRANSIMAGETOOL_H
