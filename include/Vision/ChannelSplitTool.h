#ifndef CHANNELSPLITTOOL_H
#define CHANNELSPLITTOOL_H

#include "VisionTool.h"

// 图像通道分离算子：支持 RGB/HSV/Lab/YUV 四种颜色空间的通道分离
// 输入：彩色图像（BGR 格式）
// 输出：指定颜色空间的指定通道（单通道转 BGR 作为 overlay）
class ChannelSplitTool : public QDV::VisionTool {
public:
    ChannelSplitTool();

    QString type() const override { return "ChannelSplit"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    QString m_colorSpace = "RGB";  // 颜色空间：RGB / HSV / Lab / YUV
    int m_channelIndex = 0;        // 通道索引：0/1/2
};

#endif // CHANNELSPLITTOOL_H
