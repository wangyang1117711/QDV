#ifndef DYNTHRESHOLDTOOL_H
#define DYNTHRESHOLDTOOL_H

#include "VisionTool.h"

// 动态阈值算子：基于局部邻域自适应阈值进行二值化分割
class DynThresholdTool : public QDV::VisionTool {
public:
    DynThresholdTool();

    QString type() const override { return "DynThreshold"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    // 局部邻域尺寸（奇数，有效范围 3 ~ 101）
    int m_blockSize = 15;
    // 自适应阈值常数偏移 C
    double m_C = 2.0;
    // 自适应方法：mean（均值）/ gaussian（高斯）
    QString m_method = "mean";
};

#endif // DYNTHRESHOLDTOOL_H
