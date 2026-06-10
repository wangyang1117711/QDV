#ifndef AICLASSIFYTOOL_H
#define AICLASSIFYTOOL_H

#include "Core/VisionTool.h"
#include <QJsonArray>

class InferenceEngine;

class AiClassifyTool : public QDV::VisionTool {
public:
    AiClassifyTool();
    ~AiClassifyTool() override;

    QString type() const override { return "AiClassify"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;

    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    void setModelPath(const QString& path) { m_modelPath = path; }
    QString modelPath() const { return m_modelPath; }

    void setCategoryLabels(const QStringList& labels) { m_categoryLabels = labels; }
    QStringList categoryLabels() const { return m_categoryLabels; }

    void setConfidenceThreshold(double threshold) { m_confidenceThreshold = threshold; }
    double confidenceThreshold() const { return m_confidenceThreshold; }

    void setTopK(int k) { m_topK = k; }
    int topK() const { return m_topK; }

    void setInputSize(int width, int height) { m_inputWidth = width; m_inputHeight = height; }
    int inputWidth() const { return m_inputWidth; }
    int inputHeight() const { return m_inputHeight; }

private:
    InferenceEngine* m_engine;
    QString m_modelPath;
    QStringList m_categoryLabels;
    double m_confidenceThreshold = 0.5;
    int m_topK = 3;
    int m_inputWidth = 224;
    int m_inputHeight = 224;
    bool m_warmedUp = false;
};

#endif // AICLASSIFYTOOL_H