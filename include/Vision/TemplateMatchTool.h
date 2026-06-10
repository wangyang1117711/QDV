#ifndef TEMPLATEMATCHTOOL_H
#define TEMPLATEMATCHTOOL_H

#include "VisionTool.h"
#include <QFile>

class TemplateMatchTool : public QDV::VisionTool {
public:
    TemplateMatchTool();
    
    QString type() const override { return "TemplateMatch"; }
    
    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;
    
    bool validateTemplatePath(const QString& path);
    bool loadTemplate();
    
private:
    QString m_templatePath;
    cv::Mat m_template;
    double m_threshold = 0.8;
    QString m_matchMethod = "SQDIFF";
};

#endif // TEMPLATEMATCHTOOL_H