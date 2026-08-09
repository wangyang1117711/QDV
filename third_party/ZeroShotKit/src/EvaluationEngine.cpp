// ============================================================================
// M5-3: 评估体系引擎实现
// ============================================================================

#include "EvaluationEngine.h"
#include "ZeroShotKit/Logger.h"

#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QJsonDocument>
#include <algorithm>
#include <numeric>

namespace zsu {

// ============================================================================
// EvalMetrics::toJson
// ============================================================================

QJsonObject EvalMetrics::toJson() const {
    QJsonObject obj;
    obj["total_samples"] = totalSamples;
    obj["correct_predictions"] = correctPredictions;
    obj["accuracy"] = accuracy;
    obj["true_positives"] = truePositives;
    obj["false_positives"] = falsePositives;
    obj["true_negatives"] = trueNegatives;
    obj["false_negatives"] = falseNegatives;
    obj["precision"] = precision;
    obj["recall"] = recall;
    obj["f1_score"] = f1Score;
    obj["auroc"] = auroc;
    obj["fpr"] = fpr;
    obj["tpr"] = tpr;
    obj["total_latency_ms"] = (qint64)totalLatencyMs;
    obj["avg_latency_ms"] = avgLatencyMs;
    obj["min_latency_ms"] = (qint64)minLatencyMs;
    obj["max_latency_ms"] = (qint64)maxLatencyMs;
    obj["p50_latency_ms"] = p50LatencyMs;
    obj["p99_latency_ms"] = p99LatencyMs;
    obj["throughput_fps"] = throughputFPS;
    return obj;
}

// ============================================================================
// EvaluationEngine
// ============================================================================

EvaluationEngine::EvaluationEngine(QObject* parent)
    : QObject(parent)
{
}

EvaluationEngine::~EvaluationEngine() {
}

// --- 数据集管理 ---

void EvaluationEngine::addSample(const EvalSample& sample) {
    m_samples.append(sample);
}

void EvaluationEngine::addSamples(const QList<EvalSample>& samples) {
    m_samples.append(samples);
}

void EvaluationEngine::clearSamples() {
    m_samples.clear();
    m_lastMetrics = EvalMetrics{};
}

// --- 评估执行 ---

EvalMetrics EvaluationEngine::evaluate(ZeroShotEngine* engine, float threshold) {
    if (!engine || !engine->isModelLoaded()) {
        ZSU_LOG_ERROR("EvaluationEngine: 引擎未加载模型");
        m_lastMetrics = EvalMetrics{};
        return m_lastMetrics;
    }

    if (m_samples.isEmpty()) {
        ZSU_LOG_WARN("EvaluationEngine: 无测试样本");
        m_lastMetrics = EvalMetrics{};
        return m_lastMetrics;
    }

    ZSU_LOG_INFO(QString("EvaluationEngine: 开始评估 %1 个样本, threshold=%2")
        .arg(m_samples.size()).arg(threshold));

    for (int i = 0; i < m_samples.size(); ++i) {
        m_samples[i] = evaluateSingle(engine, m_samples[i], threshold);
        emit progressUpdated(i + 1, m_samples.size());
    }

    m_lastMetrics = computeMetrics(m_samples, threshold);
    emit evaluationCompleted(m_lastMetrics);

    ZSU_LOG_INFO(QString("EvaluationEngine: 评估完成 accuracy=%1 f1=%2 avg_latency=%3ms")
        .arg(m_lastMetrics.accuracy)
        .arg(m_lastMetrics.f1Score)
        .arg(m_lastMetrics.avgLatencyMs));

    return m_lastMetrics;
}

EvalMetrics EvaluationEngine::evaluatePipeline(PipelineEngine* engine, float threshold) {
    if (!engine) {
        ZSU_LOG_ERROR("EvaluationEngine: PipelineEngine 为空");
        m_lastMetrics = EvalMetrics{};
        return m_lastMetrics;
    }

    if (m_samples.isEmpty()) {
        ZSU_LOG_WARN("EvaluationEngine: 无测试样本");
        m_lastMetrics = EvalMetrics{};
        return m_lastMetrics;
    }

    ZSU_LOG_INFO(QString("EvaluationEngine: 开始流水线评估 %1 个样本").arg(m_samples.size()));

    for (int i = 0; i < m_samples.size(); ++i) {
        QElapsedTimer timer;
        timer.start();

        PipelineResult pipeResult = engine->infer(m_samples[i].image);
        m_samples[i].latencyMs = timer.elapsed();

        // 使用异常检测阶段的结果
        m_samples[i].predictedScore = pipeResult.anomalyResult.anomalyScore;
        m_samples[i].predictedCategory = pipeResult.anomalyResult.category;
        m_samples[i].correct = (m_samples[i].predictedScore >= threshold) == m_samples[i].isAnomaly;

        emit progressUpdated(i + 1, m_samples.size());
    }

    m_lastMetrics = computeMetrics(m_samples, threshold);
    emit evaluationCompleted(m_lastMetrics);

    return m_lastMetrics;
}

EvalSample EvaluationEngine::evaluateSingle(ZeroShotEngine* engine, const EvalSample& sample, float threshold) {
    EvalSample result = sample;

    QElapsedTimer timer;
    timer.start();

    ZeroShotResult inferResult = engine->infer(sample.image);
    result.latencyMs = timer.elapsed();
    result.predictedScore = inferResult.anomalyScore;
    result.predictedCategory = inferResult.category;
    result.correct = (result.predictedScore >= threshold) == result.isAnomaly;

    return result;
}

// --- 指标计算 ---

EvalMetrics EvaluationEngine::computeMetrics(QList<EvalSample>& samples, float threshold) {
    EvalMetrics metrics;
    metrics.totalSamples = samples.size();

    if (samples.isEmpty()) {
        return metrics;
    }

    // 分类指标
    QList<qint64> latencies;
    metrics.minLatencyMs = INT64_MAX;

    for (const auto& s : samples) {
        latencies.append(s.latencyMs);
        metrics.totalLatencyMs += s.latencyMs;

        if (s.latencyMs < metrics.minLatencyMs) metrics.minLatencyMs = s.latencyMs;
        if (s.latencyMs > metrics.maxLatencyMs) metrics.maxLatencyMs = s.latencyMs;

        if (s.correct) metrics.correctPredictions++;

        // 混淆矩阵
        bool predictedAnomaly = (s.predictedScore >= threshold);
        if (predictedAnomaly && s.isAnomaly) metrics.truePositives++;
        else if (predictedAnomaly && !s.isAnomaly) metrics.falsePositives++;
        else if (!predictedAnomaly && s.isAnomaly) metrics.falseNegatives++;
        else metrics.trueNegatives++;
    }

    // 准确率
    metrics.accuracy = (double)metrics.correctPredictions / metrics.totalSamples;

    // 精确率
    if (metrics.truePositives + metrics.falsePositives > 0) {
        metrics.precision = (double)metrics.truePositives / (metrics.truePositives + metrics.falsePositives);
    }

    // 召回率
    if (metrics.truePositives + metrics.falseNegatives > 0) {
        metrics.recall = (double)metrics.truePositives / (metrics.truePositives + metrics.falseNegatives);
    }

    // F1
    if (metrics.precision + metrics.recall > 0) {
        metrics.f1Score = 2.0 * metrics.precision * metrics.recall / (metrics.precision + metrics.recall);
    }

    // FPR 和 TPR
    if (metrics.trueNegatives + metrics.falsePositives > 0) {
        metrics.fpr = (double)metrics.falsePositives / (metrics.trueNegatives + metrics.falsePositives);
    }
    if (metrics.truePositives + metrics.falseNegatives > 0) {
        metrics.tpr = (double)metrics.truePositives / (metrics.truePositives + metrics.falseNegatives);
    }

    // AUROC
    metrics.auroc = calculateAUROC(samples);

    // 性能指标
    metrics.avgLatencyMs = (double)metrics.totalLatencyMs / metrics.totalSamples;
    metrics.p50LatencyMs = percentile(latencies, 50.0);
    metrics.p99LatencyMs = percentile(latencies, 99.0);

    // 吞吐量
    if (metrics.totalLatencyMs > 0) {
        metrics.throughputFPS = (double)metrics.totalSamples / metrics.totalLatencyMs * 1000.0;
    }

    return metrics;
}

// --- 报告生成 ---

QJsonObject EvaluationEngine::generateJsonReport() const {
    QJsonObject report;
    report["evaluation_metrics"] = m_lastMetrics.toJson();

    // 样本详情
    QJsonArray sampleArray;
    for (const auto& s : m_samples) {
        QJsonObject sampleObj;
        sampleObj["image_path"] = s.imagePath;
        sampleObj["is_anomaly"] = s.isAnomaly;
        sampleObj["predicted_score"] = s.predictedScore;
        sampleObj["predicted_category"] = s.predictedCategory;
        sampleObj["latency_ms"] = (qint64)s.latencyMs;
        sampleObj["correct"] = s.correct;
        sampleArray.append(sampleObj);
    }
    report["samples"] = sampleArray;

    return report;
}

QString EvaluationEngine::generateTextReport() const {
    QString report;
    QTextStream ts(&report);

    ts << "========== 零样本推理评估报告 ==========\n\n";
    ts << "--- 数据集 ---\n";
    ts << QString("总样本数: %1\n").arg(m_lastMetrics.totalSamples);
    ts << QString("正常样本: %1\n").arg(m_lastMetrics.trueNegatives + m_lastMetrics.falsePositives);
    ts << QString("异常样本: %1\n\n").arg(m_lastMetrics.truePositives + m_lastMetrics.falseNegatives);

    ts << "--- 分类指标 ---\n";
    ts << QString("准确率 (Accuracy):  %1\n").arg(m_lastMetrics.accuracy, 0, 'f', 4);
    ts << QString("精确率 (Precision): %1\n").arg(m_lastMetrics.precision, 0, 'f', 4);
    ts << QString("召回率 (Recall):    %1\n").arg(m_lastMetrics.recall, 0, 'f', 4);
    ts << QString("F1分数 (F1-Score):  %1\n\n").arg(m_lastMetrics.f1Score, 0, 'f', 4);

    ts << "--- 混淆矩阵 ---\n";
    ts << QString("真阳性 (TP): %1\n").arg(m_lastMetrics.truePositives);
    ts << QString("假阳性 (FP): %1\n").arg(m_lastMetrics.falsePositives);
    ts << QString("真阴性 (TN): %1\n").arg(m_lastMetrics.trueNegatives);
    ts << QString("假阴性 (FN): %1\n\n").arg(m_lastMetrics.falseNegatives);

    ts << "--- 异常检测指标 ---\n";
    ts << QString("AUROC: %1\n").arg(m_lastMetrics.auroc, 0, 'f', 4);
    ts << QString("FPR:   %1\n").arg(m_lastMetrics.fpr, 0, 'f', 4);
    ts << QString("TPR:   %1\n\n").arg(m_lastMetrics.tpr, 0, 'f', 4);

    ts << "--- 性能指标 ---\n";
    ts << QString("平均延迟:   %1 ms\n").arg(m_lastMetrics.avgLatencyMs, 0, 'f', 2);
    ts << QString("最小延迟:   %1 ms\n").arg(m_lastMetrics.minLatencyMs);
    ts << QString("最大延迟:   %1 ms\n").arg(m_lastMetrics.maxLatencyMs);
    ts << QString("P50延迟:    %1 ms\n").arg(m_lastMetrics.p50LatencyMs, 0, 'f', 2);
    ts << QString("P99延迟:    %1 ms\n").arg(m_lastMetrics.p99LatencyMs, 0, 'f', 2);
    ts << QString("吞吐量:     %1 FPS\n\n").arg(m_lastMetrics.throughputFPS, 0, 'f', 2);

    ts << "========================================\n";

    return report;
}

bool EvaluationEngine::saveReport(const QString& filePath, bool jsonFormat) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ZSU_LOG_ERROR(QString("EvaluationEngine: 无法打开报告文件: %1").arg(filePath));
        return false;
    }

    if (jsonFormat) {
        QJsonDocument doc(generateJsonReport());
        file.write(doc.toJson(QJsonDocument::Indented));
    } else {
        file.write(generateTextReport().toUtf8());
    }

    file.close();
    ZSU_LOG_INFO(QString("EvaluationEngine: 报告已保存: %1").arg(filePath));
    return true;
}

// --- 静态方法 ---

QList<EvalSample> EvaluationEngine::loadDatasetFromDirectory(const QString& dirPath) {
    QList<EvalSample> samples;
    QDir dir(dirPath);

    if (!dir.exists()) {
        ZSU_LOG_WARN(QString("EvaluationEngine: 数据集目录不存在: %1").arg(dirPath));
        return samples;
    }

    // 加载 normal/ 子目录
    QDir normalDir(dir.filePath("normal"));
    if (normalDir.exists()) {
        QStringList filters = {"*.jpg", "*.jpeg", "*.png", "*.bmp"};
        QFileInfoList files = normalDir.entryInfoList(filters, QDir::Files);
        for (const auto& fi : files) {
            EvalSample sample;
            sample.imagePath = fi.absoluteFilePath();
            sample.image = cv::imread(fi.absoluteFilePath().toStdString());
            sample.isAnomaly = false;
            sample.category = "normal";
            if (!sample.image.empty()) {
                samples.append(sample);
            }
        }
        ZSU_LOG_INFO(QString("加载正常样本: %1 个").arg(files.size()));
    }

    // 加载 anomaly/ 子目录
    QDir anomalyDir(dir.filePath("anomaly"));
    if (anomalyDir.exists()) {
        QStringList filters = {"*.jpg", "*.jpeg", "*.png", "*.bmp"};
        QFileInfoList files = anomalyDir.entryInfoList(filters, QDir::Files);
        for (const auto& fi : files) {
            EvalSample sample;
            sample.imagePath = fi.absoluteFilePath();
            sample.image = cv::imread(fi.absoluteFilePath().toStdString());
            sample.isAnomaly = true;
            sample.category = "anomaly";
            if (!sample.image.empty()) {
                samples.append(sample);
            }
        }
        ZSU_LOG_INFO(QString("加载异常样本: %1 个").arg(files.size()));
    }

    return samples;
}

double EvaluationEngine::calculateAUROC(const QList<EvalSample>& samples) {
    if (samples.size() < 2) return 0.0;

    // 按 predictedScore 降序排序
    QList<EvalSample> sorted = samples;
    std::sort(sorted.begin(), sorted.end(),
        [](const EvalSample& a, const EvalSample& b) {
            return a.predictedScore > b.predictedScore;
        });

    int totalPositives = 0;
    int totalNegatives = 0;
    for (const auto& s : sorted) {
        if (s.isAnomaly) totalPositives++;
        else totalNegatives++;
    }

    if (totalPositives == 0 || totalNegatives == 0) return 0.0;

    // 计算 ROC 曲线下面积（梯形法）
    double auroc = 0.0;
    int tp = 0;
    int fp = 0;
    double prevTpr = 0.0;
    double prevFpr = 0.0;

    for (const auto& s : sorted) {
        if (s.isAnomaly) {
            tp++;
        } else {
            fp++;
        }

        double tpr = (double)tp / totalPositives;
        double fpr = (double)fp / totalNegatives;

        // 梯形面积
        auroc += (fpr - prevFpr) * (tpr + prevTpr) / 2.0;

        prevTpr = tpr;
        prevFpr = fpr;
    }

    return auroc;
}

double EvaluationEngine::percentile(const QList<qint64>& values, double p) {
    if (values.isEmpty()) return 0.0;

    QList<qint64> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    double index = (p / 100.0) * (sorted.size() - 1);
    int lower = (int)index;
    int upper = lower + 1;

    if (upper >= sorted.size()) {
        return (double)sorted.last();
    }

    double weight = index - lower;
    return (double)sorted[lower] * (1.0 - weight) + (double)sorted[upper] * weight;
}

} // namespace zsu
