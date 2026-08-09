#pragma once

#include <QString>
#include <QMetaType>

namespace QDV {

// 进程与训练/推理服务综合快照
struct ProcessSnapshot {
    qint64 timestamp = 0;
    quint32 pid = 0;
    double cpuPercent = 0.0;
    quint64 workingSetBytes = 0;
    quint64 privateBytes = 0;
    quint64 diskReadBytes = 0;
    quint64 diskWriteBytes = 0;
    int tcpConnectionCount = 0;
    bool serverReachable = false;
    qint64 serverLatencyMs = -1;
    quint32 serverPid = 0;
    double serverCpuPercent = -1.0;
    double serverProgress = -1.0;
    QString serverJobStatus;
    // 训练实时指标（本地训练或服务端训练上报）
    int trainingEpoch = 0;
    int trainingTotalEpochs = 0;
    double trainingLoss = -1.0;
    double trainingAccuracy = -1.0;
    double valLoss = -1.0;
    double valAccuracy = -1.0;
    qint64 trainingElapsedSeconds = -1;
    qint64 trainingRemainingSeconds = -1;
    // GPU 状态
    bool gpuAvailable = false;
    QString gpuName;
    int gpuUtilizationPercent = 0;
    int gpuMemoryUsedMB = 0;
    int gpuMemoryTotalMB = 0;
    int gpuTemperatureC = 0;
    QString gpuError;
    // 推理性能
    double lastInferenceLatencyMs = -1.0;
    double avgInferenceLatencyMs = -1.0;
    double inferenceThroughputPerMin = 0.0;
    // 模型加载
    int loadedModelCount = 0;
    QString lastModelLoadError;
};

} // namespace QDV

Q_DECLARE_METATYPE(QDV::ProcessSnapshot)
