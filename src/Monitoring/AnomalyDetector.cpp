#include "Monitoring/AnomalyDetector.h"

#include <QUuid>
#include <QDateTime>

namespace QDV {

AnomalyDetector::AnomalyDetector() = default;

QList<Anomaly> AnomalyDetector::evaluate(const ProcessSnapshot& current,
                                         const ProcessSnapshot& previous,
                                         const QList<Logger::ErrorRecord>& recentErrors) {
    QList<Anomaly> result;

    if (current.timestamp == 0 || previous.timestamp == 0) {
        return result;
    }

    // 1. CPU 持续高占用
    if (current.cpuPercent > m_cpuThresholdPercent) {
        ++m_highCpuCount;
        if (m_highCpuCount >= m_cpuConsecutiveSamples) {
            QVariantMap ctx;
            ctx["rule"] = "cpu_high";
            ctx["threshold_percent"] = m_cpuThresholdPercent;
            ctx["current_percent"] = current.cpuPercent;
            ctx["consecutive_samples"] = m_highCpuCount;
            result.append(createAnomaly("warning", "cpu",
                QString("CPU 占用持续高于 %1%，当前 %2%")
                    .arg(m_cpuThresholdPercent).arg(current.cpuPercent, 0, 'f', 1),
                current, ctx));
        }
    } else {
        m_highCpuCount = 0;
    }

    // 2. 内存高占用
    if (current.workingSetBytes > m_memoryThresholdBytes) {
        QVariantMap ctx;
        ctx["rule"] = "memory_high";
        ctx["threshold_bytes"] = static_cast<qint64>(m_memoryThresholdBytes);
        ctx["current_bytes"] = static_cast<qint64>(current.workingSetBytes);
        double currentMb = current.workingSetBytes / (1024.0 * 1024.0);
        result.append(createAnomaly("warning", "memory",
            QString("工作集内存超过 %1 MB，当前 %2 MB")
                .arg(m_memoryThresholdBytes / (1024 * 1024))
                .arg(currentMb, 0, 'f', 1),
            current, ctx));
    }

    // 3. 磁盘 I/O 突增
    quint64 readDelta = (current.diskReadBytes > previous.diskReadBytes)
                        ? (current.diskReadBytes - previous.diskReadBytes) : 0;
    quint64 writeDelta = (current.diskWriteBytes > previous.diskWriteBytes)
                         ? (current.diskWriteBytes - previous.diskWriteBytes) : 0;
    quint64 totalDelta = readDelta + writeDelta;
    if (totalDelta > m_diskIoThresholdBytes) {
        QVariantMap ctx;
        ctx["rule"] = "disk_io_spike";
        ctx["threshold_bytes"] = static_cast<qint64>(m_diskIoThresholdBytes);
        ctx["total_delta_bytes"] = static_cast<qint64>(totalDelta);
        ctx["read_delta_bytes"] = static_cast<qint64>(readDelta);
        ctx["write_delta_bytes"] = static_cast<qint64>(writeDelta);
        result.append(createAnomaly("warning", "disk",
            QString("单周期磁盘 I/O 突增 %1 MB")
                .arg(totalDelta / (1024.0 * 1024.0), 0, 'f', 1),
            current, ctx));
    }

    // 4. 训练服务延迟或不可达
    if (!current.serverReachable) {
        QVariantMap ctx;
        ctx["rule"] = "server_unreachable";
        ctx["latency_ms"] = current.serverLatencyMs;
        result.append(createAnomaly("error", "network",
            QString("训练服务不可达"), current, ctx));
    } else if (current.serverLatencyMs > m_latencyThresholdMs) {
        QVariantMap ctx;
        ctx["rule"] = "server_latency_high";
        ctx["threshold_ms"] = m_latencyThresholdMs;
        ctx["latency_ms"] = current.serverLatencyMs;
        result.append(createAnomaly("warning", "network",
            QString("训练服务延迟 %1 ms，超过阈值 %2 ms")
                .arg(current.serverLatencyMs).arg(m_latencyThresholdMs),
            current, ctx));
    }

    // 5. 错误/警告频率突增
    if (!recentErrors.isEmpty()) {
        qint64 now = current.timestamp;
        int errorCount = 0;
        int criticalCount = 0;
        for (const auto& err : recentErrors) {
            QDateTime dt = QDateTime::fromString(err.timestamp, Qt::ISODate);
            if (dt.isValid() && dt.toMSecsSinceEpoch() >= now - m_errorRateThresholdSeconds * 1000) {
                if (err.level == Logger::Critical) {
                    ++criticalCount;
                } else if (err.level == Logger::Error) {
                    ++errorCount;
                }
            }
        }
        int total = errorCount + criticalCount;
        if (total > m_errorRateThresholdCount) {
            QVariantMap ctx;
            ctx["rule"] = "error_rate_high";
            ctx["threshold_count"] = m_errorRateThresholdCount;
            ctx["window_seconds"] = m_errorRateThresholdSeconds;
            ctx["error_count"] = errorCount;
            ctx["critical_count"] = criticalCount;
            result.append(createAnomaly("error", "error_rate",
                QString("最近 %1 秒内出现 %2 条 Error/Critical 记录")
                    .arg(m_errorRateThresholdSeconds).arg(total),
                current, ctx));
        }
    }

    // 6. 训练进度停滞：服务在线、任务 running，但 progress 长时间无变化
    //    这是语义级异常检测：服务返回正常但 progress 卡住，往往意味着训练回调未注册、
    //    trainer 内部异常或 GPU/CPU 训练死锁。
    if (m_progressStallThresholdSeconds > 0
        && current.serverReachable
        && current.serverJobStatus == "running"
        && current.serverProgress >= 0.0) {
        if (m_lastProgress < 0.0) {
            // 首次记录
            m_lastProgress = current.serverProgress;
            m_lastProgressChangeTs = current.timestamp;
            m_progressStallAlerted = false;
        } else {
            // progress 数值发生变化（允许浮点误差）
            if (qAbs(current.serverProgress - m_lastProgress) > 0.01) {
                m_lastProgress = current.serverProgress;
                m_lastProgressChangeTs = current.timestamp;
                m_progressStallAlerted = false;
            } else if (!m_progressStallAlerted) {
                // 同一停滞周期内仅告警一次，避免每 2 秒重复刷屏
                qint64 stalledMs = current.timestamp - m_lastProgressChangeTs;
                if (stalledMs >= m_progressStallThresholdSeconds * 1000LL) {
                    QVariantMap ctx;
                    ctx["rule"] = "progress_stall";
                    ctx["threshold_seconds"] = m_progressStallThresholdSeconds;
                    ctx["stalled_seconds"] = stalledMs / 1000;
                    ctx["current_progress"] = current.serverProgress;
                    ctx["job_status"] = current.serverJobStatus;
                    result.append(createAnomaly("warning", "progress",
                        QString("训练进度停滞 %1 秒（当前 %2%，任务状态 %3）")
                            .arg(stalledMs / 1000)
                            .arg(current.serverProgress, 0, 'f', 1)
                            .arg(current.serverJobStatus),
                        current, ctx));
                    m_progressStallAlerted = true;
                }
            }
        }
    } else {
        // 非运行态（无任务/已完成/失败/取消）时重置追踪，避免下次启动训练时误报
        m_lastProgress = -1.0;
        m_lastProgressChangeTs = 0;
        m_progressStallAlerted = false;
    }

    return result;
}

void AnomalyDetector::setCpuThreshold(double percent, int consecutiveSamples) {
    m_cpuThresholdPercent = percent;
    m_cpuConsecutiveSamples = qMax(1, consecutiveSamples);
}

void AnomalyDetector::setMemoryThreshold(quint64 bytes) {
    m_memoryThresholdBytes = bytes;
}

void AnomalyDetector::setDiskIoThreshold(quint64 bytesPerSample) {
    m_diskIoThresholdBytes = bytesPerSample;
}

void AnomalyDetector::setLatencyThreshold(qint64 ms) {
    m_latencyThresholdMs = ms;
}

void AnomalyDetector::setErrorRateThreshold(int count, int seconds) {
    m_errorRateThresholdCount = count;
    m_errorRateThresholdSeconds = qMax(1, seconds);
}

Anomaly AnomalyDetector::createAnomaly(const QString& severity,
                                       const QString& category,
                                       const QString& message,
                                       const ProcessSnapshot& snapshot,
                                       const QVariantMap& context) const {
    Anomaly a;
    a.anomalyId = QUuid::createUuid().toString();
    a.timestamp = snapshot.timestamp;
    a.severity = severity;
    a.category = category;
    a.message = message;
    a.snapshot = snapshot;
    a.context = context;
    return a;
}

} // namespace QDV
