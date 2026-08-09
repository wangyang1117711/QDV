#include "Monitoring/SystemMetricsCollector.h"

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <winsock2.h>
#endif

#include <QDateTime>
#include <QByteArray>

namespace QDV {

SystemMetricsCollector::SystemMetricsCollector() = default;
SystemMetricsCollector::~SystemMetricsCollector() = default;

ProcessSnapshot SystemMetricsCollector::collect() {
    ProcessSnapshot snapshot;
    snapshot.timestamp = QDateTime::currentDateTime().toMSecsSinceEpoch();
    snapshot.pid = currentPid();

    RawCounters curr;
    if (!collectRawCounters(curr)) {
        return snapshot;
    }

    // 内存、磁盘 I/O、网络连接数使用当前绝对值
    snapshot.workingSetBytes = curr.workingSetBytes;
    snapshot.privateBytes = curr.privateBytes;
    snapshot.diskReadBytes = curr.diskReadBytes;
    snapshot.diskWriteBytes = curr.diskWriteBytes;
    snapshot.tcpConnectionCount = curr.tcpConnectionCount;

    // 首次采集没有前次数据，无法计算 CPU 差值，仅保存原始计数器
    if (!m_hasPrev) {
        m_prev = curr;
        m_hasPrev = true;
        return snapshot;
    }

    // 计算 CPU 占用百分比
    snapshot.cpuPercent = calculateCpuPercent(m_prev, curr);

    m_prev = curr;
    return snapshot;
}

quint32 SystemMetricsCollector::currentPid() const {
#ifdef Q_OS_WIN
    return static_cast<quint32>(GetCurrentProcessId());
#else
    return 0;
#endif
}

bool SystemMetricsCollector::collectRawCounters(RawCounters& out) {
#ifdef Q_OS_WIN
    out.timestamp = QDateTime::currentDateTime().toMSecsSinceEpoch();

    // 进程时间
    FILETIME createTime, exitTime, kernelTime, userTime;
    if (!GetProcessTimes(GetCurrentProcess(), &createTime, &exitTime, &kernelTime, &userTime)) {
        return false;
    }

    // 系统时间
    FILETIME sysIdle, sysKernel, sysUser;
    if (!GetSystemTimes(&sysIdle, &sysKernel, &sysUser)) {
        return false;
    }

    // 内存信息
    PROCESS_MEMORY_COUNTERS_EX memCounters;
    ZeroMemory(&memCounters, sizeof(memCounters));
    memCounters.cb = sizeof(memCounters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memCounters),
                              sizeof(memCounters))) {
        return false;
    }

    // 磁盘 I/O
    IO_COUNTERS ioCounters;
    ZeroMemory(&ioCounters, sizeof(ioCounters));
    if (!GetProcessIoCounters(GetCurrentProcess(), &ioCounters)) {
        return false;
    }

    // TCP 连接数
    int tcpCount = 0;
    DWORD tableSize = 0;
    if (GetTcpTable(nullptr, &tableSize, FALSE) == ERROR_INSUFFICIENT_BUFFER) {
        QByteArray buffer(static_cast<int>(tableSize), 0);
        auto* table = reinterpret_cast<MIB_TCPTABLE*>(buffer.data());
        if (GetTcpTable(table, &tableSize, FALSE) == NO_ERROR) {
            tcpCount = static_cast<int>(table->dwNumEntries);
        }
    }

    auto fileTimeToUint64 = [](const FILETIME& ft) -> quint64 {
        return (static_cast<quint64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };

    out.processKernelTime = fileTimeToUint64(kernelTime);
    out.processUserTime = fileTimeToUint64(userTime);
    out.systemIdleTime = fileTimeToUint64(sysIdle);
    out.systemKernelTime = fileTimeToUint64(sysKernel);
    out.systemUserTime = fileTimeToUint64(sysUser);
    out.workingSetBytes = memCounters.WorkingSetSize;
    out.privateBytes = memCounters.PrivateUsage;
    out.diskReadBytes = ioCounters.ReadTransferCount;
    out.diskWriteBytes = ioCounters.WriteTransferCount;
    out.tcpConnectionCount = tcpCount;

    return true;
#else
    Q_UNUSED(out)
    return false;
#endif
}

double SystemMetricsCollector::calculateCpuPercent(const RawCounters& prev, const RawCounters& curr) const {
    quint64 processDelta = (curr.processKernelTime - prev.processKernelTime)
                         + (curr.processUserTime - prev.processUserTime);
    // systemKernelTime 已包含 idle 时间，因此系统总时间为 kernel + user
    quint64 systemDelta = (curr.systemKernelTime - prev.systemKernelTime)
                        + (curr.systemUserTime - prev.systemUserTime);

    if (systemDelta == 0) {
        return 0.0;
    }

    double percent = static_cast<double>(processDelta) / static_cast<double>(systemDelta) * 100.0;
    // 限制在合理范围，避免时钟回拨等异常导致负值或超大值
    if (percent < 0.0) percent = 0.0;
    if (percent > 100.0) percent = 100.0;
    return percent;
}

} // namespace QDV
