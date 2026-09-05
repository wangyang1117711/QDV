#ifndef YOLODETECTTOOL_H
#define YOLODETECTTOOL_H

#include "Core/VisionTool.h"
#include "OperatorSDK/IInferenceEngine.h"
#include <QJsonArray>
#include <QMutex>

class YoloDetectTool : public QDV::VisionTool {
public:
    YoloDetectTool();
    ~YoloDetectTool() override;

    QString type() const override { return "YoloDetect"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;

    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // 端口声明（与 config/operators.json 的 outputs 保持一致）
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

    // 参数设置器
    void setModelPath(const QString& path) { m_modelPath = path; }
    QString modelPath() const { return m_modelPath; }

    void setConfidenceThreshold(double threshold) { m_confThreshold = threshold; }
    double confidenceThreshold() const { return m_confThreshold; }

    void setIoUThreshold(double threshold) { m_iouThreshold = threshold; }
    double iouThreshold() const { return m_iouThreshold; }

    void setInputSize(int width, int height) { m_inputWidth = width; m_inputHeight = height; }
    int inputWidth() const { return m_inputWidth; }
    int inputHeight() const { return m_inputHeight; }

    void setCategoryLabels(const QStringList& labels) { m_categoryLabels = labels; }
    QStringList categoryLabels() const { return m_categoryLabels; }

    // 依赖注入：通过 IInferenceEngine 接口注入 AI 引擎
    void setInferenceEngine(QDV::IInferenceEngine* engine) { m_engine = engine; }
    QDV::IInferenceEngine* inferenceEngine() const { return m_engine; }

private:
    QDV::IInferenceEngine* m_engine = nullptr;
    QString m_modelPath;
    QStringList m_categoryLabels;
    double m_confThreshold = 0.25;   // YOLO 置信度阈值（默认 0.25）
    double m_iouThreshold = 0.45;    // NMS IoU 阈值（默认 0.45）
    int m_inputWidth = 640;          // YOLO 默认输入尺寸
    int m_inputHeight = 640;
    bool m_warmedUp = false;

    // 线程安全：防止 m_warmedUp 检查-设置竞态（多线程重复 loadModel/warmUp）
    mutable QMutex m_execMutex;

    // 绘制检测框到 overlayImage
    void drawDetections(cv::Mat& overlay, const QJsonArray& detections,
                        const QStringList& labels, double scaleX, double scaleY);
};

#endif // YOLODETECTTOOL_H
