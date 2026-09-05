#ifndef OCRTOOL_H
#define OCRTOOL_H

#include "VisionTool.h"

// OCR 字符识别：基于 cv::dnn CRNN 模型 + CTC 解码，
// 未配置模型时优雅降级（不报错），overlay 显示识别文本。
class OcrTool : public QDV::VisionTool {
public:
    OcrTool();

    QString type() const override { return "Ocr"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // 端口声明（与 config/operators.json 的 outputs 保持一致）
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

private:
    QString m_modelPath;            // ONNX 模型文件路径
    QString m_charsetPath;          // 字符集文件路径（每行一个字符，UTF8）
    int m_imgHeight = 32;           // 网络输入高度
    double m_confThreshold = 0.5;   // 置信度阈值
};

#endif // OCRTOOL_H
