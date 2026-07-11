#ifndef CLOSINGTOOL_H
#define CLOSINGTOOL_H

#include "VisionTool.h"

// 形态学闭运算算子：先膨胀后腐蚀，
// 可填充细小黑色孔洞、连接邻近目标，同时基本保持目标尺寸。
class ClosingTool : public QDV::VisionTool {
public:
    ClosingTool();

    QString type() const override { return "Closing"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    int m_kernelSize = 3;        // 结构元素尺寸（奇数，1~31）
    QString m_kernelShape = "rect"; // 结构元素形状：rect/cross/ellipse
};

#endif // CLOSINGTOOL_H
