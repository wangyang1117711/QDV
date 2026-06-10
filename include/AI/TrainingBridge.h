#ifndef TRAININGBRIDGE_H
#define TRAININGBRIDGE_H

#include <QObject>
#include <QString>
#include <QProcess>
#include <QMap>
#include <QList>
#include <QPair>
#include <QVariantMap>
#include <QTimer>
#include <QElapsedTimer>

namespace QDV {

class TrainingBridge : public QObject {
    Q_OBJECT
public:
    explicit TrainingBridge(QObject* parent = nullptr);
    ~TrainingBridge();

    bool isTraining() const { return m_process != nullptr; }

    void startTraining(
        const QString& dataManifestPath,
        const QString& outputDir,
        const QString& modelType = "resnet18",
        int numEpochs = 10,
        int batchSize = 8,
        double learningRate = 0.001,
        const QString& pythonPath = "python"
    );

    void cancelTraining();

    static bool checkPythonEnvironment(const QString& pythonPath, QString& errorOut);

    static QString generateDatasetManifest(
        const QList<QPair<QString, QString>>& imageLabelPairs,
        const QStringList& allLabels,
        double validationSplit,
        const QString& outputPath
    );

signals:
    void trainingProgress(const QVariantMap& progress);
    void trainingCompleted(const QVariantMap& result);
    void trainingError(const QString& phase, const QString& message);
    void logOutput(const QString& message);

private slots:
    void onProcessReadyReadStandardOutput();
    void onProcessReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onHeartbeatCheck();

private:
    QProcess* m_process = nullptr;
    QString m_tempConfigPath;
    QString m_bufferedOutput;
    QTimer* m_heartbeatTimer = nullptr;
    QElapsedTimer m_lastOutputTime;
    bool m_receivedAnyOutput = false;
    bool m_completedEmitted = false;  // 防止 complete 信号被重复触发

    void writeTrainingConfig(
        const QString& dataManifestPath,
        const QString& modelType,
        int numEpochs,
        int batchSize,
        double learningRate,
        const QString& configPath
    );

    void parseAndEmitOutput(const QString& line);
};

} // namespace QDV

#endif
