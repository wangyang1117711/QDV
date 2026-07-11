#ifndef POINTSHARRISTOOL_H
#define POINTSHARRISTOOL_H

#include "VisionTool.h"

// Harris 角点检测算子：计算 Harris 角点响应，归一化到 [0,1] 后按阈值筛选角点，
// 并在 overlay 上用红色圆圈标注。
class PointsHarrisTool : public QDV::VisionTool {
public:
    PointsHarrisTool();

    QString type() const override { return "PointsHarris"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    int m_blockSize = 2;            // 邻域大小，范围 [1, 10]
    int m_ksize = 3;                // Sobel 孔径，奇数 [3, 31]
    double m_k = 0.04;              // Harris 自由参数，范围 [0.01, 0.25]
    double m_threshold = 0.01;      // 角点筛选阈值，范围 [0, 1]
};

#endif // POINTSHARRISTOOL_H
