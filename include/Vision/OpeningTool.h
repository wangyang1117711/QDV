#ifndef OPENINGTOOL_H
#define OPENINGTOOL_H

#include "VisionTool.h"

// 形态学开运算算子：先腐蚀后膨胀，
// 可消除细小白色噪点，同时基本保持目标尺寸与位置。
class OpeningTool : public QDV::VisionTool {
public:
    OpeningTool();

    QString type() const override { return "Opening"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    int m_kernelSize = 3;        // 结构元素尺寸（奇数，1~31）
    QString m_kernelShape = "rect"; // 结构元素形状：rect/cross/ellipse
};

#endif // OPENINGTOOL_H
