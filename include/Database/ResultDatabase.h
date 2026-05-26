#ifndef RESULT_DATABASE_H
#define RESULT_DATABASE_H

#include <QObject>
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QMutex>

class ResultDatabase : public QObject {
    Q_OBJECT
    
public:
    static ResultDatabase* instance();
    
    bool open(const QString& dbPath = "./data/qdv_results.db");
    void close();
    
    bool insertResult(const QString& schemeId, const QString& schemeName,
                      bool ok, double score, const QString& imagePath,
                      const QString& timestamp = QString());
    
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
    QMutex m_mutex;
    bool m_safeDeleteEnabled = true;
    
    static ResultDatabase* s_instance;
    
    bool createTables();
    int countResults(const QString& schemeId);
};

#endif // RESULT_DATABASE_H