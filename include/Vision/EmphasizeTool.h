#ifndef EMPHASIZETOOL_H
#define EMPHASIZETOOL_H

#include "VisionTool.h"

// 锐化增强算子：先高斯模糊再做反锐化掩模
// result = input * (1 + amount/100) - blurred * (amount/100)
class EmphasizeTool : public QDV::VisionTool {
public:
    EmphasizeTool();

    QString type() const override { return "Emphasize"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    // 高斯模糊卷积核大小（必须为奇数，范围 1~31）
    int m_kernelSize = 3;
    // 锐化强度（百分比，范围 0~20）
    double m_amount = 7.0;
};

#endif // EMPHASIZETOOL_H
