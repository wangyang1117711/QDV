#include "ResultDatabase.h"
#include "Logger.h"

ResultDatabase* ResultDatabase::s_instance = nullptr;

ResultDatabase::ResultDatabase(QObject* parent) : QObject(parent) {
}

ResultDatabase::~ResultDatabase() {
    close();
}

ResultDatabase* ResultDatabase::instance() {
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    
    if (!s_instance) {
        s_instance = new ResultDatabase();
    }
    return s_instance;
}

bool ResultDatabase::open(const QString& dbPath) {
    QMutexLocker locker(&m_mutex);
    
    m_db = QSqlDatabase::addDatabase("QSQLITE", "QDVResults");
    m_db.setDatabaseName(dbPath);
    
    if (!m_db.open()) {
        Logger::error("Failed to open database: " + dbPath);
        return false;
    }
    
    return createTables();
}

void ResultDatabase::close() {
    QMutexLocker locker(&m_mutex);
    
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool ResultDatabase::createTables() {
    QMutexLocker locker(&m_mutex);
    
    QSqlQuery query(m_db);
    
    QString createTable = R"(
        CREATE TABLE IF NOT EXISTS results (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            scheme_id TEXT NOT NULL,
            scheme_name TEXT NOT NULL,
            ok INTEGER NOT NULL,
            score REAL NOT NULL,
            image_path TEXT,
            timestamp TEXT NOT NULL,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
    )";
    
    bool success = query.exec(createTable);
    if (!success) {
        Logger::error("Failed to create results table");
    }
    
    query.exec("CREATE INDEX IF NOT EXISTS idx_scheme_id ON results(scheme_id)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_timestamp ON results(timestamp)");
    
    return success;
}

bool ResultDatabase::insertResult(const QString& schemeId, const QString& schemeName,
                                  bool ok, double score, const QString& imagePath,
                                  const QString& timestamp) {
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        return false;
    }
    
    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO results (scheme_id, scheme_name, ok, score, image_path, timestamp)
        VALUES (:scheme_id, :scheme_name, :ok, :score, :image_path, :timestamp)
    )");
    
    query.bindValue(":scheme_id", schemeId);
    query.bindValue(":scheme_name", schemeName);
    query.bindValue(":ok", ok ? 1 : 0);
    query.bindValue(":score", score);
    query.bindValue(":image_path", imagePath);
    query.bindValue(":timestamp", timestamp.isEmpty() ? QDateTime::currentDateTime().toString(Qt::ISODate) : timestamp);
    
    bool success = query.exec();
    emit resultInserted(success);
    
    if (!success) {
        Logger::error("Failed to insert result: " + query.lastError().text());
    }
    
    return success;
}

QList<QMap<QString, QVariant>> ResultDatabase::queryResults(const QString& schemeId,
                                                             const QString& startTime,
                                                             const QString& endTime) {
    QMutexLocker locker(&m_mutex);
    
    QList<QMap<QString, QVariant>> results;
    
    if (!m_db.isOpen()) {
        return results;
    }
    
    QString queryStr = "SELECT * FROM results WHERE 1=1";
    
    if (!schemeId.isEmpty()) {
        queryStr += " AND scheme_id = :scheme_id";
    }
    if (!startTime.isEmpty()) {
        queryStr += " AND timestamp >= :start_time";
    }
    if (!endTime.isEmpty()) {
        queryStr += " AND timestamp <= :end_time";
    }
    
    queryStr += " ORDER BY timestamp DESC LIMIT 1000";
    
    QSqlQuery query(m_db);
    query.prepare(queryStr);
    
    if (!schemeId.isEmpty()) {
        query.bindValue(":scheme_id", schemeId);
    }
    if (!startTime.isEmpty()) {
        query.bindValue(":start_time", startTime);
    }
    if (!endTime.isEmpty()) {
        query.bindValue(":end_time", endTime);
    }
    
    if (query.exec()) {
        while (query.next()) {
            QMap<QString, QVariant> result;
            result["id"] = query.value("id").toInt();
            result["scheme_id"] = query.value("scheme_id").toString();
            result["scheme_name"] = query.value("scheme_name").toString();
            result["ok"] = query.value("ok").toInt() == 1;
            result["score"] = query.value("score").toDouble();
            result["image_path"] = query.value("image_path").toString();
            result["timestamp"] = query.value("timestamp").toString();
            results.append(result);
        }
    }
    
    return results;
}

int ResultDatabase::countResults(const QString& schemeId) {
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        return 0;
    }
    
    QSqlQuery query(m_db);
    if (schemeId.isEmpty()) {
        query.exec("SELECT COUNT(*) FROM results");
    } else {
        query.prepare("SELECT COUNT(*) FROM results WHERE scheme_id = :scheme_id");
        query.bindValue(":scheme_id", schemeId);
        query.exec();
    }
    
    if (query.next()) {
        return query.value(0).toInt();
    }
    
    return 0;
}

bool ResultDatabase::deleteResults(const QString& schemeId, bool requireConfirmation) {
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        return false;
    }
    
    int countToDelete = countResults(schemeId);
    
    if (countToDelete == 0) {
        return true;
    }
    
    if (requireConfirmation && m_safeDeleteEnabled) {
        if (schemeId.isEmpty() && countToDelete > 10) {
            Logger::warn("Attempted to delete all results without confirmation. Operation blocked.");
            return false;
        }
    }
    
    QSqlQuery query(m_db);
    bool success;
    
    if (schemeId.isEmpty()) {
        success = query.exec("DELETE FROM results");
    } else {
        query.prepare("DELETE FROM results WHERE scheme_id = :scheme_id");
        query.bindValue(":scheme_id", schemeId);
        success = query.exec();
    }
    
    if (success) {
        Logger::info(QString("Deleted %1 results").arg(countToDelete));
        emit resultsDeleted(countToDelete);
    } else {
        Logger::error("Failed to delete results: " + query.lastError().text());
    }
    
    return success;
}

int ResultDatabase::getResultCount(const QString& schemeId) {
    return countResults(schemeId);
}