#ifndef QDV_OPERATOR_CONTEXT_H
#define QDV_OPERATOR_CONTEXT_H

#include <QJsonObject>
#include <opencv2/core/mat.hpp>
#include "OperatorSDK/IInferenceEngine.h"

namespace QDV {

/// 算子执行上下文（封装输入图像、参数、环境）
class OperatorContext {
public:
    explicit OperatorContext(const cv::Mat& input,
                             const QJsonObject& params = QJsonObject())
        : m_input(input), m_params(params) {}

    const cv::Mat& input() const { return m_input; }
    const QJsonObject& params() const { return m_params; }

    void setInferenceEngine(IInferenceEngine* engine) { m_engine = engine; }
    IInferenceEngine* inferenceEngine() const { return m_engine; }

private:
    cv::Mat             m_input;
    QJsonObject         m_params;
    IInferenceEngine*   m_engine = nullptr;
};

} // namespace QDV

#endif // QDV_OPERATOR_CONTEXT_H
