#ifndef BARCODE1DTOOL_H
#define BARCODE1DTOOL_H

#include "VisionTool.h"

// 一维码识别：纯 OpenCV 实现线剖面解码，尝试 EAN-13 解码，
// 在 overlay 上绘制条码区域框与解码文本，识别不到也算执行成功。
class Barcode1dTool : public QDV::VisionTool {
public:
    Barcode1dTool();

    QString type() const override { return "Barcode1d"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    QString m_format = "auto";      // 条码格式（占位，目前仅尝试 EAN-13）
};

#endif // BARCODE1DTOOL_H
