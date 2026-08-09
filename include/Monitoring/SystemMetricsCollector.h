#pragma once

#include "Monitoring/ProcessSnapshot.h"

namespace QDV {

// 系统指标采集器
// 使用 Windows API 采集本进程的 CPU、内存、磁盘 I/O 与网络连接数
class SystemMetricsCollector {
public:
    SystemMetricsCollector();
    ~SystemMetricsCollector();

    // 单次采集，返回包含计算后 CPU 占用的快照
    ProcessSnapshot collect();

private:
    // 内部原始计数器，用于 CPU 差值计算与当前绝对值记录
    struct RawCounters {
        quint64 processKernelTime = 0;
        quint64 processUserTime = 0;
        quint64 systemIdleTime = 0;
        quint64 systemKernelTime = 0;
        quint64 systemUserTime = 0;
        quint64 workingSetBytes = 0;
        quint64 privateBytes = 0;
        quint64 diskReadBytes = 0;
        quint64 diskWriteBytes = 0;
        int tcpConnectionCount = 0;
        qint64 timestamp = 0;
    };

    RawCounters m_prev;
    bool m_hasPrev = false;

    // 获取本进程 ID
    quint32 currentPid() const;
    // 采集原始计数器
    bool collectRawCounters(RawCounters& out);
    // 计算两次采样间的 CPU 百分比
    double calculateCpuPercent(const RawCounters& prev, const RawCounters& curr) const;
};

} // namespace QDV
