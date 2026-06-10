#include "AI/TrainingBridge.h"
#include "Core/Logger.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QStandardPaths>
#include <QRandomGenerator>
#include <QUuid>
#include <QProcess>
#include <QCoreApplication>
#include <QDateTime>

namespace QDV {

TrainingBridge::TrainingBridge(QObject* parent)
    : QObject(parent), m_process(nullptr)
{
}

TrainingBridge::~TrainingBridge()
{
    cancelTraining();
}

bool TrainingBridge::checkPythonEnvironment(const QString& pythonPath, QString& errorOut)
{
    // 简化检查：只检查 Python 是否能启动，依赖在真正训练时再检查
    QProcess process;
    process.start(pythonPath, QStringList() << "--version");
    if (!process.waitForStarted(3000)) {
        errorOut = "无法启动 Python: " + process.errorString();
        return false;
    }
    if (!process.waitForFinished(5000)) {
        errorOut = "Python 执行超时";
        return false;
    }
    if (process.exitCode() != 0) {
        errorOut = "Python 返回错误: " + QString::fromUtf8(process.readAllStandardError());
        return false;
    }
    return true;
}

QString TrainingBridge::generateDatasetManifest(
    const QList<QPair<QString, QString>>& imageLabelPairs,
    const QStringList& allLabels,
    double validationSplit,
    const QString& outputPath)
{
    QJsonObject root;
    root["version"] = "1.0";
    root["task"] = "classification";
    root["num_classes"] = allLabels.size();

    QJsonArray labelsArray;
    for (const QString& label : allLabels) {
        labelsArray.append(label);
    }
    root["labels"] = labelsArray;

    QJsonArray imagesArray;

    QMap<QString, QList<QPair<QString, QString>>> groupedByLabel;
    for (const auto& pair : imageLabelPairs) {
        groupedByLabel[pair.second].append(pair);
    }

    for (auto it = groupedByLabel.begin(); it != groupedByLabel.end(); ++it) {
        const auto& pairs = it.value();
        int valCount = qMax(1, static_cast<int>(pairs.size() * validationSplit));
        QList<bool> isVal(pairs.size(), false);
        for (int i = 0; i < valCount; ++i) {
            isVal[i] = true;
        }
        for (int i = pairs.size() - 1; i > 0; --i) {
            int j = QRandomGenerator::global()->bounded(i + 1);
            std::swap(isVal[i], isVal[j]);
        }

        for (int i = 0; i < pairs.size(); ++i) {
            QJsonObject imgObj;
            imgObj["path"] = pairs[i].first;
            imgObj["label"] = pairs[i].second;
            imgObj["split"] = isVal[i] ? "val" : "train";
            imagesArray.append(imgObj);
        }
    }

    root["images"] = imagesArray;

    QJsonDocument doc(root);
    QFile file(outputPath);
    if (file.open(QIODevice::WriteOnly)) {
        // 必须使用 UTF-8 编码写入 JSON, 否则 Windows 下中文路径会被错误编码 (e.g. 图像 -> 鍥惧儚)
        // 写入 BOM 以确保 Python json.load 正确识别 UTF-8
        const char* utf8BOM = "\xEF\xBB\xBF";
        file.write(utf8BOM);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        QDV::Logger::info("[TrainingBridge] 生成数据集清单: " + outputPath);
        return outputPath;
    } else {
        QDV::Logger::error("[TrainingBridge] 无法写入清单: " + file.errorString());
        return "";
    }
}

void TrainingBridge::writeTrainingConfig(
    const QString& dataManifestPath,
    const QString& modelType,
    int numEpochs,
    int batchSize,
    double learningRate,
    const QString& configPath)
{
    QJsonObject config;
    config["data_manifest"] = dataManifestPath;
    config["model_type"] = modelType;
    config["num_epochs"] = numEpochs;
    config["image_size"] = 224;
    config["batch_size"] = batchSize;
    config["learning_rate"] = learningRate;

    QJsonDocument doc(config);
    QFile file(configPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
}

void TrainingBridge::startTraining(
    const QString& dataManifestPath,
    const QString& outputDir,
    const QString& modelType,
    int numEpochs,
    int batchSize,
    double learningRate,
    const QString& pythonPath)
{
    if (m_process != nullptr) {
        emit trainingError("启动", "训练已在进行中");
        return;
    }

    QDir().mkpath(outputDir);

    m_tempConfigPath = QDir::temp().absoluteFilePath("qdv_train_config_" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json");
    writeTrainingConfig(dataManifestPath, modelType, numEpochs, batchSize, learningRate, m_tempConfigPath);

    QString scriptPath = QCoreApplication::applicationDirPath() + "/../training/train.py";
    if (!QFile::exists(scriptPath)) {
        scriptPath = "E:/anchor/Trae/QDV/training/train.py";
    }

    QStringList args;
    args << scriptPath;
    args << "--config" << m_tempConfigPath;
    args << "--output_dir" << outputDir;

    m_process = new QProcess(this);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &TrainingBridge::onProcessReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &TrainingBridge::onProcessReadyReadStandardError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TrainingBridge::onProcessFinished);

    QDV::Logger::info(QString("[TrainingBridge] 启动训练: %1 %2").arg(pythonPath, args.join(" ")));
    emit logOutput("正在启动训练...");

    // 设置工作目录为训练脚本所在目录，确保能找到项目模块
    QString workDir = QFileInfo(scriptPath).absolutePath();
    m_process->setWorkingDirectory(workDir);
    QDV::Logger::info(QString("[TrainingBridge] 工作目录: %1").arg(workDir));

    // 启动心跳监控 - 检测 Python 进程是否无响应
    m_receivedAnyOutput = false;
    m_completedEmitted = false;  // 重置 complete 标志
    m_lastOutputTime.start();
    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(5000);  // 每5秒检查一次
    connect(m_heartbeatTimer, &QTimer::timeout, this, &TrainingBridge::onHeartbeatCheck);
    m_heartbeatTimer->start();

    m_process->start(pythonPath, args);
}

void TrainingBridge::cancelTraining()
{
    if (m_heartbeatTimer != nullptr) {
        m_heartbeatTimer->stop();
        m_heartbeatTimer->deleteLater();
        m_heartbeatTimer = nullptr;
    }

    if (m_process != nullptr) {
        // 先断开 finished 信号, 避免 terminate/kill 触发 onProcessFinished 又 emit 一次 trainingCompleted
        m_process->disconnect();

        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
        }
        m_process->deleteLater();
        m_process = nullptr;

        // 标记 complete 已处理, 阻止 onProcessFinished 兜底 emit
        m_completedEmitted = true;

        QVariantMap result;
        result["success"] = false;
        result["errorMessage"] = "训练被用户取消";
        emit trainingCompleted(result);
    }
}

void TrainingBridge::onProcessReadyReadStandardOutput()
{
    QByteArray data = m_process->readAllStandardOutput();
    QString output = QString::fromUtf8(data);
    m_bufferedOutput += output;

    // 收到任何输出都重置心跳
    m_receivedAnyOutput = true;
    m_lastOutputTime.restart();

    int pos;
    while ((pos = m_bufferedOutput.indexOf('\n')) != -1) {
        QString line = m_bufferedOutput.left(pos).trimmed();
        m_bufferedOutput = m_bufferedOutput.mid(pos + 1);
        if (!line.isEmpty()) {
            parseAndEmitOutput(line);
        }
    }
}

void TrainingBridge::onProcessReadyReadStandardError()
{
    QByteArray data = m_process->readAllStandardError();
    QString error = QString::fromUtf8(data);

    // 收到 stderr 输出也说明进程有响应，重置心跳
    m_receivedAnyOutput = true;
    m_lastOutputTime.restart();

    QDV::Logger::error("[TrainingBridge] Python stderr: " + error);
    emit logOutput("[错误] " + error);
}

void TrainingBridge::onHeartbeatCheck()
{
    if (m_process == nullptr) {
        return;
    }

    qint64 elapsed = m_lastOutputTime.elapsed();

    // 如果30秒内没有收到任何输出，认为进程卡住
    if (elapsed > 30000) {
        QString errMsg;
        if (!m_receivedAnyOutput) {
            // 从未收到任何输出 - 极有可能是 Python 启动失败（依赖加载卡死）
            errMsg = QString("Python 训练进程启动后 %1 秒内没有任何输出。\n\n"
                             "可能原因：\n"
                             "1. import torch/torchvision 卡死（Python 3.14 兼容性问题）\n"
                             "2. CUDA/驱动初始化失败\n"
                             "3. 缺少关键依赖库\n\n"
                             "建议排查：\n"
                             "1. 在命令行手动运行: python -c \"import torch; print(torch.__version__)\"\n"
                             "2. 检查 CUDA 是否可用: python -c \"import torch; print(torch.cuda.is_available())\"\n"
                             "3. 尝试使用更稳定的 Python 版本（如 3.10/3.11）").arg(elapsed / 1000);
        } else {
            // 之前有输出但停止了 - 训练过程中卡住
            errMsg = QString("Python 训练进程已 %1 秒未输出，疑似卡死。").arg(elapsed / 1000);
        }

        QDV::Logger::error("[TrainingBridge] 训练进程无响应: " + errMsg);
        emit trainingError("心跳超时", errMsg);
        emit logOutput("[超时] " + errMsg);

        // 强制终止进程
        m_process->kill();
        if (!m_process->waitForFinished(3000)) {
            QDV::Logger::error("[TrainingBridge] 无法终止卡死的 Python 进程");
        }
    }
}

void TrainingBridge::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus);

    // 停止心跳监控
    if (m_heartbeatTimer != nullptr) {
        m_heartbeatTimer->stop();
        m_heartbeatTimer->deleteLater();
        m_heartbeatTimer = nullptr;
    }

    // 优先处理残留缓冲区 (极少见: 进程退出时缓冲区可能还有未处理的完整 JSON 行)
    if (!m_completedEmitted && m_bufferedOutput.contains("\"type\":\"complete\"")) {
        QStringList lines = m_bufferedOutput.split('\n', Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            if (line.contains("\"type\":\"complete\"")) {
                parseAndEmitOutput(line.trimmed());
            }
        }
    }

    // 只有在从未发送过 complete 信号时, 才视为异常退出
    // 修复: 之前会重复 emit, 导致 UI 弹 "训练完成" 后又弹 "训练失败 (code 0)"
    if (!m_completedEmitted) {
        QDV::Logger::error(QString("[TrainingBridge] 训练进程异常退出，未收到 complete 信号 (exitCode=%1)").arg(exitCode));
        QVariantMap result;
        result["success"] = false;
        result["errorMessage"] = QString("训练进程异常退出 (code %1)").arg(exitCode);
        emit trainingCompleted(result);
    } else {
        QDV::Logger::info(QString("[TrainingBridge] 训练进程正常结束 (exitCode=%1, complete 信号已发送)").arg(exitCode));
    }

    if (!m_tempConfigPath.isEmpty()) {
        QFile::remove(m_tempConfigPath);
        m_tempConfigPath.clear();
    }

    if (m_process != nullptr) {
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void TrainingBridge::parseAndEmitOutput(const QString& line)
{
    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
    if (!doc.isObject()) {
        emit logOutput(line);
        return;
    }

    QJsonObject obj = doc.object();
    QString type = obj.value("type").toString();

    if (type == "progress") {
        QVariantMap prog;
        prog["epoch"] = obj.value("epoch").toInt();
        prog["totalEpochs"] = obj.value("total_epochs").toInt();
        prog["trainLoss"] = obj.value("train_loss").toDouble();
        prog["trainAccuracy"] = obj.value("train_acc").toDouble();
        prog["valLoss"] = obj.value("val_loss").toDouble();
        prog["valAccuracy"] = obj.value("val_acc").toDouble();
        prog["elapsedSeconds"] = obj.value("elapsed_sec").toDouble();
        emit trainingProgress(prog);

        QString logStr = QString("[Epoch %1/%2] Train Acc: %3%, Val Acc: %4%")
            .arg(prog["epoch"].toInt()).arg(prog["totalEpochs"].toInt())
            .arg(QString::number(prog["trainAccuracy"].toDouble() * 100, 'f', 1))
            .arg(QString::number(prog["valAccuracy"].toDouble() * 100, 'f', 1));
        emit logOutput(logStr);

    } else if (type == "complete") {
        // 标记已发出 complete 信号, 避免 onProcessFinished 重复 emit
        m_completedEmitted = true;

        QVariantMap result;
        result["success"] = obj.value("success").toBool();
        result["onnxPath"] = obj.value("onnx_path").toString();
        result["labelsPath"] = obj.value("labels_path").toString();
        result["totalTimeSeconds"] = obj.value("total_time_sec").toDouble();

        QJsonObject metricsObj = obj.value("metrics").toObject();
        for (auto it = metricsObj.begin(); it != metricsObj.end(); ++it) {
            result[it.key()] = it.value().toDouble();
        }

        emit trainingCompleted(result);

    } else if (type == "error") {
        QString phase = obj.value("phase").toString();
        QString message = obj.value("message").toString();
        emit trainingError(phase, message);
        emit logOutput("[错误] " + message);
    }
}

} // namespace QDV
