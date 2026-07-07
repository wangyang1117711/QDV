#ifndef QDV_HISTOGRAM_OPERATOR_H
#define QDV_HISTOGRAM_OPERATOR_H

#include "OperatorSDK/IOperator.h"
#include <opencv2/imgproc.hpp>

// Windows 动态库导出宏
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#   define QDV_EXPORT Q_DECL_EXPORT
#else
#   define QDV_EXPORT __attribute__((visibility("default")))
#endif

namespace QDV {

/// 直方图算子（实现 IOperator 接口）
/// 支持 3 种模式：
/// - gray:     灰度直方图（cv::calcHist）
/// - color:    彩色直方图（BGR 三通道分别 calcHist）
/// - equalize: 直方图均衡化（cv::equalizeHist）
class HistogramOperator : public IOperator {
public:
    HistogramOperator() { m_name = "直方图工具"; }
    ~HistogramOperator() override = default;

    QString type() const override { return "Histogram"; }
    QString version() const override { return "1.0.0"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;

    IOperator* clone() const override { return new HistogramOperator(*this); }

    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    /// 执行灰度直方图计算
    bool executeGray(const cv::Mat& input, ToolResult& result);
    /// 执行彩色直方图计算
    bool executeColor(const cv::Mat& input, ToolResult& result);
    /// 执行直方图均衡化
    bool executeEqualize(const cv::Mat& input, ToolResult& result);

    QString m_mode = "gray";       ///< 模式：gray/color/equalize
    int     m_bins = 256;          ///< 直方图箱数
    float   m_rangeMin = 0.0f;     ///< 统计范围下界
    float   m_rangeMax = 256.0f;   ///< 统计范围上界
};

} // namespace QDV

// 动态库导出 C API（供 OperatorPluginLoader 加载）
extern "C" {
    QDV_EXPORT const char* operator_type();
    QDV_EXPORT const char* operator_version();
    QDV_EXPORT QDV::IOperator* create_operator();
}

#endif // QDV_HISTOGRAM_OPERATOR_H
