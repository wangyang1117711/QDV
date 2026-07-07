#ifndef QDV_INFERENCE_ENGINE_ADAPTER_H
#define QDV_INFERENCE_ENGINE_ADAPTER_H

#include "OperatorSDK/IInferenceEngine.h"
#include "AI/InferenceEngine.h"

namespace QDV {

/// 适配器：将 InferenceEngine 包装为 IInferenceEngine
/// 目的：让 Vision 模块通过接口调用 AI 推理，打破 Vision→AI 反向依赖
/// 生命周期：m_engine 所有权归外部（注入方），适配器不 delete
class InferenceEngineAdapter : public IInferenceEngine {
public:
    /// 构造时传入真实引擎；engine 可为 nullptr（RT-006 引擎未注入场景）
    explicit InferenceEngineAdapter(InferenceEngine* engine)
        : m_engine(engine) {}

    /// 加载模型；m_engine 为 nullptr 时返回 false（RT-006 RT-007）
    bool loadModel(const QString& modelPath,
                   const QSize& inputSize,
                   const cv::Scalar& mean,
                   double scale,
                   bool swapRB) override {
        if (!m_engine) return false;
        return m_engine->loadModel(modelPath, inputSize, mean, scale, swapRB);
    }

    /// 预热；m_engine 为 nullptr 时返回 false
    bool warmUp(int iterations = 3) override {
        if (!m_engine) return false;
        return m_engine->warmUp(iterations);
    }

    /// 推理；m_engine 为 nullptr 时返回 false（RT-006 RT-007）
    bool infer(const cv::Mat& input, QJsonObject& result) override {
        if (!m_engine) return false;
        return m_engine->infer(input, result);
    }

    /// 最近一次推理的指标；m_engine 为 nullptr 时返回默认值
    InferenceMetricsLite lastMetrics() const override {
        if (!m_engine) return InferenceMetricsLite{};
        const ::InferenceMetrics& src = m_engine->lastMetrics();
        InferenceMetricsLite dst;
        dst.preprocessMs  = src.preprocessMs;
        dst.inferenceMs   = src.inferenceMs;
        dst.postprocessMs = src.postprocessMs;
        dst.totalMs       = src.totalMs;
        dst.backend       = src.backend;
        return dst;
    }

    /// 返回后端名（"OpenCV_DNN" / "ONNXRuntime"）；m_engine 为 nullptr 时返回空串
    QString backend() const override {
        if (!m_engine) return QString();
        // InferenceEngine::backend() 返回 Backend 枚举（0=OpenCVDNN, 1=ONNXRuntime）
        switch (m_engine->backend()) {
            case InferenceEngine::BackendOpenCVDNN:   return QStringLiteral("OpenCV_DNN");
            case InferenceEngine::BackendONNXRuntime: return QStringLiteral("ONNXRuntime");
            default: return QStringLiteral("Unknown");
        }
    }

private:
    InferenceEngine* m_engine;  // 不拥有所有权
};

} // namespace QDV

#endif // QDV_INFERENCE_ENGINE_ADAPTER_H
