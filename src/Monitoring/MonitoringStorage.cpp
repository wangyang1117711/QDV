#include "Monitoring/MonitoringStorage.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QMutexLocker>

namespace QDV {

namespace {

QJsonObject snapshotToJson(const ProcessSnapshot& s) {
    QJsonObject obj;
    obj["timestamp"] = s.timestamp;
    obj["pid"] = static_cast<int>(s.pid);
    obj["cpu_percent"] = s.cpuPercent;
    obj["working_set_bytes"] = static_cast<qint64>(s.workingSetBytes);
    obj["private_bytes"] = static_cast<qint64>(s.privateBytes);
    obj["disk_read_bytes"] = static_cast<qint64>(s.diskReadBytes);
    obj["disk_write_bytes"] = static_cast<qint64>(s.diskWriteBytes);
    obj["tcp_connection_count"] = s.tcpConnectionCount;
    obj["server_reachable"] = s.serverReachable;
    obj["server_latency_ms"] = s.serverLatencyMs;
    // 训练服务侧进程信息（跨进程监控用）
    obj["server_pid"] = static_cast<int>(s.serverPid);
    obj["server_cpu_percent"] = s.serverCpuPercent;
    // 训练进度（用于语义级异常检测：进度停滞）
    obj["server_progress"] = s.serverProgress;
    obj["server_job_status"] = s.serverJobStatus;
    obj["training_epoch"] = s.trainingEpoch;
    obj["training_total_epochs"] = s.trainingTotalEpochs;
    obj["training_loss"] = s.trainingLoss;
    obj["training_accuracy"] = s.trainingAccuracy;
    obj["val_loss"] = s.valLoss;
    obj["val_accuracy"] = s.valAccuracy;
    obj["training_elapsed_seconds"] = s.trainingElapsedSeconds;
    obj["training_remaining_seconds"] = s.trainingRemainingSeconds;
    // 推理性能
    obj["last_inference_latency_ms"] = s.lastInferenceLatencyMs;
    obj["avg_inference_latency_ms"] = s.avgInferenceLatencyMs;
    obj["inference_throughput_per_min"] = s.inferenceThroughputPerMin;
    // GPU 状态
    obj["gpu_available"] = s.gpuAvailable;
    obj["gpu_name"] = s.gpuName;
    obj["gpu_utilization_percent"] = s.gpuUtilizationPercent;
    obj["gpu_memory_used_mb"] = s.gpuMemoryUsedMB;
    obj["gpu_memory_total_mb"] = s.gpuMemoryTotalMB;
    obj["gpu_temperature_c"] = s.gpuTemperatureC;
    obj["gpu_error"] = s.gpuError;
    // 模型加载
    obj["loaded_model_count"] = s.loadedModelCount;
    obj["last_model_load_error"] = s.lastModelLoadError;
    return obj;
}

QJsonObject anomalyToJson(const Anomaly& a) {
    QJsonObject obj;
    obj["anomaly_id"] = a.anomalyId;
    obj["timestamp"] = a.timestamp;
    obj["severity"] = a.severity;
    obj["category"] = a.category;
    obj["message"] = a.message;
    obj["snapshot"] = snapshotToJson(a.snapshot);

    QJsonObject ctx;
    for (auto it = a.context.begin(); it != a.context.end(); ++it) {
        ctx[it.key()] = QJsonValue::fromVariant(it.value());
    }
    obj["context"] = ctx;
    return obj;
}

} // namespace

MonitoringStorage::MonitoringStorage(const QString& baseDir)
    : m_baseDir(baseDir) {
    QDir().mkpath(m_baseDir);
    m_currentDate = QDateTime::currentDateTime().toString("yyyyMMdd");
    cleanupOldFiles();
}

void MonitoringStorage::appendSnapshot(const ProcessSnapshot& snapshot) {
    ensureDateRolled();
    QJsonDocument doc(snapshotToJson(snapshot));
    appendLine(metricsFilePath(), QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void MonitoringStorage::appendAnomaly(const Anomaly& anomaly) {
    ensureDateRolled();
    QJsonDocument doc(anomalyToJson(anomaly));
    appendLine(anomaliesFilePath(), QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void MonitoringStorage::cleanupOldFiles(int keepDays) {
    QMutexLocker locker(&m_mutex);
    QDir dir(m_baseDir);
    if (!dir.exists()) return;

    QDateTime cutoff = QDateTime::currentDateTime().addDays(-keepDays);
    QStringList filters;
    filters << "*_metrics.jsonl" << "*_anomalies.jsonl";
    QStringList files = dir.entryList(filters, QDir::Files);

    for (const QString& file : files) {
        QFileInfo info(dir.absoluteFilePath(file));
        if (info.lastModified() < cutoff) {
            QFile::remove(info.absoluteFilePath());
        }
    }
}

QString MonitoringStorage::metricsFilePath() const {
    return m_baseDir + QString("/%1_metrics.jsonl").arg(m_currentDate);
}

QString MonitoringStorage::anomaliesFilePath() const {
    return m_baseDir + QString("/%1_anomalies.jsonl").arg(m_currentDate);
}

void MonitoringStorage::ensureDateRolled() {
    QMutexLocker locker(&m_mutex);
    QString today = QDateTime::currentDateTime().toString("yyyyMMdd");
    if (today != m_currentDate) {
        m_currentDate = today;
        cleanupOldFiles();
    }
}

void MonitoringStorage::appendLine(const QString& filePath, const QString& line) {
    QMutexLocker locker(&m_mutex);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << line << "\n";
    file.close();
}

} // namespace QDV
