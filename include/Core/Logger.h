#pragma once

#include <QObject>
#include <QString>
#include <QMutex>
#include <QMutexLocker>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>
#include <QDir>
#include <QTimer>
#include <QStringList>
#include <QMetaType>

namespace QDV {

class Logger : public QObject {
    Q_OBJECT

public:
    enum LogLevel {
        Trace,
        Debug,
        Info,
        Warn,
        Error,
        Critical
    };

    /// P0-1（0906 优化）：最低输出级别（运行时可用环境变量 QDV_LOG_LEVEL 调整）。
    /// Debug/Trace 级热路径诊断日志在过滤后直接丢弃，连字符串参数都不求值成本
    /// 由调用方 .arg() 产生——因此热路径仍应传未格式化前的惰性写法或接受轻量拼接。
    static void setMinLevel(LogLevel level) { s_minLevel = level; }
    static LogLevel minLevel() { return s_minLevel; }
    static bool shouldLog(LogLevel level) { return level >= s_minLevel; }

    // 错误记录结构，用于 Logger 与监控模块之间的契约
    struct ErrorRecord {
        QString timestamp;
        LogLevel level;
        QString message;
    };

    static Logger* instance() {
        QMutexLocker locker(&s_instanceMutex);
        if (!s_instance) {
            s_instance = new Logger();
        }
        return s_instance;
    }

    // 日志记录核心方法，实现在 Logger.cpp 中
    void log(LogLevel level, const QString& message);

    static void trace(const QString& message)   { if (shouldLog(Trace)) instance()->log(Trace, message); }
    static void debug(const QString& message)   { if (shouldLog(Debug)) instance()->log(Debug, message); }
    static void info(const QString& message)    { if (shouldLog(Info)) instance()->log(Info, message); }
    static void warn(const QString& message)    { if (shouldLog(Warn)) instance()->log(Warn, message); }
    static void error(const QString& message)   { if (shouldLog(Error)) instance()->log(Error, message); }
    static void critical(const QString& message){ if (shouldLog(Critical)) instance()->log(Critical, message); }

    static void shutdown() {
        if (s_instance) {
            s_instance->flushBuffer();
            s_instance->closeFile();
        }
    }

signals:
    // 当记录 Error 或更高级别日志时发射，供监控模块监听
    void errorOccurred(const QDV::Logger::ErrorRecord& record);

private:
    Logger() {
        QDir().mkdir("logs");

        m_flushTimer = new QTimer(this);
        m_flushTimer->setInterval(100);
        connect(m_flushTimer, &QTimer::timeout, this, &Logger::flushBuffer);
        m_flushTimer->start();

        openLogFile();
        cleanupOldLogs();
    }

    ~Logger() {
        m_flushTimer->stop();
        flushBuffer();
        closeFile();
    }

    void openLogFile() {
        QMutexLocker locker(&m_fileMutex);
        closeFile();

        m_currentDate = QDateTime::currentDateTime().toString("yyyyMMdd");
        m_logFile.setFileName("logs/" + m_currentDate + ".log");

        if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            m_fileOpened = true;
        }
    }

    void closeFile() {
        if (m_fileOpened) {
            m_logFile.close();
            m_fileOpened = false;
        }
    }

    void flushBuffer() {
        QStringList toFlush;
        {
            QMutexLocker locker(&m_bufferMutex);
            if (m_buffer.isEmpty()) {
                return;
            }
            toFlush = m_buffer;
            m_buffer.clear();
        }

        QMutexLocker fileLocker(&m_fileMutex);

        QString today = QDateTime::currentDateTime().toString("yyyyMMdd");
        if (today != m_currentDate) {
            closeFile();
            m_currentDate = today;
            m_logFile.setFileName("logs/" + m_currentDate + ".log");
            if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                m_fileOpened = true;
            }
        }

        if (!m_fileOpened) {
            if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                return;
            }
            m_fileOpened = true;
        }

        QTextStream out(&m_logFile);
        for (const QString& line : toFlush) {
            out << line << "\n";
        }
        out.flush();

        rotateIfNeeded();
    }

    void rotateIfNeeded() {
        if (m_logFile.size() < MAX_FILE_SIZE) return;

        closeFile();
        QString datePrefix = m_currentDate;
        int suffix = 1;
        QString rotatedPath;
        do {
            rotatedPath = QString("logs/%1_%2.log").arg(datePrefix).arg(suffix);
            ++suffix;
        } while (QFileInfo::exists(rotatedPath));

        QFile::rename("logs/" + datePrefix + ".log", rotatedPath);

        m_currentDate = QDateTime::currentDateTime().toString("yyyyMMdd");
        m_logFile.setFileName("logs/" + m_currentDate + ".log");
        if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            m_fileOpened = true;
        }
    }

    void cleanupOldLogs() {
        QDir dir("logs");
        QStringList logFiles = dir.entryList({"*.log"}, QDir::Files);
        QDateTime cutoff = QDateTime::currentDateTime().addDays(-7);

        for (const QString& file : logFiles) {
            QFileInfo info(dir.absoluteFilePath(file));
            if (info.lastModified() < cutoff) {
                QFile::remove(info.absoluteFilePath());
            }
        }
    }

    static constexpr int BUFFER_FLUSH_SIZE = 256;
    static constexpr qint64 MAX_FILE_SIZE = 50 * 1024 * 1024;

    QStringList m_buffer;
    QMutex m_bufferMutex;
    QMutex m_fileMutex;
    QFile m_logFile;
    bool m_fileOpened = false;
    QString m_currentDate;
    QTimer* m_flushTimer = nullptr;

    static Logger* s_instance;
    static QMutex s_instanceMutex;
    /// P0-1（0906 优化）：全局最低日志级别，默认 Info（Trace/Debug 被过滤）
    static LogLevel s_minLevel;
};

} // namespace QDV

Q_DECLARE_METATYPE(QDV::Logger::ErrorRecord)
