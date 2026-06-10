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

    static Logger* instance() {
        QMutexLocker locker(&s_instanceMutex);
        if (!s_instance) {
            s_instance = new Logger();
        }
        return s_instance;
    }

    void log(LogLevel level, const QString& message) {
        QString levelStr;
        switch (level) {
            case Trace: levelStr = "[TRACE]"; break;
            case Debug: levelStr = "[DEBUG]"; break;
            case Info: levelStr = "[INFO]"; break;
            case Warn: levelStr = "[WARN]"; break;
            case Error: levelStr = "[ERROR]"; break;
            case Critical: levelStr = "[CRITICAL]"; break;
        }

        QString logMsg = QString("%1 %2 %3").arg(
            QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"),
            levelStr,
            message
        );

        qDebug().noquote() << logMsg;

        QMutexLocker locker(&m_bufferMutex);
        m_buffer.append(logMsg);

        locker.unlock();
        flushBuffer();
    }

    static void trace(const QString& message)   { instance()->log(Trace, message); }
    static void debug(const QString& message)   { instance()->log(Debug, message); }
    static void info(const QString& message)    { instance()->log(Info, message); }
    static void warn(const QString& message)    { instance()->log(Warn, message); }
    static void error(const QString& message)   { instance()->log(Error, message); }
    static void critical(const QString& message){ instance()->log(Critical, message); }

    static void shutdown() {
        if (s_instance) {
            s_instance->flushBuffer();
            s_instance->closeFile();
        }
    }

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
};

} // namespace QDV