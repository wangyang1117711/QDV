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

private:
    QString m_filePath;
    int m_colorMode = 2;
};

#endif // READIMAGETOOL_H