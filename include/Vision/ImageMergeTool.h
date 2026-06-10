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
    cv::Mat m_storedImage;
};

#endif // IMAGEMERGETOOL_H