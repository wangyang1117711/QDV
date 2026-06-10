#ifndef IMAGEARITHMETICTOOL_H
#define IMAGEARITHMETICTOOL_H

#include "VisionTool.h"

class ImageArithmeticTool : public QDV::VisionTool {
public:
    ImageArithmeticTool() = default;
    ~ImageArithmeticTool() override = default;

    QString type() const override { return "ImageArithmetic"; }
    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    QString m_operation = "add";
    double m_scalar = 0.0;
    bool m_useScalar = false;
    cv::Mat m_secondImage;
    bool m_hasSecondImage = false;
};

#endif // IMAGEARITHMETICTOOL_H