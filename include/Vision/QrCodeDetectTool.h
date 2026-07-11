#ifndef QRCODEDETECTTOOL_H
#define QRCODEDETECTTOOL_H

#include "VisionTool.h"

// 二维码识别：使用 cv::QRCodeDetector 检测并解码二维码，
// 在 overlay 上绘制四边形角点框与解码文本，未检测到也算执行成功。
class QrCodeDetectTool : public QDV::VisionTool {
public:
    QrCodeDetectTool();

    QString type() const override { return "QrCodeDetect"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;
};

#endif // QRCODEDETECTTOOL_H
