#ifndef READIMAGETOOL_H
#define READIMAGETOOL_H

#include "Core/VisionTool.h"

class ReadImageTool : public QDV::VisionTool {
public:
    ReadImageTool();
    ~ReadImageTool() override;

    QString type() const override { return "ReadImage"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;

    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    void setFilePath(const QString& path) { m_filePath = path; }
    QString filePath() const { return m_filePath; }

    void setColorMode(int mode) { m_colorMode = mode; }
    int colorMode() const { return m_colorMode; }

    // v5.3：通道分离模式
    void setChannelMode(const QString& mode) { m_channelMode = mode; }
    QString channelMode() const { return m_channelMode; }

private:
    QString m_filePath;
    int m_colorMode = 2;           // 0=unchanged, 1=grayscale, 2=color
    // v5.3：通道分离模式
    // none(默认) / R / G / B / H / S / V / L / a / b / Gray
    // 色彩空间转换：BGR_RGB / RGB_HSV / RGB_Lab / RGB_YUV
    QString m_channelMode = "none";
};

#endif // READIMAGETOOL_H