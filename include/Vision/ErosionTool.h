#ifndef EROSIONTOOL_H
#define EROSIONTOOL_H

#include "VisionTool.h"

// 形态学腐蚀算子：使用结构元素对图像进行腐蚀操作，
// 可消除细小白色噪点、断开细小连接。
class ErosionTool : public QDV::VisionTool {
public:
    ErosionTool();

    QString type() const override { return "Erosion"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    int m_kernelSize = 3;        // 结构元素尺寸（奇数，1~31）
    QString m_kernelShape = "rect"; // 结构元素形状：rect/cross/ellipse
};

#endif // EROSIONTOOL_H
