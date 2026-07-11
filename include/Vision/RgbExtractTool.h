#ifndef RGBEXTRACTTOOL_H
#define RGBEXTRACTTOOL_H

#include "VisionTool.h"

// RGB 通道提取：从 BGR 图像中提取指定通道（r/g/b），
// 单通道转 BGR 后作为 overlay 返回，并输出通道均值。
class RgbExtractTool : public QDV::VisionTool {
public:
    RgbExtractTool();

    QString type() const override { return "RgbExtract"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    QString m_channel = "r";       // 待提取通道：r / g / b
};

#endif // RGBEXTRACTTOOL_H
