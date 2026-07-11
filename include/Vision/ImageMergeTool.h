#ifndef IMAGEMERGETOOL_H
#define IMAGEMERGETOOL_H

#include "VisionTool.h"

class ImageMergeTool : public QDV::VisionTool {
public:
    ImageMergeTool() = default;
    ~ImageMergeTool() override = default;

    QString type() const override { return "ImageMerge"; }
    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    QString m_mergeType = "horizontal";
    // P1-A13 修复：之前 alpha 参数被完全忽略，execute 硬编码 0.5
    double m_alpha = 0.5;
    cv::Mat m_storedImage;
};

#endif // IMAGEMERGETOOL_H