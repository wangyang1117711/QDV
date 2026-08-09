#pragma once

#include "Monitoring/Anomaly.h"
#include "Monitoring/GpuMetricsCollector.h"
#include "Monitoring/ProcessSnapshot.h"
#include "Monitoring/SystemMetricsCollector.h"
#include "Monitoring/AnomalyDetector.h"
#include "Monitoring/MonitoringStorage.h"
#include "Core/Logger.h"

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QElapsedTimer>
#include <memory>

namespace QDV {

// 训练进度明细（用于把训练脚本输出的 epoch/loss/acc 等实时传给监控器）
struct TrainingProgressDetail {
    double progress = -1.0;          // 0.0 ~ 1.0，<0 表示无活跃任务
    QString status;                  // pending/running/completed/failed/cancelled
    int epoch = 0;
    int totalEpochs = 0;
    double trainLoss = -1.0;
    double trainAcc = -1.0;
    double valLoss = -1.0;
    double valAcc = -1.0;
    qint64 elapsedSeconds = -1;      // 已运行秒数
    qint64 remainingSeconds = -1;    // 预估剩余秒数
};

// 训练推理监控器
// 单例 orchestrator，定时采集系统指标、识别异常、持久化数据，并向 UI 上报
class TrainingInferenceMonitor : public QObject {
    Q_OBJECT

public:
    static TrainingInferenceMonitor* instance();

    // 启动/停止监控循环（intervalMs 为采样间隔，默认 2000 ms）
    void start(int intervalMs = 2000);
    void stop();

    // 业务模块上报训练任务进度（status 为 pending/running/completed/failed/cancelled）
    // progress < 0 表示无活跃任务；detail 包含 epoch/loss/acc/已运行/剩余等实时指标
    void recordTrainingProgress(const TrainingProgressDetail& detail);
    // 兼容旧接口：仅上报 progress + status
    void recordTrainingProgress(double progress, const QString& status);

    // 业务模块上报单次推理延迟（ms）
    void recordInferenceLatency(qint64 ms);

    // 业务模块上报模型加载状态
    void recordModelLoadStatus(int loadedCount, const QString& lastError);

    // 业务模块上报训练服务健康状态（本任务不实现 HTTP 轮询，由调用方传入）
    void recordServerHealth(bool reachable, qint64 latencyMs,
                            quint32 pid = 0, double cpuPercent = -1.0);

signals:
    // 新快照产生
    void snapshotReady(const QDV::ProcessSnapshot& snapshot);
    // 检测到异常
    void anomalyDetected(const QDV::Anomaly& anomaly);

private slots:
    void onTick();
    void onGpuMetricsUpdated(const QDV::GpuMetrics& metrics);

private:
    explicit TrainingInferenceMonitor(QObject* parent = nullptr);
    ~TrainingInferenceMonitor() override;

    // 推理延迟记录，用于计算近 60 秒吞吐量和平均延迟
    struct InferenceLatencyRecord {
        qint64 timestamp = 0;
        qint64 latencyMs = 0;
    };

    // 计算并填充快照中的推理性能字段
    void fillInferenceMetrics(ProcessSnapshot& snapshot);

    static TrainingInferenceMonitor* s_instance;
    static QMutex s_instanceMutex;

    std::unique_ptr<SystemMetricsCollector> m_collector;
    std::unique_ptr<GpuMetricsCollector> m_gpuCollector;
    std::unique_ptr<AnomalyDetector> m_detector;
    std::unique_ptr<MonitoringStorage> m_storage;

    QTimer* m_tickTimer = nullptr;

    // 最近一次 GPU 指标
    GpuMetrics m_lastGpuMetrics;
    QMutex m_gpuMutex;

    ProcessSnapshot m_lastSnapshot;
    QMutex m_snapshotMutex;

    // 训练服务健康状态
    bool m_serverReachable = false;
    qint64 m_serverLatencyMs = -1;
    quint32 m_serverPid = 0;
    double m_serverCpuPercent = -1.0;
    QMutex m_serverMutex;

    // TrainingClient / TrainingBridge 上报的训练进度明细
    TrainingProgressDetail m_lastTrainingDetail;
    QMutex m_progressMutex;

    // 模型加载状态
    int m_loadedModelCount = 0;
    QString m_lastModelLoadError;
    QMutex m_modelMutex;

    // 推理延迟记录（近 60 秒窗口）
    QList<InferenceLatencyRecord> m_inferenceLatencies;
    QMutex m_inferenceMutex;

    // 最近错误缓存（用于异常规则）
    QList<Logger::ErrorRecord> m_recentErrors;
    QMutex m_errorMutex;

    static constexpr qint64 INFERENCE_WINDOW_MS = 60000; // 60 秒窗口
};

} // namespace QDV
