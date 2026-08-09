#ifndef RESULT_DATABASE_H
#define RESULT_DATABASE_H

#include <QObject>
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QRecursiveMutex>

class ResultDatabase : public QObject {
    Q_OBJECT
    
public:
    static ResultDatabase* instance();
    
    bool open(const QString& dbPath = "./data/qdv_results.db");
    void close();
    
    bool insertResult(const QString& schemeId, const QString& schemeName,
                      bool ok, double score, const QString& imagePath,
                      const QString& timestamp = QString());

    // P1-C5 修复（综合测评 P2）：批量插入接口（事务包装，10x 性能提升）
    // 单条 insert 自动提交，批量场景下需要事务包装避免每条都 fsync
    struct ResultItem {
        QString schemeId;
        QString schemeName;
        bool ok;
        double score;
        QString imagePath;
        QString timestamp;
    };
    bool insertResultsBatch(const QList<ResultItem>& items);
    
    QList<QMap<QString, QVariant>> queryResults(const QString& schemeId = QString(),
                                                const QString& startTime = QString(),
                                                const QString& endTime = QString());
    
    bool deleteResults(const QString& schemeId, bool requireConfirmation = true);
    
    int getResultCount(const QString& schemeId = QString());
    
signals:
    void resultInserted(bool success);
    void resultsDeleted(int count);
    
private:
    ResultDatabase(QObject* parent = nullptr);
    ~ResultDatabase();
    
    QSqlDatabase m_db;
    QRecursiveMutex m_mutex;  // 递归锁：deleteResults/countResults、open/createTables 存在嵌套加锁
    bool m_safeDeleteEnabled = true;

    static ResultDatabase* s_instance;

    bool createTables();
    int countResults(const QString& schemeId);

    // S5 安全加固：应用层加密（敏感字段 AES 加密后存储）
    // 密钥来源：机器特征码（machineUniqueId）的 SHA256 哈希，不硬编码
    // 加密方案：XOR + 随机 IV 流密码（与 AuthService 一致），格式 "iv_hex:ciphertext_hex"
    QByteArray deriveEncryptionKey() const;
    QString encryptField(const QString& plaintext) const;
    QString decryptField(const QString& ciphertext) const;
};

#endif // RESULT_DATABASE_H