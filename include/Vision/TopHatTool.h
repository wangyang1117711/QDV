#ifndef TOPHATTOOL_H
#define TOPHATTOOL_H

#include "VisionTool.h"

// 形态学顶帽变换算子：原图减去开运算结果，
// 可提取比周围环境更亮的小区域与细节。
class TopHatTool : public QDV::VisionTool {
public:
    TopHatTool();

    QString type() const override { return "TopHat"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    int m_kernelSize = 3;        // 结构元素尺寸（奇数，1~31）
    QString m_kernelShape = "rect"; // 结构元素形状：rect/cross/ellipse
};

#endif // TOPHATTOOL_H
