// =====================================================================
// TrainingBridge.h — 标训一键闭环的训练桥（独立自包含，无主工程依赖）
//
// 职责：把标注工具导出的 YOLO 数据集（dataset.yaml + images/ + labels/）
// 喂给项目 training/yolo_train.py，通过 QProcess 异步执行（即「训练线程」），
// 解析其 JSON-Lines 进度协议（progress/log/complete/error），向前端转发。
//
// 与 QDV::TrainingBridge 协议保持一致（progress/complete/error/log），
// 但配置 schema 对齐 yolo_train.py：
//   {model_type, num_epochs, batch_size, image_size, learning_rate,
//    data_yaml, output_dir}
// 启动：python yolo_train.py --config <cfg> --output_dir <out>
//
// 设计说明：QProcess 内部在独立线程运行训练进程，UI 不阻塞，
// 满足「接训练线程形成标训一键闭环」的需求。
// =====================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QProcess>
#include <QVariantMap>
#include <QTimer>
#include <QElapsedTimer>

class TrainingBridge : public QObject
{
    Q_OBJECT
public:
    explicit TrainingBridge(QObject* parent = nullptr);
    ~TrainingBridge();

    bool isTraining() const { return m_process != nullptr; }

    // 启动 YOLO 训练
    //   dataYaml : 标注工具导出（yolo_detect）的 dataset.yaml 绝对路径
    //   outputDir: 模型输出目录（同时作为 --output_dir）
    //   options  : {pythonPath, modelType, numEpochs, batchSize,
    //               imageSize, learningRate}
    // 返回 true 表示已成功拉起训练进程
    bool startTraining(const QString& dataYaml,
                       const QString& outputDir,
                       const QVariantMap& options = QVariantMap());

    void cancelTraining();

    // 真实检测 Python 环境：除 --version 外，还验证 ultralytics/torch/CUDA 能否正常 import，
    // 返回 QVariantMap {ok:bool, detail:str, version:str, cuda:bool} 供 QML 显示
    Q_INVOKABLE static QVariantMap checkPythonEnvironment(const QString& pythonPath);

    // 兼容旧调用：仅 --version 检测（建议改用 checkPythonEnvironment 返回更详细信息）
    static bool checkPython(const QString& pythonPath, QString& errorOut);

signals:
    void trainingProgress(const QVariantMap& progress);
    void trainingCompleted(const QVariantMap& result);
    void trainingError(const QString& phase, const QString& message);
    void logOutput(const QString& message);

private slots:
    void onReadyOut();
    void onReadyErr();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onHeartbeat();

private:
    void parseLine(const QString& line);
    QString resolveScript() const;
    void writeConfig(const QString& dataYaml, const QString& outputDir,
                     const QVariantMap& options, const QString& configPath);

    QProcess* m_process = nullptr;
    QString m_configPath;
    QString m_buf;
    QTimer* m_hb = nullptr;
    QElapsedTimer m_lastOut;
    bool m_gotOut = false;
    bool m_done = false;  // 防止 complete 信号重复触发
};
