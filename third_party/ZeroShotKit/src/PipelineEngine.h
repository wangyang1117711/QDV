#ifndef PIPELINEENGINE_H
#define PIPELINEENGINE_H

// ============================================================================
// M5-2: 流水线并行推理引擎
//
// 功能：
//   1. 多阶段推理流水线（异常检测 → 目标检测 → 分割）
//   2. 支持串行和并行（QtConcurrent）两种执行模式
//   3. 阶段进度跟踪、取消、超时控制
//   4. 批量推理支持
//
// 流水线阶段：
//   Stage 1: AnomalyCLIP / PatchCore 异常检测
//   Stage 2: Grounding DINO 目标检测（当异常分数 > 阈值时触发）
//   Stage 3: MobileSAM 分割（当检测到目标时触发）
// ============================================================================

#include <QObject>
#include <QString>
#include <QList>
#include <QFuture>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <opencv2/opencv.hpp>
#include "ZeroShotEngine.h"
#include "ZeroShotTypes.h"

namespace zsu {

// 流水线阶段配置
struct PipelineStageConfig {
    bool enableAnomalyDetect = true;    // 阶段1: 异常检测
    bool enableObjectDetect = true;     // 阶段2: 目标检测
    bool enableSegmentation = true;     // 阶段3: 分割
    float anomalyTriggerThreshold = 0.5f;  // 异常分数超过此值才触发后续阶段
    float detectionTriggerThreshold = 0.3f; // 检测分数超过此值才触发分割
};

// 流水线结果
struct PipelineResult {
    bool success = false;
    QString errorMessage;

    // 各阶段结果
    ZeroShotResult anomalyResult;       // 阶段1结果
    ZeroShotResult detectionResult;     // 阶段2结果
    ZeroShotResult segmentResult;       // 阶段3结果

    // 各阶段耗时（ms）
    qint64 anomalyLatencyMs = 0;
    qint64 detectionLatencyMs = 0;
    qint64 segmentLatencyMs = 0;
    qint64 totalLatencyMs = 0;

    // 执行的阶段
    bool anomalyExecuted = false;
    bool detectionExecuted = false;
    bool segmentExecuted = false;

    // 批量推理索引
    int batchIndex = -1;
};

// 流水线统计
struct PipelineStats {
    int totalInferences = 0;        // 总推理次数
    int successfulInferences = 0;   // 成功推理次数
    int failedInferences = 0;       // 失败推理次数
    qint64 totalLatencyMs = 0;      // 总延迟
    qint64 minLatencyMs = INT64_MAX; // 最小延迟
    qint64 maxLatencyMs = 0;        // 最大延迟
    double avgLatencyMs = 0.0;      // 平均延迟

    // 各阶段执行次数
    int anomalyExecutions = 0;
    int detectionExecutions = 0;
    int segmentExecutions = 0;

    void reset() {
        *this = PipelineStats{};
    }

    void update(qint64 latency, bool success) {
        totalInferences++;
        if (success) {
            successfulInferences++;
        } else {
            failedInferences++;
        }
        totalLatencyMs += latency;
        if (latency < minLatencyMs) minLatencyMs = latency;
        if (latency > maxLatencyMs) maxLatencyMs = latency;
        avgLatencyMs = (double)totalLatencyMs / totalInferences;
    }
};

class PipelineEngine : public QObject {
    Q_OBJECT

public:
    explicit PipelineEngine(QObject* parent = nullptr);
    ~PipelineEngine();

    // --- 引擎绑定 ---
    // 绑定已加载模型的 ZeroShotEngine（PipelineEngine 不负责模型加载）
    void setAnomalyEngine(ZeroShotEngine* engine) { m_anomalyEngine = engine; }
    void setDetectionEngine(ZeroShotEngine* engine) { m_detectionEngine = engine; }
    void setSegmentEngine(ZeroShotEngine* engine) { m_segmentEngine = engine; }

    ZeroShotEngine* anomalyEngine() const { return m_anomalyEngine; }
    ZeroShotEngine* detectionEngine() const { return m_detectionEngine; }
    ZeroShotEngine* segmentEngine() const { return m_segmentEngine; }

    // --- 阶段配置 ---
    void setStageConfig(const PipelineStageConfig& config) { m_stageConfig = config; }
    PipelineStageConfig stageConfig() const { return m_stageConfig; }

    // --- 执行模式 ---
    void setParallelMode(bool enable) { m_parallelMode = enable; }
    bool parallelMode() const { return m_parallelMode; }

    // --- 同步推理 ---
    // 单张图像推理
    PipelineResult infer(const cv::Mat& input);
    // 批量推理
    QList<PipelineResult> inferBatch(const QList<cv::Mat>& inputs);

    // --- 异步推理 ---
    // 异步单张推理（使用 QtConcurrent）
    void inferAsync(const cv::Mat& input);
    // 异步批量推理
    void inferBatchAsync(const QList<cv::Mat>& inputs);
    // 是否正在执行
    bool isRunning() const { return m_futureWatcher.isRunning(); }
    // 取消推理
    void cancel() { m_cancelFlag.store(true, std::memory_order_release); }
    bool isCancelled() const { return m_cancelFlag.load(std::memory_order_acquire); }
    void resetCancel() { m_cancelFlag.store(false, std::memory_order_release); }

    // --- 统计 ---
    PipelineStats stats() const { return m_stats; }
    void resetStats() { m_stats.reset(); }

signals:
    void inferenceCompleted(const PipelineResult& result);
    void batchCompleted(const QList<PipelineResult>& results);
    void stageCompleted(const QString& stageName, qint64 elapsedMs);
    void progressUpdated(int current, int total);

private:
    ZeroShotEngine* m_anomalyEngine = nullptr;
    ZeroShotEngine* m_detectionEngine = nullptr;
    ZeroShotEngine* m_segmentEngine = nullptr;

    PipelineStageConfig m_stageConfig;
    bool m_parallelMode = false;

    // 异步执行
    QFutureWatcher<PipelineResult> m_futureWatcher;
    QFutureWatcher<QList<PipelineResult>> m_batchFutureWatcher;
    std::atomic<bool> m_cancelFlag{false};

    // 统计
    PipelineStats m_stats;

    // 内部：执行单个流水线
    PipelineResult executePipeline(const cv::Mat& input);
};

} // namespace zsu

#endif // PIPELINEENGINE_H
