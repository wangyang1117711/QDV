#ifndef DISTANCEPPTOOL_H
#define DISTANCEPPTOOL_H

#include "VisionTool.h"

// 点到点距离测量算子
// 计算两点 (p1x,p1y) 与 (p2x,p2y) 的欧氏距离, 支持像素到实际尺寸的缩放
class DistancePpTool : public QDV::VisionTool {
public:
    DistancePpTool();

    QString type() const override { return "DistancePp"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    double m_p1x = 0.0;          // 第一个点 x
    double m_p1y = 0.0;          // 第一个点 y
    double m_p2x = 0.0;          // 第二个点 x
    double m_p2y = 0.0;          // 第二个点 y
    double m_pixelScale = 1.0;   // 像素到实际尺寸的缩放系数(>0)
    // P0-3 扩展：输出单位选择（px/mm），默认 px 保持向后兼容
    QString m_outputUnit = "px";
};

#endif // DISTANCEPPTOOL_H
