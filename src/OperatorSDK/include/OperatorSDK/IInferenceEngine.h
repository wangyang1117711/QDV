#ifndef QDV_IINFERENCE_ENGINE_H
#define QDV_IINFERENCE_ENGINE_H

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QSize>
#include <opencv2/core/mat.hpp>

namespace QDV {

/// 轻量推理指标结构（与 ::InferenceMetrics 字段一致，但定义在 QDV 命名空间内）
/// 目的：避免 #include "AI/InferenceEngine.h" 反向依赖 AI 模块
struct InferenceMetricsLite {
    qint64   preprocessMs = 0;
    qint64   inferenceMs  = 0;
    qint64   postprocessMs = 0;
    qint64   totalMs      = 0;
    QString  backend;
};

/// AI 推理抽象接口（依赖注入锚点）
/// 作用：让 Vision 模块通过接口调用 AI 推理，避免 Vision→AI 反向依赖
class IInferenceEngine {
public:
    virtual ~IInferenceEngine() = default;

    /// 加载模型；签名与 InferenceEngine::loadModel 对齐
    /// std 参数默认 (1,1,1) 表示不做 std 归一化（向后兼容）
    virtual bool loadModel(const QString& modelPath,
                           const QSize& inputSize,
                           const cv::Scalar& mean,
                           double scale,
                           bool swapRB,
                           const cv::Scalar& std = cv::Scalar(1.0, 1.0, 1.0)) = 0;

    /// 预热；与 InferenceEngine::warmUp 对齐
    virtual bool warmUp(int iterations = 3) = 0;

    /// 推理；与 InferenceEngine::infer(const cv::Mat&, QJsonObject&) 对齐
    virtual bool infer(const cv::Mat& input, QJsonObject& result) = 0;

    /// 目标检测推理（YOLOv5/v8/SSD 等）
    /// 输入：图像 + 置信度阈值 + IoU 阈值
    /// 输出：检测结果 JSON（detections 数组，每个含 classId/className/confidence/bbox[x,y,w,h]）
    virtual bool detect(const cv::Mat& input, double confThreshold, double iouThreshold,
                        QJsonObject& result) = 0;

    /// 语义分割推理（UNet/MaskRCNN 等）
    /// 输入：图像
    /// 输出：分割掩膜（单通道 uint8，每个像素值为类别索引）+ 可选的彩色叠加图
    virtual bool segment(const cv::Mat& input, cv::Mat& mask, cv::Mat& overlay,
                         QJsonObject& result) = 0;

    /// 设置类别标签
    virtual void setCategoryLabels(const QStringList& labels) = 0;

    /// 最近一次推理的指标
    virtual InferenceMetricsLite lastMetrics() const = 0;

    /// 返回后端名（"OpenCV_DNN" / "ONNXRuntime"）
    virtual QString backend() const = 0;
};

} // namespace QDV

#endif // QDV_IINFERENCE_ENGINE_H
