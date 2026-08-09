#include "Monitoring/GpuMetricsCollector.h"

#include "Core/Logger.h"

#include <QCoreApplication>
#include <QProcess>
#include <QTimer>
#include <QStringList>

namespace QDV {

namespace {

// nvidia-smi 查询字段说明：
// name               : GPU 名称
// utilization.gpu    : GPU 利用率 (%)
// memory.used        : 已用显存 (MiB)
// memory.total       : 总显存 (MiB)
// temperature.gpu    : 温度 (C)
const char* NVIDIA_SMI_ARGS =
    "--query-gpu=name,utilization.gpu,memory.used,memory.total,temperature.gpu "
    "--format=csv,noheader,nounits";

// 解析 "NVIDIA GeForce RTX 3060, 45 %, 2048 MiB, 12288 MiB, 55" 这类输出
GpuMetrics parseCsvLine(const QString& line) {
    GpuMetrics metrics;
    metrics.available = false;

    QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return metrics;
    }

    // 按逗号拆分，保留名称中的逗号（GPU 名称通常不含逗号）
    QStringList parts = trimmed.split(',');
    if (parts.size() < 5) {
        return metrics;
    }

    auto toIntSafe = [](const QString& s) -> int {
        bool ok = false;
        int v = s.trimmed().split(' ').first().toInt(&ok);
        return ok ? v : 0;
    };

    metrics.name = parts[0].trimmed();
    metrics.utilizationPercent = toIntSafe(parts[1]);
    metrics.memoryUsedMB = toIntSafe(parts[2]);
    metrics.memoryTotalMB = toIntSafe(parts[3]);
    metrics.temperatureC = toIntSafe(parts[4]);
    metrics.available = !metrics.name.isEmpty() && metrics.memoryTotalMB > 0;
    return metrics;
}

} // namespace

GpuMetricsCollector::GpuMetricsCollector(QObject* parent)
    : QObject(parent)
    , m_updateTimer(new QTimer(this))
    , m_process(new QProcess(this)) {

    qRegisterMetaType<QDV::GpuMetrics>();

    m_updateTimer->setSingleShot(true);
    connect(m_updateTimer, &QTimer::timeout, this, &GpuMetricsCollector::onUpdateTimeout);

    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                onProcessFinished(exitCode, static_cast<int>(exitStatus));
            });
    connect(m_process,
            QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            this,
            [this](QProcess::ProcessError error) {
                onProcessError(static_cast<int>(error));
            });
}

GpuMetricsCollector::~GpuMetricsCollector() {
    stop();
}

void GpuMetricsCollector::start(int intervalMs) {
    if (m_updateTimer->isActive()) {
        return;
    }
    // 立即执行一次，让用户尽快看到 GPU 状态
    onUpdateTimeout();
    m_updateTimer->start(qMax(1000, intervalMs));
}

void GpuMetricsCollector::stop() {
    if (m_updateTimer->isActive()) {
        m_updateTimer->stop();
    }
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(500)) {
            m_process->kill();
        }
    }
}

GpuMetrics GpuMetricsCollector::lastMetrics() const {
    return m_lastMetrics;
}

void GpuMetricsCollector::onUpdateTimeout() {
    if (m_busy || m_process->state() != QProcess::NotRunning) {
        // 如果上一次还没结束，跳过本次，避免堆积
        return;
    }

    QString program = "nvidia-smi";
    QStringList args = QString(NVIDIA_SMI_ARGS).split(' ', Qt::SkipEmptyParts);

    m_busy = true;
    m_process->start(program, args);

    // 安全兜底：3 秒后若进程仍未结束，强制杀掉，避免 hang 住
    QTimer::singleShot(3000, this, [this]() {
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
        }
    });
}

void GpuMetricsCollector::onProcessFinished(int exitCode, int exitStatus) {
    Q_UNUSED(exitStatus);
    m_busy = false;

    if (exitCode != 0) {
        m_lastMetrics = GpuMetrics();
        m_lastMetrics.error = QString("nvidia-smi exitCode=%1").arg(exitCode);
        emit metricsUpdated(m_lastMetrics);
        return;
    }

    QString output = QString::fromLocal8Bit(m_process->readAllStandardOutput());
    parseOutput(output);
}

void GpuMetricsCollector::onProcessError(int processError) {
    Q_UNUSED(processError);
    m_busy = false;

    m_lastMetrics = GpuMetrics();
    m_lastMetrics.error = "nvidia-smi not available";
    emit metricsUpdated(m_lastMetrics);
}

void GpuMetricsCollector::parseOutput(const QString& output) {
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        m_lastMetrics = GpuMetrics();
        m_lastMetrics.error = "nvidia-smi output empty";
        emit metricsUpdated(m_lastMetrics);
        return;
    }

    // 仅取第一块 GPU 的指标
    GpuMetrics metrics = parseCsvLine(lines.first());
    if (!metrics.available) {
        metrics.error = "failed to parse nvidia-smi output";
    }
    m_lastMetrics = metrics;
    emit metricsUpdated(m_lastMetrics);
}

} // namespace QDV
