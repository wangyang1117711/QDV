#ifndef CONNECTIONTOOL_H
#define CONNECTIONTOOL_H

#include "VisionTool.h"

// 连通域分析算子：对输入图像二值化后进行连通域标记，
// 过滤面积在 [minArea, maxArea] 范围内的连通域，并以随机颜色标注。
class ConnectionTool : public QDV::VisionTool {
public:
    ConnectionTool();

    QString type() const override { return "Connection"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    double m_threshold = 128.0;     // 二值化阈值，范围 [0, 255]
    int m_connectivity = 8;         // 连通域邻域：4 或 8
    double m_minArea = 10.0;        // 最小面积过滤
    double m_maxArea = 100000.0;    // 最大面积过滤
};

#endif // CONNECTIONTOOL_H
