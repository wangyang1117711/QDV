#include "ResultDatabase.h"
#include "Core/Logger.h"
#include <QSqlError>

using namespace QDV;

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
        // P1-C15 修复（架构评估 D6）：记录实际 SQL 错误，便于排查
        const QString err = QString("Failed to open database: %1 | SQL error: %2 (driver: %3)")
            .arg(dbPath)
            .arg(m_db.lastError().text())
            .arg(m_db.lastError().driverText());
        Logger::error(err);
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
        // P1-C15 修复：记录实际 SQL 错误
        Logger::error(QString("Failed to create results table: %1").arg(query.lastError().text()));
    }

    // P1-C15 修复：索引创建失败不再静默，记录警告
    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_scheme_id ON results(scheme_id)")) {
        Logger::warn(QString("Failed to create idx_scheme_id: %1").arg(query.lastError().text()));
    }
    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_timestamp ON results(timestamp)")) {
        Logger::warn(QString("Failed to create idx_timestamp: %1").arg(query.lastError().text()));
    }

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

// P1-C5 修复（综合测评 P2）：批量插入事务包装
// 之前单条 insert 自动提交，N 条记录会触发 N 次 fsync，性能极差。
// 改为 BEGIN TRANSACTION + N 次 INSERT + COMMIT，性能提升约 10x。
// 任意一条失败会 ROLLBACK 整批，保证原子性。
bool ResultDatabase::insertResultsBatch(const QList<ResultItem>& items) {
    QMutexLocker locker(&m_mutex);

    if (!m_db.isOpen()) {
        Logger::error("insertResultsBatch: database not open");
        return false;
    }
    if (items.isEmpty()) {
        return true;
    }

    if (!m_db.transaction()) {
        Logger::error("insertResultsBatch: BEGIN TRANSACTION failed: " + m_db.lastError().text());
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO results (scheme_id, scheme_name, ok, score, image_path, timestamp)
        VALUES (:scheme_id, :scheme_name, :ok, :score, :image_path, :timestamp)
    )");

    int inserted = 0;
    for (const auto& item : items) {
        query.bindValue(":scheme_id", item.schemeId);
        query.bindValue(":scheme_name", item.schemeName);
        query.bindValue(":ok", item.ok ? 1 : 0);
        query.bindValue(":score", item.score);
        query.bindValue(":image_path", item.imagePath);
        query.bindValue(":timestamp", item.timestamp.isEmpty()
            ? QDateTime::currentDateTime().toString(Qt::ISODate) : item.timestamp);

        if (!query.exec()) {
            Logger::error(QString("insertResultsBatch: failed at item %1/%2: %3")
                .arg(inserted + 1).arg(items.size()).arg(query.lastError().text()));
            if (!m_db.rollback()) {
                Logger::error("insertResultsBatch: ROLLBACK failed: " + m_db.lastError().text());
            }
            emit resultInserted(false);
            return false;
        }
        ++inserted;
    }

    if (!m_db.commit()) {
        Logger::error("insertResultsBatch: COMMIT failed: " + m_db.lastError().text());
        if (!m_db.rollback()) {
            Logger::error("insertResultsBatch: ROLLBACK after COMMIT failure failed");
        }
        emit resultInserted(false);
        return false;
    }

    Logger::info(QString("insertResultsBatch: %1 items committed").arg(inserted));
    emit resultInserted(true);
    return true;
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