#pragma once

#include "Monitoring/Anomaly.h"
#include "Monitoring/ProcessSnapshot.h"

#include <QString>
#include <QMutex>

namespace QDV {

// 监控数据持久化
// 以 JSONL 格式写入 baseDir 目录，按日期分文件，保留最近 7 天
class MonitoringStorage {
public:
    explicit MonitoringStorage(const QString& baseDir);

    // 追加一条进程快照
    void appendSnapshot(const ProcessSnapshot& snapshot);
    // 追加一条异常记录
    void appendAnomaly(const Anomaly& anomaly);

    // 清理超过保留天数的旧文件
    void cleanupOldFiles(int keepDays = 7);

private:
    QString m_baseDir;
    QMutex m_mutex;
    QString m_currentDate;

    QString metricsFilePath() const;
    QString anomaliesFilePath() const;
    void ensureDateRolled();
    void appendLine(const QString& filePath, const QString& line);
};

} // namespace QDV
