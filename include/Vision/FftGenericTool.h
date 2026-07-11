#ifndef FFTGENERICTOOL_H
#define FFTGENERICTOOL_H

#include "VisionTool.h"

// FFT 变换算子：forward=cv::dft, inverse=cv::idft
// 显示幅度谱（logScale=true 时使用 log(1+magnitude) 提升动态范围）
class FftGenericTool : public QDV::VisionTool {
public:
    FftGenericTool();

    QString type() const override { return "FftGeneric"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    // 变换方向：forward（正变换）/ inverse（逆变换）
    QString m_mode = "forward";
    // 是否对幅度谱取对数，便于可视化
    bool m_logScale = true;
};

#endif // FFTGENERICTOOL_H
