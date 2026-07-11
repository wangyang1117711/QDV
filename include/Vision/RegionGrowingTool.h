#ifndef REGIONGROWINGTOOL_H
#define REGIONGROWINGTOOL_H

#include "VisionTool.h"

// 区域生长算子：从种子点出发，按灰度差阈值进行区域生长
class RegionGrowingTool : public QDV::VisionTool {
public:
    RegionGrowingTool();

    QString type() const override { return "RegionGrowing"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    // 种子点 X 坐标
    int m_seedX = 0;
    // 种子点 Y 坐标
    int m_seedY = 0;
    // 灰度差阈值（有效范围 0 ~ 255）
    double m_threshold = 10.0;
    // 连通性：4 或 8
    int m_connectivity = 8;
};

#endif // REGIONGROWINGTOOL_H
