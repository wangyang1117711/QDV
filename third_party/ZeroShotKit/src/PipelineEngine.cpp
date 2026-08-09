// ============================================================================
// M5-2: 流水线并行推理引擎实现
//
// 多阶段推理流水线：异常检测 → 目标检测 → 分割
// 支持串行和并行（QtConcurrent）两种执行模式
// ============================================================================

#include "PipelineEngine.h"
#include "ZeroShotKit/Logger.h"

#include <QtConcurrent>

namespace zsu {

PipelineEngine::PipelineEngine(QObject* parent)
    : QObject(parent)
{
    // 异步推理完成信号
    connect(&m_futureWatcher, &QFutureWatcher<PipelineResult>::finished, [this]() {
        PipelineResult result = m_futureWatcher.result();
        m_stats.update(result.totalLatencyMs, result.success);
        emit inferenceCompleted(result);
    });

    connect(&m_batchFutureWatcher, &QFutureWatcher<QList<PipelineResult>>::finished, [this]() {
        QList<PipelineResult> results = m_batchFutureWatcher.result();
        for (const auto& r : results) {
            m_stats.update(r.totalLatencyMs, r.success);
        }
        emit batchCompleted(results);
    });
}

PipelineEngine::~PipelineEngine() {
    cancel();
    m_futureWatcher.waitForFinished();
    m_batchFutureWatcher.waitForFinished();
}

// ============================================================================
// 同步推理
// ============================================================================

PipelineResult PipelineEngine::infer(const cv::Mat& input) {
    if (input.empty()) {
        PipelineResult result;
        result.success = false;
        result.errorMessage = "输入图像为空";
        return result;
    }

    resetCancel();
    PipelineResult result = executePipeline(input);
    m_stats.update(result.totalLatencyMs, result.success);
    return result;
}

QList<PipelineResult> PipelineEngine::inferBatch(const QList<cv::Mat>& inputs) {
    QList<PipelineResult> results;
    resetCancel();

    for (int i = 0; i < inputs.size(); ++i) {
        if (isCancelled()) {
            ZSU_LOG_WARN("PipelineEngine: 批量推理被取消");
            break;
        }

        PipelineResult result = executePipeline(inputs[i]);
        result.batchIndex = i;
        results.append(result);
        m_stats.update(result.totalLatencyMs, result.success);

        emit progressUpdated(i + 1, inputs.size());
    }

    return results;
}

// ============================================================================
// 异步推理
// ============================================================================

void PipelineEngine::inferAsync(const cv::Mat& input) {
    resetCancel();

    // 使用 QtConcurrent 异步执行
    QFuture<PipelineResult> future = QtConcurrent::run([this, input]() {
        return executePipeline(input);
    });

    m_futureWatcher.setFuture(future);
}

void PipelineEngine::inferBatchAsync(const QList<cv::Mat>& inputs) {
    resetCancel();

    QFuture<QList<PipelineResult>> future = QtConcurrent::run([this, inputs]() {
        QList<PipelineResult> results;
        for (int i = 0; i < inputs.size(); ++i) {
            if (isCancelled()) {
                ZSU_LOG_WARN("PipelineEngine: 异步批量推理被取消");
                break;
            }

            PipelineResult result = executePipeline(inputs[i]);
            result.batchIndex = i;
            results.append(result);
        }
        return results;
    });

    m_batchFutureWatcher.setFuture(future);
}

// ============================================================================
// 流水线执行
// ============================================================================

PipelineResult PipelineEngine::executePipeline(const cv::Mat& input) {
    PipelineResult result;
    QElapsedTimer totalTimer;
    totalTimer.start();

    if (input.empty()) {
        result.success = false;
        result.errorMessage = "输入图像为空";
        result.totalLatencyMs = totalTimer.elapsed();
        return result;
    }

    // --- 阶段1: 异常检测（AnomalyCLIP / PatchCore） ---
    if (m_stageConfig.enableAnomalyDetect && m_anomalyEngine && m_anomalyEngine->isModelLoaded()) {
        QElapsedTimer timer;
        timer.start();

        result.anomalyResult = m_anomalyEngine->infer(input);
        result.anomalyLatencyMs = timer.elapsed();
        result.anomalyExecuted = true;
        m_stats.anomalyExecutions++;

        emit stageCompleted("AnomalyDetect", result.anomalyLatencyMs);

        if (!result.anomalyResult.success) {
            result.success = false;
            result.errorMessage = "异常检测失败: " + result.anomalyResult.errorMessage;
            result.totalLatencyMs = totalTimer.elapsed();
            return result;
        }

        ZSU_LOG_DEBUG(QString("Pipeline 阶段1: 异常检测 score=%1 latency=%2ms")
            .arg(result.anomalyResult.anomalyScore)
            .arg(result.anomalyLatencyMs));

        // 如果异常分数低于阈值，跳过后续阶段
        if (result.anomalyResult.anomalyScore < m_stageConfig.anomalyTriggerThreshold) {
            result.success = true;
            result.totalLatencyMs = totalTimer.elapsed();
            ZSU_LOG_DEBUG(QString("Pipeline: 异常分数 %1 低于阈值 %2，跳过后续阶段")
                .arg(result.anomalyResult.anomalyScore)
                .arg(m_stageConfig.anomalyTriggerThreshold));
            return result;
        }
    }

    // --- 阶段2: 目标检测（Grounding DINO） ---
    if (m_stageConfig.enableObjectDetect && m_detectionEngine && m_detectionEngine->isModelLoaded()) {
        QElapsedTimer timer;
        timer.start();

        result.detectionResult = m_detectionEngine->infer(input);
        result.detectionLatencyMs = timer.elapsed();
        result.detectionExecuted = true;
        m_stats.detectionExecutions++;

        emit stageCompleted("ObjectDetect", result.detectionLatencyMs);

        if (!result.detectionResult.success) {
            ZSU_LOG_WARN(QString("Pipeline 阶段2: 目标检测失败 - %1")
                .arg(result.detectionResult.errorMessage));
            // 检测失败不终止流水线，继续到分割阶段
        } else {
            ZSU_LOG_DEBUG(QString("Pipeline 阶段2: 目标检测 category=%1 latency=%2ms")
                .arg(result.detectionResult.category)
                .arg(result.detectionLatencyMs));

            // 如果没有检测到目标，跳过分割
            if (result.detectionResult.detections.empty() ||
                result.detectionResult.anomalyScore < m_stageConfig.detectionTriggerThreshold) {
                result.success = true;
                result.totalLatencyMs = totalTimer.elapsed();
                return result;
            }
        }
    }

    // --- 阶段3: 分割（MobileSAM） ---
    if (m_stageConfig.enableSegmentation && m_segmentEngine && m_segmentEngine->isModelLoaded()) {
        QElapsedTimer timer;
        timer.start();

        result.segmentResult = m_segmentEngine->infer(input);
        result.segmentLatencyMs = timer.elapsed();
        result.segmentExecuted = true;
        m_stats.segmentExecutions++;

        emit stageCompleted("Segmentation", result.segmentLatencyMs);

        if (!result.segmentResult.success) {
            ZSU_LOG_WARN(QString("Pipeline 阶段3: 分割失败 - %1")
                .arg(result.segmentResult.errorMessage));
            // 分割失败不终止流水线
        } else {
            ZSU_LOG_DEBUG(QString("Pipeline 阶段3: 分割完成 latency=%1ms")
                .arg(result.segmentLatencyMs));
        }
    }

    result.success = true;
    result.totalLatencyMs = totalTimer.elapsed();

    ZSU_LOG_INFO(QString("Pipeline 完成: anomaly=%1ms detect=%2ms segment=%3ms total=%4ms")
        .arg(result.anomalyLatencyMs)
        .arg(result.detectionLatencyMs)
        .arg(result.segmentLatencyMs)
        .arg(result.totalLatencyMs));

    return result;
}

} // namespace zsu
