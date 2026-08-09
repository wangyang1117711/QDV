#include "ResultDatabase.h"
#include "Core/Logger.h"
#include <QSqlError>
#include <QCryptographicHash>
#include <QSysInfo>
#include <QRandomGenerator>

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

    // 崩溃根因修复：重复 open() 会重复 addDatabase 同名连接，导致 QSqlDatabase 内部
    // 引用状态混乱。多次开/关后 m_db 可能指向已失效的连接对象，QSqlQuery(m_db) 访问
    // 已释放内存触发 0xC0000005。此处先彻底清理旧连接再创建新连接。
    if (m_db.isValid() && m_db.isOpen()) {
        m_db.close();
    }
    const QString oldConnName = m_db.connectionName();
    if (!oldConnName.isEmpty()) {
        m_db = QSqlDatabase();  // 释放旧引用，必须在 removeDatabase 之前
        QSqlDatabase::removeDatabase(oldConnName);
    }

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
    // 崩溃根因修复：必须 removeDatabase 彻底清理连接，否则连接泄漏累积。
    // 旧版只 close() 不 removeDatabase()，再次 open() 时 addDatabase 同名连接
    // 会触发 Qt 警告 "connection already exists" 并返回旧连接，多次累积后
    // m_db 指向的内部句柄可能失效，导致 0xC0000005。
    const QString connName = m_db.connectionName();
    if (!connName.isEmpty()) {
        m_db = QSqlDatabase();  // 释放引用，必须在 removeDatabase 之前
        QSqlDatabase::removeDatabase(connName);
    }
}

bool ResultDatabase::createTables() {
    QMutexLocker locker(&m_mutex);  // 递归锁允许 open()/createTables() 嵌套加锁

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
    // S5 安全加固：敏感字段加密后存储（方案名、图像路径）
    query.bindValue(":scheme_name", encryptField(schemeName));
    query.bindValue(":ok", ok ? 1 : 0);
    query.bindValue(":score", score);
    query.bindValue(":image_path", encryptField(imagePath));
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
        // S5 安全加固：敏感字段加密后存储（方案名、图像路径）
        query.bindValue(":scheme_name", encryptField(item.schemeName));
        query.bindValue(":ok", item.ok ? 1 : 0);
        query.bindValue(":score", item.score);
        query.bindValue(":image_path", encryptField(item.imagePath));
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
    
    queryStr += " ORDER BY timestamp DESC LIMIT 10000";

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
            // S5 安全加固：读取时解密敏感字段
            result["scheme_name"] = decryptField(query.value("scheme_name").toString());
            result["ok"] = query.value("ok").toInt() == 1;
            result["score"] = query.value("score").toDouble();
            result["image_path"] = decryptField(query.value("image_path").toString());
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

// ===== S5 安全加固：应用层加密实现 =====
// 密钥来源 = 应用固定盐 + 机器特征码（machineUniqueId），避免硬编码密钥。
// 不同机器派生出不同密钥，数据库文件被复制到其他机器后无法解密。
QByteArray ResultDatabase::deriveEncryptionKey() const {
    const QByteArray kAppSalt = QByteArray("QDV_ResultDB_S5_v1");
    QByteArray machineId = QSysInfo::machineUniqueId();
    QByteArray material = kAppSalt + machineId;
    return QCryptographicHash::hash(material, QCryptographicHash::Sha256);
}

// S5 加密：XOR + 随机 IV 流密码（与 AuthService 加密方案一致）
// 返回格式 "iv_hex:ciphertext_hex"，仅含十六进制与冒号，可安全存入 SQLite TEXT 字段。
// 空字符串不加密直接返回，避免无谓开销并保持语义。
QString ResultDatabase::encryptField(const QString& plaintext) const {
    if (plaintext.isEmpty()) {
        return QString();
    }

    QByteArray key = deriveEncryptionKey();
    QByteArray iv(16, '\0');
    for (int i = 0; i < 16; ++i) {
        iv[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }

    QByteArray plainBytes = plaintext.toUtf8();
    QByteArray cipher;
    cipher.resize(plainBytes.size());
    for (int i = 0; i < plainBytes.size(); ++i) {
        cipher[i] = plainBytes[i] ^ key[i % key.size()] ^ iv[i % iv.size()];
    }

    return QString::fromLatin1(iv.toHex() + ":" + cipher.toHex());
}

// S5 解密：与 encryptField 互逆。
// 向后兼容：若传入值不匹配 "hex:hex" 格式（如旧版明文数据），原样返回。
QString ResultDatabase::decryptField(const QString& ciphertext) const {
    if (ciphertext.isEmpty()) {
        return QString();
    }

    const QStringList parts = ciphertext.split(':');
    if (parts.size() != 2) {
        // 非加密格式（旧版明文数据），直接返回原值以保持向后兼容
        return ciphertext;
    }

    QByteArray iv = QByteArray::fromHex(parts[0].toLatin1());
    QByteArray data = QByteArray::fromHex(parts[1].toLatin1());
    if (iv.size() == 0 || data.size() == 0) {
        return ciphertext;
    }

    QByteArray key = deriveEncryptionKey();
    QByteArray plain;
    plain.resize(data.size());
    for (int i = 0; i < data.size(); ++i) {
        plain[i] = data[i] ^ key[i % key.size()] ^ iv[i % iv.size()];
    }

    return QString::fromUtf8(plain);
}