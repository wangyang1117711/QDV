#include "../catch2/catch2_minimal.hpp"
#include "Monitoring/SystemMetricsCollector.h"
#include "Monitoring/AnomalyDetector.h"
#include "Monitoring/MonitoringStorage.h"
#include "Monitoring/ProcessSnapshot.h"
#include "Monitoring/TrainingInferenceMonitor.h"
#include "Monitoring/Anomaly.h"
#include "Core/Logger.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QMetaObject>
#include <QCoreApplication>
#include <cmath>

using namespace QDV;

namespace {

// 构造一个基础快照，各字段为合法默认值
ProcessSnapshot makeSnapshot(qint64 ts) {
    ProcessSnapshot s;
    s.timestamp = ts;
    s.pid = 1234;
    s.cpuPercent = 10.0;
    s.workingSetBytes = 100 * 1024 * 1024;  // 100 MB
    s.privateBytes = 100 * 1024 * 1024;
    s.diskReadBytes = 0;
    s.diskWriteBytes = 0;
    s.tcpConnectionCount = 5;
    s.serverReachable = true;
    s.serverLatencyMs = 100;
    s.serverProgress = -1.0;
    s.serverJobStatus = "pending";
    s.lastInferenceLatencyMs = -1.0;
    s.loadedModelCount = 0;
    return s;
}

// 检查异常列表中是否包含指定类别
bool hasCategory(const QList<Anomaly>& anomalies, const QString& category) {
    for (const auto& a : anomalies) {
        if (a.category == category) return true;
    }
    return false;
}

} // namespace

TEST_CASE("SystemMetricsCollector 首次采集 CPU 为 0", "[monitoring]") {
    SystemMetricsCollector collector;
    ProcessSnapshot snapshot = collector.collect();
    REQUIRE(snapshot.cpuPercent == 0.0);
}

TEST_CASE("AnomalyDetector CPU 持续高占用触发", "[monitoring]") {
    AnomalyDetector detector;
    detector.setCpuThreshold(80.0, 3);

    ProcessSnapshot prev = makeSnapshot(1000);
    ProcessSnapshot curr = makeSnapshot(3000);
    curr.cpuPercent = 85.0;

    QList<Anomaly> anomalies;
    // 连续 3 次高占用应触发异常
    anomalies = detector.evaluate(curr, prev, {});
    REQUIRE(anomalies.isEmpty());
    anomalies = detector.evaluate(curr, prev, {});
    REQUIRE(anomalies.isEmpty());
    anomalies = detector.evaluate(curr, prev, {});
    REQUIRE(hasCategory(anomalies, "cpu"));
}

TEST_CASE("AnomalyDetector 内存高占用触发", "[monitoring]") {
    AnomalyDetector detector;
    detector.setMemoryThreshold(1024ULL * 1024 * 1024); // 1 GB

    ProcessSnapshot prev = makeSnapshot(1000);
    ProcessSnapshot curr = makeSnapshot(3000);
    curr.workingSetBytes = 2ULL * 1024 * 1024 * 1024; // 2 GB

    QList<Anomaly> anomalies = detector.evaluate(curr, prev, {});
    REQUIRE(hasCategory(anomalies, "memory"));
}

TEST_CASE("AnomalyDetector 磁盘 I/O 突增触发", "[monitoring]") {
    AnomalyDetector detector;
    detector.setDiskIoThreshold(50ULL * 1024 * 1024); // 50 MB

    ProcessSnapshot prev = makeSnapshot(1000);
    prev.diskReadBytes = 0;
    prev.diskWriteBytes = 0;
    ProcessSnapshot curr = makeSnapshot(3000);
    curr.diskReadBytes = 30ULL * 1024 * 1024;
    curr.diskWriteBytes = 30ULL * 1024 * 1024; // 合计 60 MB

    QList<Anomaly> anomalies = detector.evaluate(curr, prev, {});
    REQUIRE(hasCategory(anomalies, "disk"));
}

TEST_CASE("AnomalyDetector 训练服务不可达触发", "[monitoring]") {
    AnomalyDetector detector;

    ProcessSnapshot prev = makeSnapshot(1000);
    ProcessSnapshot curr = makeSnapshot(3000);
    curr.serverReachable = false;
    curr.serverLatencyMs = -1;

    QList<Anomaly> anomalies = detector.evaluate(curr, prev, {});
    REQUIRE(hasCategory(anomalies, "network"));
}

TEST_CASE("AnomalyDetector 训练服务延迟高触发", "[monitoring]") {
    AnomalyDetector detector;
    detector.setLatencyThreshold(2000);

    ProcessSnapshot prev = makeSnapshot(1000);
    ProcessSnapshot curr = makeSnapshot(3000);
    curr.serverReachable = true;
    curr.serverLatencyMs = 3000;

    QList<Anomaly> anomalies = detector.evaluate(curr, prev, {});
    REQUIRE(hasCategory(anomalies, "network"));
}

TEST_CASE("AnomalyDetector 错误频率突增触发", "[monitoring]") {
    AnomalyDetector detector;
    detector.setErrorRateThreshold(5, 60);

    ProcessSnapshot prev = makeSnapshot(1000);
    ProcessSnapshot curr = makeSnapshot(3000);

    QList<Logger::ErrorRecord> errors;
    qint64 now = curr.timestamp;
    for (int i = 0; i < 6; ++i) {
        Logger::ErrorRecord rec;
        rec.timestamp = QDateTime::fromMSecsSinceEpoch(now - i * 1000).toString(Qt::ISODate);
        rec.level = Logger::Error;
        rec.message = "test error";
        errors.append(rec);
    }

    QList<Anomaly> anomalies = detector.evaluate(curr, prev, errors);
    REQUIRE(hasCategory(anomalies, "error_rate"));
}

TEST_CASE("AnomalyDetector 训练进度停滞触发", "[monitoring]") {
    AnomalyDetector detector;
    detector.setProgressStallThreshold(1); // 1 秒即触发，便于测试

    ProcessSnapshot prev = makeSnapshot(1000);
    prev.serverReachable = true;
    prev.serverJobStatus = "running";
    prev.serverProgress = 0.5;

    // 第一次记录进度，不会触发
    ProcessSnapshot curr1 = makeSnapshot(3000);
    curr1.serverReachable = true;
    curr1.serverJobStatus = "running";
    curr1.serverProgress = 0.5;
    QList<Anomaly> anomalies = detector.evaluate(curr1, prev, {});
    REQUIRE_FALSE(hasCategory(anomalies, "progress"));

    // 间隔超过 1 秒且无变化，应触发
    ProcessSnapshot curr2 = makeSnapshot(5000);
    curr2.serverReachable = true;
    curr2.serverJobStatus = "running";
    curr2.serverProgress = 0.5;
    anomalies = detector.evaluate(curr2, curr1, {});
    REQUIRE(hasCategory(anomalies, "progress"));
}

TEST_CASE("MonitoringStorage 写入与清理", "[monitoring]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    MonitoringStorage storage(tempDir.path());

    ProcessSnapshot snapshot = makeSnapshot(QDateTime::currentDateTime().toMSecsSinceEpoch());
    snapshot.cpuPercent = 12.3;
    storage.appendSnapshot(snapshot);

    Anomaly anomaly;
    anomaly.anomalyId = "test-anomaly-1";
    anomaly.timestamp = snapshot.timestamp;
    anomaly.severity = "warning";
    anomaly.category = "cpu";
    anomaly.message = "CPU high";
    anomaly.snapshot = snapshot;
    storage.appendAnomaly(anomaly);

    QString metricsFile = tempDir.path() + "/" + QDateTime::currentDateTime().toString("yyyyMMdd") + "_metrics.jsonl";
    QString anomaliesFile = tempDir.path() + "/" + QDateTime::currentDateTime().toString("yyyyMMdd") + "_anomalies.jsonl";

    REQUIRE(QFile::exists(metricsFile));
    REQUIRE(QFile::exists(anomaliesFile));

    // 验证 metrics 文件内容
    QFile f(metricsFile);
    REQUIRE(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QByteArray data = f.readAll();
    REQUIRE(!data.isEmpty());
    QJsonDocument doc = QJsonDocument::fromJson(data);
    REQUIRE(doc.isObject());
    REQUIRE(doc.object()["cpu_percent"].toDouble() == 12.3);
    f.close();

    // 验证 anomalies 文件内容
    QFile fa(anomaliesFile);
    REQUIRE(fa.open(QIODevice::ReadOnly | QIODevice::Text));
    QByteArray adata = fa.readAll();
    REQUIRE(!adata.isEmpty());
    QJsonDocument adoc = QJsonDocument::fromJson(adata);
    REQUIRE(adoc.isObject());
    REQUIRE(adoc.object()["category"].toString() == "cpu");
    fa.close();

    // 清理不应删除当天文件
    storage.cleanupOldFiles(7);
    REQUIRE(QFile::exists(metricsFile));
    REQUIRE(QFile::exists(anomaliesFile));
}

TEST_CASE("TrainingInferenceMonitor 实时记录训练明细指标", "[monitoring]") {
    auto* monitor = TrainingInferenceMonitor::instance();
    monitor->stop();  // 避免定时器干扰

    TrainingProgressDetail detail;
    detail.progress = 0.25;
    detail.status = "running";
    detail.epoch = 5;
    detail.totalEpochs = 20;
    detail.trainLoss = 0.0434;
    detail.trainAcc = 0.9875;
    detail.valLoss = 0.0016;
    detail.valAcc = 1.0;
    detail.elapsedSeconds = 115;
    detail.remainingSeconds = 345;

    QSignalSpy spy(monitor, &TrainingInferenceMonitor::snapshotReady);
    monitor->recordTrainingProgress(detail);

    // 手动触发一次快照采集
    QMetaObject::invokeMethod(monitor, "onTick", Qt::DirectConnection);
    QCoreApplication::processEvents();

    REQUIRE(spy.count() >= 1);
    QVariant v = spy.takeFirst().at(0);
    QDV::ProcessSnapshot snapshot = v.value<QDV::ProcessSnapshot>();

    REQUIRE(snapshot.serverJobStatus == "running");
    REQUIRE(snapshot.trainingEpoch == 5);
    REQUIRE(snapshot.trainingTotalEpochs == 20);
    REQUIRE(std::abs(snapshot.trainingLoss - 0.0434) < 1e-6);
    REQUIRE(std::abs(snapshot.trainingAccuracy - 0.9875) < 1e-6);
    REQUIRE(std::abs(snapshot.valLoss - 0.0016) < 1e-6);
    REQUIRE(std::abs(snapshot.valAccuracy - 1.0) < 1e-6);
    REQUIRE(snapshot.trainingElapsedSeconds == 115);
    REQUIRE(snapshot.trainingRemainingSeconds == 345);
    REQUIRE(snapshot.serverReachable == true);
    REQUIRE(snapshot.serverLatencyMs == 0);
}
