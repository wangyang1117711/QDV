#ifndef EVALUATIONENGINE_H
#define EVALUATIONENGINE_H

// ============================================================================
// M5-3: 评估体系引擎
//
// 功能：
//   1. 加载测试数据集（图像 + 标注）
//   2. 运行推理并计算评估指标
//   3. 生成评估报告（JSON/文本格式）
//
// 评估指标：
//   - 分类：Accuracy, Precision, Recall, F1
//   - 异常检测：AUROC, FPR, TPR
//   - 性能：平均延迟, P50/P99延迟, 吞吐量(FPS)
// ============================================================================

#include <QObject>
#include <QString>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <opencv2/opencv.hpp>
#include "ZeroShotEngine.h"
#include "PipelineEngine.h"
#include "ZeroShotTypes.h"

namespace zsu {

// 测试样本（单张图像 + 标注）
struct EvalSample {
    QString imagePath;          // 图像路径
    cv::Mat image;              // 图像数据
    bool isAnomaly = false;     // 是否为异常样本（ground truth）
    float anomalyScore = 0.0f;  // 真实异常分数（如果有）
    QString category;           // 真实类别
    QString predictedCategory;  // 预测类别
    float predictedScore = 0.0f; // 预测异常分数
    qint64 latencyMs = 0;       // 推理延迟
    bool correct = false;       // 预测是否正确
};

// 评估指标
struct EvalMetrics {
    // --- 分类指标 ---
    int totalSamples = 0;
    int correctPredictions = 0;
    double accuracy = 0.0;      // 准确率
    int truePositives = 0;      // 真阳性
    int falsePositives = 0;     // 假阳性
    int trueNegatives = 0;      // 真阴性
    int falseNegatives = 0;     // 假阴性
    double precision = 0.0;     // 精确率
    double recall = 0.0;        // 召回率
    double f1Score = 0.0;       // F1分数

    // --- 异常检测指标 ---
    double auroc = 0.0;         // ROC曲线下面积
    double fpr = 0.0;           // 假阳性率
    double tpr = 0.0;           // 真阳性率

    // --- 性能指标 ---
    qint64 totalLatencyMs = 0;  // 总延迟
    double avgLatencyMs = 0.0;  // 平均延迟
    qint64 minLatencyMs = 0;    // 最小延迟
    qint64 maxLatencyMs = 0;    // 最大延迟
    double p50LatencyMs = 0.0;  // P50延迟
    double p99LatencyMs = 0.0;  // P99延迟
    double throughputFPS = 0.0; // 吞吐量（帧/秒）

    // 转为JSON
    QJsonObject toJson() const;
};

class EvaluationEngine : public QObject {
    Q_OBJECT

public:
    explicit EvaluationEngine(QObject* parent = nullptr);
    ~EvaluationEngine();

    // --- 数据集管理 ---
    // 添加单个测试样本
    void addSample(const EvalSample& sample);
    // 批量添加样本
    void addSamples(const QList<EvalSample>& samples);
    // 清空数据集
    void clearSamples();
    // 获取样本数
    int sampleCount() const { return m_samples.size(); }

    // --- 评估执行 ---
    // 使用 ZeroShotEngine 评估所有样本
    EvalMetrics evaluate(ZeroShotEngine* engine, float threshold = 0.5f);
    // 使用 PipelineEngine 评估所有样本
    EvalMetrics evaluatePipeline(PipelineEngine* engine, float threshold = 0.5f);
    // 评估单个样本（返回预测结果）
    EvalSample evaluateSingle(ZeroShotEngine* engine, const EvalSample& sample, float threshold);

    // --- 结果访问 ---
    EvalMetrics lastMetrics() const { return m_lastMetrics; }
    QList<EvalSample> evaluatedSamples() const { return m_samples; }

    // --- 报告生成 ---
    // 生成JSON格式报告
    QJsonObject generateJsonReport() const;
    // 生成文本格式报告
    QString generateTextReport() const;
    // 保存报告到文件
    bool saveReport(const QString& filePath, bool jsonFormat = true) const;

    // --- 辅助方法 ---
    // 从目录加载测试图像（normal/子目录为正常样本，anomaly/子目录为异常样本）
    static QList<EvalSample> loadDatasetFromDirectory(const QString& dirPath);
    // 计算 ROC 曲线下面积（AUROC）
    static double calculateAUROC(const QList<EvalSample>& samples);
    // 计算百分位数
    static double percentile(const QList<qint64>& values, double p);

signals:
    void progressUpdated(int current, int total);
    void evaluationCompleted(const EvalMetrics& metrics);

private:
    QList<EvalSample> m_samples;
    EvalMetrics m_lastMetrics;

    // 计算指标
    EvalMetrics computeMetrics(QList<EvalSample>& samples, float threshold);
};

} // namespace zsu

#endif // EVALUATIONENGINE_H
