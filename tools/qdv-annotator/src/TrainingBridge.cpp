// =====================================================================
// TrainingBridge.cpp — 训练桥实现（对齐 yolo_train.py 协议）
// =====================================================================
#include "TrainingBridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

TrainingBridge::TrainingBridge(QObject* parent)
    : QObject(parent), m_process(nullptr)
{
}

TrainingBridge::~TrainingBridge()
{
    cancelTraining();
}

// 真实检测 Python 环境：除 --version 外，还验证 ultralytics/torch/CUDA 能否正常 import，
// 返回 QVariantMap {ok:bool, detail:str, version:str, cuda:bool} 供 QML 显示
QVariantMap TrainingBridge::checkPythonEnvironment(const QString& pythonPath)
{
    QVariantMap result;
    result["version"] = "";
    result["cuda"] = false;
    QString err;

    // 1) python 可启动
    {
        QProcess p;
        p.start(pythonPath, QStringList() << "--version");
        if (!p.waitForStarted(3000)) {
            result["ok"] = false;
            result["detail"] = "无法启动: " + p.errorString();
            return result;
        }
        if (!p.waitForFinished(5000)) {
            result["ok"] = false;
            result["detail"] = "执行 --version 超时";
            return result;
        }
        if (p.exitCode() != 0) {
            result["ok"] = false;
            result["detail"] = "返回错误: " + QString::fromUtf8(p.readAllStandardError());
            return result;
        }
        result["version"] = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    }

    // 2) ultralytics + torch + CUDA 可 import（这是训练真正需要的）
    QProcess p2;
    p2.start(pythonPath, QStringList() << "-c"
        << "import ultralytics, torch, sys; "
           "print('VER', ultralytics.__version__); "
           "print('TORCH', torch.__version__); "
           "print('CUDA', torch.cuda.is_available())");
    if (!p2.waitForStarted(3000)) {
        result["ok"] = false;
        result["detail"] = "Python 可启动，但无法启动子进程验证依赖";
        return result;
    }
    if (!p2.waitForFinished(15000)) {
        result["ok"] = false;
        result["detail"] = "验证 ultralytics/torch 超时（依赖缺失？）";
        return result;
    }
    QString out = QString::fromUtf8(p2.readAllStandardOutput());
    QString errOut = QString::fromUtf8(p2.readAllStandardError());
    if (p2.exitCode() != 0) {
        result["ok"] = false;
        // 提取根因（ModuleNotFoundError 等）
        QString root = errOut;
        int nl = root.indexOf('\n');
        if (nl > 0) root = root.left(nl).trimmed();
        if (root.isEmpty()) root = "依赖验证失败";
        result["detail"] = QString("ultralytics/torch 不可用: %1").arg(root);
        return result;
    }
    // 解析 VER / TORCH / CUDA 行
    QString ultrav = "", torchV = "", cudaStr = "False";
    for (const QString& line : out.split('\n')) {
        if (line.startsWith("VER "))    ultrav = line.mid(4).trimmed();
        else if (line.startsWith("TORCH ")) torchV = line.mid(6).trimmed();
        else if (line.startsWith("CUDA "))  cudaStr = line.mid(5).trimmed();
    }
    result["cuda"] = (cudaStr == "True");
    result["ok"] = true;
    result["detail"] = QString("ultralytics=%1, torch=%2, CUDA=%3")
                           .arg(ultrav.isEmpty() ? "?" : ultrav)
                           .arg(torchV.isEmpty() ? "?" : torchV)
                           .arg(cudaStr);
    return result;
}

// 兼容旧接口：仅 --version 检测
bool TrainingBridge::checkPython(const QString& pythonPath, QString& errorOut)
{
    QProcess p;
    p.start(pythonPath, QStringList() << "--version");
    if (!p.waitForStarted(3000)) { errorOut = "无法启动: " + p.errorString(); return false; }
    if (!p.waitForFinished(5000)) { errorOut = "执行超时"; return false; }
    if (p.exitCode() != 0) { errorOut = QString::fromUtf8(p.readAllStandardError()); return false; }
    return true;
}

// 解析 training/yolo_train.py 的绝对路径（多候选回退）
QString TrainingBridge::resolveScript() const
{
    QStringList candidates;
    // 1) 环境变量显式指定
    if (!qEnvironmentVariable("QDV_TRAIN_SCRIPT").isEmpty())
        candidates << qEnvironmentVariable("QDV_TRAIN_SCRIPT");
    // 2) 工具所在目录的上层 training/
    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << QDir(appDir).absoluteFilePath("../training/yolo_train.py");
    candidates << QDir(appDir).absoluteFilePath("training/yolo_train.py");
    // 3) 项目仓库绝对路径（开发期回退）
    candidates << "E:/anchor/Trae/QDV/training/yolo_train.py";
    candidates << "/e/anchor/Trae/QDV/training/yolo_train.py";
    for (const QString& c : candidates)
        if (QFile::exists(c)) return QDir(c).absolutePath();
    return candidates.last(); // 仍返回最后一个，便于报错提示
}

void TrainingBridge::writeConfig(const QString& dataYaml,
                                 const QString& outputDir,
                                 const QVariantMap& options,
                                 const QString& configPath)
{
    QJsonObject cfg;
    cfg["model_type"]     = options.value("modelType", "yolov8n").toString();
    cfg["num_epochs"]     = options.value("numEpochs", 50).toInt();
    cfg["batch_size"]     = options.value("batchSize", 16).toInt();
    cfg["image_size"]     = options.value("imageSize", 640).toInt();
    cfg["learning_rate"]  = options.value("learningRate", 0.01).toDouble();
    cfg["data_yaml"]      = QDir(dataYaml).absolutePath();
    cfg["output_dir"]     = QDir(outputDir).absolutePath();

    QJsonDocument doc(cfg);
    QFile f(configPath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(doc.toJson(QJsonDocument::Indented));
        f.close();
    }
}

bool TrainingBridge::startTraining(const QString& dataYaml,
                                   const QString& outputDir,
                                   const QVariantMap& options)
{
    if (m_process != nullptr) {
        emit trainingError("启动", "训练已在进行中");
        return false;
    }
    if (dataYaml.isEmpty() || !QFile::exists(dataYaml)) {
        emit trainingError("启动", "dataset.yaml 不存在: " + dataYaml);
        return false;
    }

    QDir().mkpath(outputDir);
    const QString script = resolveScript();
    if (!QFile::exists(script)) {
        emit trainingError("启动", "训练脚本未找到: " + script
                           + "\n请确认 training/yolo_train.py 存在或设置 QDV_TRAIN_SCRIPT 环境变量");
        return false;
    }

    const QString python = options.value("pythonPath", "python").toString();
    if (python.isEmpty()) {
        emit trainingError("启动", "未指定 Python 解释器路径");
        return false;
    }

    m_configPath = QDir::temp().absoluteFilePath(
        "qdv_train_config_" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json");
    writeConfig(dataYaml, outputDir, options, m_configPath);

    QStringList args;
    args << script
         << "--config" << m_configPath
         << "--output_dir" << QDir(outputDir).absolutePath();

    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &TrainingBridge::onReadyOut);
    connect(m_process, &QProcess::readyReadStandardError, this, &TrainingBridge::onReadyErr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TrainingBridge::onFinished);

    emit logOutput(QString("[TrainingBridge] 启动: %1 %2").arg(python, args.join(" ")));

    // 工作目录设为脚本所在目录，确保能 import 项目内模块
    m_process->setWorkingDirectory(QFileInfo(script).absolutePath());

    // 心跳：检测训练进程无响应（首帧可能因 torch/CUDA 初始化耗时，放宽到 60s）
    m_gotOut = false;
    m_done = false;
    m_lastOut.start();
    m_hb = new QTimer(this);
    m_hb->setInterval(5000);
    connect(m_hb, &QTimer::timeout, this, &TrainingBridge::onHeartbeat);
    m_hb->start();

    m_process->start(python, args);
    if (!m_process->waitForStarted(10000)) {
        emit trainingError("启动", "训练进程启动失败: " + m_process->errorString());
        m_process->deleteLater();
        m_process = nullptr;
        return false;
    }
    return true;
}

void TrainingBridge::cancelTraining()
{
    if (m_hb) { m_hb->stop(); m_hb->deleteLater(); m_hb = nullptr; }
    if (m_process) {
        m_process->disconnect();
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_done = true; // 阻止 onFinished 兜底再发一次
    if (!m_configPath.isEmpty()) { QFile::remove(m_configPath); m_configPath.clear(); }
}

void TrainingBridge::onReadyOut()
{
    if (!m_process) return;
    m_buf += QString::fromUtf8(m_process->readAllStandardOutput());
    m_gotOut = true;
    m_lastOut.restart();
    int pos;
    while ((pos = m_buf.indexOf('\n')) != -1) {
        QString line = m_buf.left(pos).trimmed();
        m_buf = m_buf.mid(pos + 1);
        if (!line.isEmpty()) parseLine(line);
    }
}

void TrainingBridge::onReadyErr()
{
    if (!m_process) return;
    QString err = QString::fromUtf8(m_process->readAllStandardError());
    m_gotOut = true;
    m_lastOut.restart();
    emit logOutput("[stderr] " + err);
}

void TrainingBridge::onHeartbeat()
{
    if (!m_process) return;
    const qint64 elapsed = m_lastOut.elapsed();
    if (elapsed > 60000) {
        QString msg = m_gotOut
            ? QString("训练进程已 %1 秒未输出，疑似卡死。").arg(elapsed / 1000)
            : QString("训练进程启动后 %1 秒无输出（可能 import torch/CUDA 初始化失败或 Python 环境缺失）。")
                  .arg(elapsed / 1000);
        emit trainingError("心跳超时", msg);
        emit logOutput("[超时] " + msg);
        m_process->kill();
    }
}

void TrainingBridge::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus);
    if (m_hb) { m_hb->stop(); m_hb->deleteLater(); m_hb = nullptr; }

    // 兜底处理残留缓冲区中的 complete
    if (!m_done && m_buf.contains("\"type\":\"complete\"")) {
        const QStringList lines = m_buf.split('\n', Qt::SkipEmptyParts);
        for (const QString& line : lines)
            if (line.contains("\"type\":\"complete\"")) parseLine(line.trimmed());
    }

    if (!m_done) {
        emit trainingError("异常退出",
                           QString("训练进程异常退出 (code %1)，未收到 complete 信号。").arg(exitCode));
    }
    if (!m_configPath.isEmpty()) { QFile::remove(m_configPath); m_configPath.clear(); }
    if (m_process) { m_process->deleteLater(); m_process = nullptr; }
}

void TrainingBridge::parseLine(const QString& line)
{
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
    if (!doc.isObject()) { emit logOutput(line); return; }
    const QJsonObject obj = doc.object();
    const QString type = obj.value("type").toString();

    if (type == "progress") {
        QVariantMap prog;
        prog["epoch"]          = obj.value("epoch").toInt();
        prog["totalEpochs"]    = obj.value("total_epochs").toInt();
        prog["trainLoss"]      = obj.value("train_loss").toDouble();
        prog["trainAccuracy"]  = obj.value("train_acc").toDouble();
        prog["valLoss"]        = obj.value("val_loss").toDouble();
        prog["valAccuracy"]    = obj.value("val_acc").toDouble();
        prog["elapsedSeconds"] = obj.value("elapsed_sec").toDouble();
        emit trainingProgress(prog);
    } else if (type == "complete") {
        m_done = true;
        QVariantMap result;
        result["success"]       = obj.value("success").toBool();
        result["onnxPath"]      = obj.value("onnx_path").toString();
        result["labelsPath"]    = obj.value("labels_path").toString();
        result["totalTimeSeconds"] = obj.value("total_time_sec").toDouble();
        const QJsonObject metrics = obj.value("metrics").toObject();
        for (auto it = metrics.begin(); it != metrics.end(); ++it)
            result[it.key()] = it.value().toDouble();
        emit trainingCompleted(result);
    } else if (type == "error") {
        emit trainingError(obj.value("phase").toString(), obj.value("message").toString());
        emit logOutput("[错误] " + obj.value("message").toString());
    } else if (type == "log") {
        emit logOutput(obj.value("message").toString());
    } else {
        emit logOutput(line);
    }
}
