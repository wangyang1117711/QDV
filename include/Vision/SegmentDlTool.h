#ifndef SEGMENTDLTOOL_H
#define SEGMENTDLTOOL_H

#include "Core/VisionTool.h"
#include "OperatorSDK/IInferenceEngine.h"
#include <opencv2/dnn.hpp>
#include <QJsonArray>
#include <QMutex>

/// 深度学习语义分割算子（ONNX 格式）
/// 依赖注入：通过 setInferenceEngine 注入 AI 引擎（保持架构一致性）
/// 实际推理：因 IInferenceEngine 接口仅支持分类输出，分割算子直接使用 cv::dnn::Net 推理
/// 模型输出格式：[1, num_classes, H, W]，对每个像素做 argmax 得到类别标签图
class SegmentDlTool : public QDV::VisionTool {
public:
    SegmentDlTool();
    ~SegmentDlTool() override;

    QString type() const override { return "SegmentDl"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;

    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // 端口声明（与 config/operators.json 的 outputs 保持一致）
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

    // ----- 参数 setter/getter -----
    void setModelPath(const QString& path) { m_modelPath = path; }
    QString modelPath() const { return m_modelPath; }

    void setConfidenceThreshold(double threshold) { m_confidenceThreshold = threshold; }
    double confidenceThreshold() const { return m_confidenceThreshold; }

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
    double m_confidenceThreshold = 0.5;           // 置信度阈值（用于过滤低概率像素）
    int m_inputWidth = 512;                       // 模型输入宽度
    int m_inputHeight = 512;                      // 模型输入高度
    QStringList m_categoryLabels;                 // 类别标签列表
    bool m_modelLoaded = false;                   // 模型是否已加载（fallback 路径）
    bool m_warmedUp = false;                      // 引擎分支：模型是否已加载预热
    cv::dnn::Net m_net;                           // OpenCV DNN 网络（用于分割推理）

    // 线程安全：防止 m_modelLoaded 检查-设置竞态与 m_net 并发推理冲突
    mutable QMutex m_execMutex;

    /// 懒加载模型（首次执行时调用）
    bool loadModel();

    /// 分割后处理：argmax 生成类别标签图 + 彩色掩膜
    /// 输出：colorMask（彩色掩膜）、labelMap（类别标签图）
    void postprocess(const cv::Mat& output, const cv::Size& origSize,
                     cv::Mat& colorMask, cv::Mat& labelMap);

    /// 生成类别颜色（固定种子保证一致性）
    static cv::Scalar classColor(int classId);
};

#endif // SEGMENTDLTOOL_H
