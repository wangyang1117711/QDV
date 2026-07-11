#ifndef ANGLELLTOOL_H
#define ANGLELLTOOL_H

#include "VisionTool.h"

// 线到线夹角测量算子
// 计算两条直线 (line1: P1->P2, line2: P1->P2) 的夹角, 范围 [0, 180] 度
// 通过方向向量的 atan2 求各自方向角, 取差值并归一化到 [0,180]
class AngleLlTool : public QDV::VisionTool {
public:
    AngleLlTool();

    QString type() const override { return "AngleLl"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    // 第一条直线: (line1P1x, line1P1y) -> (line1P2x, line1P2y)
    double m_line1P1x = 0.0;
    double m_line1P1y = 0.0;
    double m_line1P2x = 0.0;
    double m_line1P2y = 0.0;
    // 第二条直线: (line2P1x, line2P1y) -> (line2P2x, line2P2y)
    double m_line2P1x = 0.0;
    double m_line2P1y = 0.0;
    double m_line2P2x = 0.0;
    double m_line2P2y = 0.0;
};

#endif // ANGLELLTOOL_H
