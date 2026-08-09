#include "Monitoring/TrainingInferenceMonitor.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QMutexLocker>

namespace QDV {

TrainingInferenceMonitor* TrainingInferenceMonitor::s_instance = nullptr;
QMutex TrainingInferenceMonitor::s_instanceMutex;

TrainingInferenceMonitor* TrainingInferenceMonitor::instance() {
    QMutexLocker locker(&s_instanceMutex);
    if (!s_instance) {
        s_instance = new TrainingInferenceMonitor();
    }
    return s_instance;
}

TrainingInferenceMonitor::TrainingInferenceMonitor(QObject* parent)
    : QObject(parent)
    , m_collector(std::make_unique<SystemMetricsCollector>())
    , m_gpuCollector(std::make_unique<GpuMetricsCollector>())
    , m_detector(std::make_unique<AnomalyDetector>())
    , m_storage(std::make_unique<MonitoringStorage>(
          QCoreApplication::applicationDirPath() + "/logs/metrics")) {

    qRegisterMetaType<QDV::ProcessSnapshot>();
    qRegisterMetaType<QDV::Anomaly>();
    qRegisterMetaType<QDV::GpuMetrics>();

    m_tickTimer = new QTimer(this);
    connect(m_tickTimer, &QTimer::timeout, this, &TrainingInferenceMonitor::onTick);
    connect(m_gpuCollector.get(), &GpuMetricsCollector::metricsUpdated,
            this, &TrainingInferenceMonitor::onGpuMetricsUpdated);

    // 监听 Logger 错误，用于异常规则与上下文 enrich
    connect(Logger::instance(), &Logger::errorOccurred,
            this, [this](const Logger::ErrorRecord& record) {
                QMutexLocker locker(&m_errorMutex);
                m_recentErrors.append(record);
                // 保持列表规模可控，仅保留最近 200 条
                while (m_recentErrors.size() > 200) {
                    m_recentErrors.removeFirst();
                }
            }, Qt::QueuedConnection);
}

TrainingInferenceMonitor::~TrainingInferenceMonitor() {
    stop();
    s_instance = nullptr;
}

void TrainingInferenceMonitor::start(int intervalMs) {
    if (m_tickTimer->isActive()) {
        return;
    }

    Logger::info(QString("TrainingInferenceMonitor 启动，采样间隔 %1 ms").arg(intervalMs));

    // 启动 GPU 异步采集（与系统指标同周期）
    m_gpuCollector->start(intervalMs);

    // 立即执行一次采集，让用户尽快看到数据
    onTick();

    m_tickTimer->start(qMax(500, intervalMs));
}

void TrainingInferenceMonitor::stop() {
    if (m_tickTimer->isActive()) {
        m_tickTimer->stop();
    }
    m_gpuCollector->stop();

    Logger::info("TrainingInferenceMonitor 停止");
}

void TrainingInferenceMonitor::recordTrainingProgress(const TrainingProgressDetail& detail) {
    QMutexLocker locker(&m_progressMutex);
    m_lastTrainingDetail = detail;
}

void TrainingInferenceMonitor::recordTrainingProgress(double progress, const QString& status) {
    QMutexLocker locker(&m_progressMutex);
    m_lastTrainingDetail.progress = progress;
    m_lastTrainingDetail.status = status;
}

void TrainingInferenceMonitor::recordInferenceLatency(qint64 ms) {
    QMutexLocker locker(&m_inferenceMutex);
    InferenceLatencyRecord rec;
    rec.timestamp = QDateTime::currentDateTime().toMSecsSinceEpoch();
    rec.latencyMs = ms;
    m_inferenceLatencies.append(rec);

    // 清理超过 60 秒的旧记录
    qint64 cutoff = rec.timestamp - INFERENCE_WINDOW_MS;
    while (!m_inferenceLatencies.isEmpty() && m_inferenceLatencies.first().timestamp < cutoff) {
        m_inferenceLatencies.removeFirst();
    }
}

void TrainingInferenceMonitor::recordModelLoadStatus(int loadedCount, const QString& lastError) {
    QMutexLocker locker(&m_modelMutex);
    m_loadedModelCount = loadedCount;
    m_lastModelLoadError = lastError;
}

void TrainingInferenceMonitor::recordServerHealth(bool reachable, qint64 latencyMs,
                                                   quint32 pid, double cpuPercent) {
    QMutexLocker locker(&m_serverMutex);
    m_serverReachable = reachable;
    m_serverLatencyMs = latencyMs;
    m_serverPid = pid;
    m_serverCpuPercent = cpuPercent;
}

void TrainingInferenceMonitor::onTick() {
    // 采集进程指标
    ProcessSnapshot snapshot = m_collector->collect();
    snapshot.timestamp = QDateTime::currentDateTime().toMSecsSinceEpoch();

    // 合并训练服务健康状态（外部 HTTP 服务）
    bool externalServerReachable = false;
    qint64 externalServerLatencyMs = -1;
    {
        QMutexLocker locker(&m_serverMutex);
        externalServerReachable = m_serverReachable;
        externalServerLatencyMs = m_serverLatencyMs;
        snapshot.serverPid = m_serverPid;
        snapshot.serverCpuPercent = m_serverCpuPercent;
    }

    // 合并训练进度：本地训练 running 时也视为训练服务在线
    {
        QMutexLocker locker(&m_progressMutex);
        snapshot.serverProgress = m_lastTrainingDetail.progress;
        snapshot.serverJobStatus = m_lastTrainingDetail.status;
        snapshot.trainingEpoch = m_lastTrainingDetail.epoch;
        snapshot.trainingTotalEpochs = m_lastTrainingDetail.totalEpochs;
        snapshot.trainingLoss = m_lastTrainingDetail.trainLoss;
        snapshot.trainingAccuracy = m_lastTrainingDetail.trainAcc;
        snapshot.valLoss = m_lastTrainingDetail.valLoss;
        snapshot.valAccuracy = m_lastTrainingDetail.valAcc;
        snapshot.trainingElapsedSeconds = m_lastTrainingDetail.elapsedSeconds;
        snapshot.trainingRemainingSeconds = m_lastTrainingDetail.remainingSeconds;

        // 若本地训练正在运行，优先标记为在线；否则使用外部服务状态
        if (m_lastTrainingDetail.status == "running") {
            snapshot.serverReachable = true;
            snapshot.serverLatencyMs = 0;
        } else {
            snapshot.serverReachable = externalServerReachable;
            snapshot.serverLatencyMs = externalServerLatencyMs;
        }
    }

    // 合并模型加载状态
    {
        QMutexLocker locker(&m_modelMutex);
        snapshot.loadedModelCount = m_loadedModelCount;
        snapshot.lastModelLoadError = m_lastModelLoadError;
    }

    // 合并推理性能指标
    fillInferenceMetrics(snapshot);

    // 合并 GPU 指标
    {
        QMutexLocker locker(&m_gpuMutex);
        snapshot.gpuAvailable = m_lastGpuMetrics.available;
        snapshot.gpuName = m_lastGpuMetrics.name;
        snapshot.gpuUtilizationPercent = m_lastGpuMetrics.utilizationPercent;
        snapshot.gpuMemoryUsedMB = m_lastGpuMetrics.memoryUsedMB;
        snapshot.gpuMemoryTotalMB = m_lastGpuMetrics.memoryTotalMB;
        snapshot.gpuTemperatureC = m_lastGpuMetrics.temperatureC;
        snapshot.gpuError = m_lastGpuMetrics.error;
    }

    // 持久化快照
    m_storage->appendSnapshot(snapshot);

    // 异常检测
    ProcessSnapshot previous;
    {
        QMutexLocker locker(&m_snapshotMutex);
        previous = m_lastSnapshot;
        m_lastSnapshot = snapshot;
    }

    QList<Logger::ErrorRecord> errors;
    {
        QMutexLocker locker(&m_errorMutex);
        errors = m_recentErrors;
    }

    QList<Anomaly> anomalies = m_detector->evaluate(snapshot, previous, errors);
    for (const Anomaly& a : anomalies) {
        m_storage->appendAnomaly(a);
        emit anomalyDetected(a);
    }

    emit snapshotReady(snapshot);
}

void TrainingInferenceMonitor::onGpuMetricsUpdated(const QDV::GpuMetrics& metrics) {
    QMutexLocker locker(&m_gpuMutex);
    m_lastGpuMetrics = metrics;
}

void TrainingInferenceMonitor::fillInferenceMetrics(ProcessSnapshot& snapshot) {
    QMutexLocker locker(&m_inferenceMutex);

    if (m_inferenceLatencies.isEmpty()) {
        snapshot.lastInferenceLatencyMs = -1.0;
        snapshot.avgInferenceLatencyMs = -1.0;
        snapshot.inferenceThroughputPerMin = 0.0;
        return;
    }

    qint64 now = QDateTime::currentDateTime().toMSecsSinceEpoch();
    qint64 cutoff = now - INFERENCE_WINDOW_MS;

    // 清理窗口外的旧记录
    while (!m_inferenceLatencies.isEmpty() && m_inferenceLatencies.first().timestamp < cutoff) {
        m_inferenceLatencies.removeFirst();
    }

    if (m_inferenceLatencies.isEmpty()) {
        snapshot.lastInferenceLatencyMs = -1.0;
        snapshot.avgInferenceLatencyMs = -1.0;
        snapshot.inferenceThroughputPerMin = 0.0;
        return;
    }

    // 最后一条延迟
    snapshot.lastInferenceLatencyMs = static_cast<double>(m_inferenceLatencies.last().latencyMs);

    // 窗口内平均延迟
    qint64 total = 0;
    for (const auto& rec : m_inferenceLatencies) {
        total += rec.latencyMs;
    }
    snapshot.avgInferenceLatencyMs = static_cast<double>(total) / m_inferenceLatencies.size();

    // 近 60 秒吞吐量（记录数即每分钟吞吐量，因为窗口为 60 秒）
    snapshot.inferenceThroughputPerMin = static_cast<double>(m_inferenceLatencies.size());
}

} // namespace QDV
