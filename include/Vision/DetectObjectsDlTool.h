#ifndef DETECTOBJECTSDLTOOL_H
#define DETECTOBJECTSDLTOOL_H

#include "Core/VisionTool.h"
#include "OperatorSDK/IInferenceEngine.h"
#include <opencv2/dnn.hpp>
#include <QJsonArray>

/// 深度学习目标检测算子（YOLOv5 ONNX 格式）
/// 依赖注入：通过 setInferenceEngine 注入 AI 引擎（保持架构一致性）
/// 实际推理：因 IInferenceEngine 接口仅支持分类输出，检测算子直接使用 cv::dnn::Net 推理
class DetectObjectsDlTool : public QDV::VisionTool {
public:
    DetectObjectsDlTool();
    ~DetectObjectsDlTool() override;

    QString type() const override { return "DetectObjectsDl"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;

    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // ----- 参数 setter/getter -----
    void setModelPath(const QString& path) { m_modelPath = path; }
    QString modelPath() const { return m_modelPath; }

    void setConfidenceThreshold(double threshold) { m_confidenceThreshold = threshold; }
    double confidenceThreshold() const { return m_confidenceThreshold; }

    void setNmsThreshold(double threshold) { m_nmsThreshold = threshold; }
    double nmsThreshold() const { return m_nmsThreshold; }

    void setInputSize(int width, int height) { m_inputWidth = width; m_inputHeight = height; }
    int inputWidth() const { return m_inputWidth; }
    int inputHeight() const { return m_inputHeight; }

    void setCategoryLabels(const QStringList& labels) { m_categoryLabels = labels; }
    QStringList categoryLabels() const { return m_categoryLabels; }

    // 依赖注入：通过 IInferenceEngine 接口注入 AI 引擎（同 AiClassifyTool）
    void setInferenceEngine(QDV::IInferenceEngine* engine) { m_engine = engine; }
    QDV::IInferenceEngine* inferenceEngine() const { return m_engine; }

private:
    QDV::IInferenceEngine* m_engine = nullptr;   // 注入引擎（不拥有所有权）
    QString m_modelPath;
    double m_confidenceThreshold = 0.25;          // 置信度阈值
    double m_nmsThreshold = 0.45;                 // NMS 阈值
    int m_inputWidth = 640;                       // 模型输入宽度
    int m_inputHeight = 640;                      // 模型输入高度
    QStringList m_categoryLabels;                 // 类别标签列表
    bool m_modelLoaded = false;                   // 模型是否已加载
    cv::dnn::Net m_net;                           // OpenCV DNN 网络（用于检测推理）

    /// 懒加载模型（首次执行时调用）
    bool loadModel();

    /// YOLOv5 后处理：解析检测框、NMS、过滤低置信度
    void postprocess(const cv::Mat& output, const cv::Size& origSize,
                     std::vector<int>& classIds,
                     std::vector<float>& confidences,
                     std::vector<cv::Rect>& boxes);
};

#endif // DETECTOBJECTSDLTOOL_H
