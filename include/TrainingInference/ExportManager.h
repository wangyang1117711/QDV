#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QJsonObject>

struct InferenceResult {
    QString imagePath;
    QString category;
    double confidence;
    QJsonObject rawOutput;
};

class ExportManager : public QObject {
    Q_OBJECT

public:
    static ExportManager* instance();

    bool exportToCSV(const QString& filePath, const QList<InferenceResult>& results);
    bool exportToJSON(const QString& filePath, const QList<InferenceResult>& results);
    bool exportToTXT(const QString& filePath, const QList<InferenceResult>& results);

signals:
    void exportCompleted(const QString& filePath, int recordCount);
    void exportFailed(const QString& error);

private:
    ExportManager(QObject* parent = nullptr);
    static ExportManager* s_instance;
};