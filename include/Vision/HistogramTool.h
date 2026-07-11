#ifndef HISTOGRAMTOOL_H
#define HISTOGRAMTOOL_H

#include "VisionTool.h"

// 直方图工具：计算并可视化灰度/彩色/均衡化直方图，
// 修复直方图无法使用问题，生成 256x256 BGR 可视化图作为 overlay。
class HistogramTool : public QDV::VisionTool {
public:
    HistogramTool();

    QString type() const override { return "Histogram"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    QString m_mode = "gray";       // 模式：gray / color / equalize
    int m_bins = 256;              // 直方图 bin 数量
    float m_rangeMin = 0.0f;       // 统计范围下限
    float m_rangeMax = 256.0f;     // 统计范围上限
};

#endif // HISTOGRAMTOOL_H
