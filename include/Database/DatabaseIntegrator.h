#ifndef DATABASE_INTEGRATOR_H
#define DATABASE_INTEGRATOR_H

#include <QObject>
#include <QString>
#include "Core/DetectionStats.h"

class ResultDatabase;

class DatabaseIntegrator : public QObject {
    Q_OBJECT

public:
    static DatabaseIntegrator* instance();

    bool initialize(const QString& dbPath = "./data/qdv_results.db");
    void shutdown();

    bool saveDetectionResult(const QString& schemeId, const QString& schemeName,
                             const DetectionStats& stats);
    bool saveDetectionResult(const QString& schemeId, const QString& schemeName,
                             bool ok, double score, const QString& imagePath = QString());

    DetectionStats loadStatsForScheme(const QString& schemeId);

signals:
    void resultSaved(bool success);
    void errorOccurred(const QString& error);

private:
    DatabaseIntegrator(QObject* parent = nullptr);
    ~DatabaseIntegrator();

    ResultDatabase* m_db = nullptr;
    bool m_initialized = false;

    static DatabaseIntegrator* s_instance;
};

#endif