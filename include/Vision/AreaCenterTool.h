#ifndef AREACENTERTOOL_H
#define AREACENTERTOOL_H

#include "VisionTool.h"

// 面积中心点（HALCON area_center 风格）：二值化后计算前景区域面积与重心，
// 支持 region/largest/moments 三种模式，在 overlay 上绘制十字标定与文字。
class AreaCenterTool : public QDV::VisionTool {
public:
    AreaCenterTool();

    QString type() const override { return "AreaCenter"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    double m_threshold = 128.0;    // 二值化阈值，范围 [0, 255]
    QString m_mode = "region";     // 模式：region / largest / moments
};

#endif // AREACENTERTOOL_H
