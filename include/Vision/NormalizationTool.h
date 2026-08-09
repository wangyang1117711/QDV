#ifndef NORMALIZATIONTOOL_H
#define NORMALIZATIONTOOL_H

#include "Core/VisionTool.h"
#include <QString>
#include <QJsonObject>
#include <vector>

// 均一化算子：为深度学习预处理提供多种数值标准化模式
//
// 支持模式：
//   - minMax      : 最小-最大归一化，将像素值映射到 targetRange 指定区间
//   - zScore      : Z-Score 标准化，(x - mean) / (std + epsilon)
//   - layerNorm   : 层均一化，对每个样本按全部特征维度计算均值/标准差后标准化
//   - instanceNorm: 实例均一化，对每个样本的每个通道独立计算均值/标准差后标准化
//   - batchNorm   : 批均一化（单图场景下按整张图像所有像素计算统计量）
//   - imageNet    : ImageNet 预训练标准化，x/255 后按 ImageNet mean/std 标准化
//
// 输出：
//   - result.overlayImage : 可视化图像（已映射到 [0,255] 的 BGR 图）
//   - result.data         : 包含实际使用的 mean/std/min/max 等元信息
class NormalizationTool : public QDV::VisionTool {
public:
    NormalizationTool();
    ~NormalizationTool() override = default;

    QString type() const override { return "Normalization"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;

    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // 模式常量
    enum class Mode {
        MinMax,
        ZScore,
        LayerNorm,
        InstanceNorm,
        BatchNorm,
        ImageNet
    };

    Mode mode() const { return m_mode; }
    void setMode(Mode mode) { m_mode = mode; }

private:
    // 解析逗号分隔的浮点字符串到向量
    static std::vector<double> parseFloatList(const QString& text);

    // 根据通道数补齐均值/标准差向量（用户只给一个值时自动广播）
    static std::vector<double> broadcastToChannels(const std::vector<double>& values, int channels);

    // 将输入转换为 float32 矩阵
    static cv::Mat toFloat32(const cv::Mat& input);

    // 计算单通道 Mat 的均值与标准差
    static void computeMeanStd(const cv::Mat& mat, double& mean, double& std, double epsilon);

    // 执行具体归一化算法
    bool normalize(const cv::Mat& input, cv::Mat& output, double& outMin, double& outMax);

    // 将浮点结果映射为可显示的 BGR 图像（用于 overlayImage）
    static cv::Mat toDisplayImage(const cv::Mat& normalized);

    Mode m_mode = Mode::MinMax;
    QString m_targetRange = "0_1";      // minMax 模式目标区间："0_1" 或 "minus1_1"
    bool m_perChannel = true;            // 是否在通道维度独立计算统计量
    std::vector<double> m_meanValues;    // 自定义均值（逗号分隔）
    std::vector<double> m_stdValues;     // 自定义标准差（逗号分隔）
    double m_epsilon = 1e-5;             // 防止除零

    // 实际执行时使用的统计量（写入 result.data 供排查）
    std::vector<double> m_usedMean;
    std::vector<double> m_usedStd;
    double m_usedMin = 0.0;
    double m_usedMax = 0.0;
};

#endif // NORMALIZATIONTOOL_H
