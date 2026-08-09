#pragma once

#include <QObject>
#include <QString>

class QTimer;
class QProcess;

namespace QDV {

// GPU 实时指标（单卡，取索引 0）
struct GpuMetrics {
    bool available = false;      // 是否成功获取到 GPU 信息
    QString name;                // GPU 名称，如 "NVIDIA GeForce RTX 3060"
    int utilizationPercent = 0;  // GPU 利用率 0~100
    int memoryUsedMB = 0;        // 已用显存 MB
    int memoryTotalMB = 0;       // 总显存 MB
    int temperatureC = 0;        // 温度（摄氏度）
    QString error;               // 失败时的简短错误信息
};

// 基于 nvidia-smi 异步采集 GPU 指标
// - 在 NVIDIA 驱动/硬件存在时返回真实利用率、显存、温度
// - 不存在或调用失败时 available=false，调用方可显示 "--"
class GpuMetricsCollector : public QObject {
    Q_OBJECT

public:
    explicit GpuMetricsCollector(QObject* parent = nullptr);
    ~GpuMetricsCollector() override;

    // 启动/停止周期性采集（默认 2000 ms）
    void start(int intervalMs = 2000);
    void stop();

    // 最近一次成功采集到的指标
    GpuMetrics lastMetrics() const;

signals:
    void metricsUpdated(const QDV::GpuMetrics& metrics);

private slots:
    void onUpdateTimeout();
    void onProcessFinished(int exitCode, int exitStatus);
    void onProcessError(int processError);

private:
    void parseOutput(const QString& output);

    QTimer* m_updateTimer = nullptr;
    QProcess* m_process = nullptr;
    GpuMetrics m_lastMetrics;
    bool m_busy = false;
};

} // namespace QDV

Q_DECLARE_METATYPE(QDV::GpuMetrics)
