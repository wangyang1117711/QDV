#ifndef GAUSSFILTERTOOL_H
#define GAUSSFILTERTOOL_H

#include "VisionTool.h"

// 高斯滤波算子：对灰度图执行 cv::GaussianBlur
class GaussFilterTool : public QDV::VisionTool {
public:
    GaussFilterTool();

    QString type() const override { return "GaussFilter"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    // 卷积核大小（必须为奇数，范围 1~31）
    int m_kernelSize = 3;
    // X 方向高斯标准差，0.0 表示由 kernelSize 自动推算
    double m_sigmaX = 0.0;
    // Y 方向高斯标准差，0.0 表示与 sigmaX 相同
    double m_sigmaY = 0.0;
};

#endif // GAUSSFILTERTOOL_H
