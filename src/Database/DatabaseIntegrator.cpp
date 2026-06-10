#include "Database/DatabaseIntegrator.h"
#include "Database/ResultDatabase.h"
#include "Core/Logger.h"

using namespace QDV;

DatabaseIntegrator* DatabaseIntegrator::s_instance = nullptr;

DatabaseIntegrator::DatabaseIntegrator(QObject* parent) : QObject(parent) {
}

DatabaseIntegrator::~DatabaseIntegrator() {
    shutdown();
}

DatabaseIntegrator* DatabaseIntegrator::instance() {
    if (!s_instance) {
        s_instance = new DatabaseIntegrator();
    }
    return s_instance;
}

bool DatabaseIntegrator::initialize(const QString& dbPath) {
    if (m_initialized) return true;

    m_db = ResultDatabase::instance();
    bool ok = m_db->open(dbPath);
    if (ok) {
        m_initialized = true;
        Logger::info("Database integrator initialized: " + dbPath);
    } else {
        Logger::error("Database integrator failed to open: " + dbPath);
        emit errorOccurred("Failed to open database: " + dbPath);
    }
    return ok;
}

void DatabaseIntegrator::shutdown() {
    if (m_initialized) {
        m_db->close();
        m_initialized = false;
    }
}

bool DatabaseIntegrator::saveDetectionResult(const QString& schemeId, const QString& schemeName,
                                              const DetectionStats& stats) {
    if (!m_initialized) {
        Logger::warn("Database not initialized, cannot save result");
        return false;
    }

    bool ok = m_db->insertResult(schemeId, schemeName, stats.passed > stats.failed,
                                 stats.passRate, "");

    if (ok) {
        emit resultSaved(true);
        Logger::info(QString("Detection result saved: %1 total").arg(stats.totalDetected));
    } else {
        emit resultSaved(false);
        emit errorOccurred("Failed to save detection result");
    }

    return ok;
}

bool DatabaseIntegrator::saveDetectionResult(const QString& schemeId, const QString& schemeName,
                                              bool ok, double score, const QString& imagePath) {
    if (!m_initialized) {
        return false;
    }

    bool saved = m_db->insertResult(schemeId, schemeName, ok, score, imagePath);

    emit resultSaved(saved);
    return saved;
}

DetectionStats DatabaseIntegrator::loadStatsForScheme(const QString& schemeId) {
    DetectionStats stats;

    if (!m_initialized) return stats;

    auto results = m_db->queryResults(schemeId);
    stats.totalDetected = results.size();

    for (const auto& row : results) {
        bool ok = row.value("ok", false).toBool();
        if (ok) stats.passed++;
        else stats.failed++;
    }

    stats.passRate = stats.totalDetected > 0
        ? (static_cast<double>(stats.passed) / stats.totalDetected * 100.0) : 0.0;

    return stats;
}