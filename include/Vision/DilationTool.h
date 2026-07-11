#ifndef DILATIONTOOL_H
#define DILATIONTOOL_H

#include "VisionTool.h"

// 形态学膨胀算子：使用结构元素对图像进行膨胀操作，
// 可填充细小孔洞、连接邻近区域。
class DilationTool : public QDV::VisionTool {
public:
    DilationTool();

    QString type() const override { return "Dilation"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    int m_kernelSize = 3;        // 结构元素尺寸（奇数，1~31）
    QString m_kernelShape = "rect"; // 结构元素形状：rect/cross/ellipse
};

#endif // DILATIONTOOL_H
