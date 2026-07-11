#ifndef BOTTOMHATTOOL_H
#define BOTTOMHATTOOL_H

#include "VisionTool.h"

// 形态学底帽变换算子：闭运算结果减去原图，
// 可提取比周围环境更暗的小区域与细节。
class BottomHatTool : public QDV::VisionTool {
public:
    BottomHatTool();

    QString type() const override { return "BottomHat"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    int m_kernelSize = 3;        // 结构元素尺寸（奇数，1~31）
    QString m_kernelShape = "rect"; // 结构元素形状：rect/cross/ellipse
};

#endif // BOTTOMHATTOOL_H
