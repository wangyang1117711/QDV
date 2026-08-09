#pragma once

#include "Monitoring/Anomaly.h"
#include "Monitoring/ProcessSnapshot.h"
#include "Core/Logger.h"

#include <QList>
#include <QString>
#include <QVariantMap>

namespace QDV {

// 异常检测器
// 基于连续快照与 Logger 错误记录识别资源异常与服务异常
class AnomalyDetector {
public:
    AnomalyDetector();

    // 评估当前快照，返回本次触发的异常列表（可能为空）
    QList<Anomaly> evaluate(const ProcessSnapshot& current,
                            const ProcessSnapshot& previous,
                            const QList<Logger::ErrorRecord>& recentErrors);

    // 配置阈值
    void setCpuThreshold(double percent, int consecutiveSamples);
    void setMemoryThreshold(quint64 bytes);
    void setDiskIoThreshold(quint64 bytesPerSample);
    void setLatencyThreshold(qint64 ms);
    void setErrorRateThreshold(int count, int seconds);

    // 进度停滞阈值：训练服务在线、任务 running，但 progress 在指定秒数内无变化
    // 即触发 warning。设为 0 可禁用该规则。
    void setProgressStallThreshold(int seconds) { m_progressStallThresholdSeconds = qMax(0, seconds); }

private:
    // 规则参数
    double m_cpuThresholdPercent = 80.0;
    int m_cpuConsecutiveSamples = 3;
    quint64 m_memoryThresholdBytes = 1024ULL * 1024 * 1024; // 1 GB
    quint64 m_diskIoThresholdBytes = 50ULL * 1024 * 1024;   // 50 MB
    qint64 m_latencyThresholdMs = 2000;
    int m_errorRateThresholdCount = 5;
    int m_errorRateThresholdSeconds = 60;
    int m_progressStallThresholdSeconds = 300;  // 默认 5 分钟

    // CPU 高占用持续计数
    int m_highCpuCount = 0;

    // 进度停滞追踪
    double m_lastProgress = -1.0;       // 上一次采样到的 progress（仅当 status==running 时记录）
    qint64 m_lastProgressChangeTs = 0;   // 上一次 progress 真正变化的时间戳
    bool m_progressStallAlerted = false; // 当前停滞周期内是否已告警过（progress 变化时复位）

    Anomaly createAnomaly(const QString& severity,
                          const QString& category,
                          const QString& message,
                          const ProcessSnapshot& snapshot,
                          const QVariantMap& context) const;
};

} // namespace QDV
