#pragma once

#include <QObject>
#include <QString>
#include <QMutex>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>

namespace QDV {

class Logger : public QObject
{
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
        if (!s_instance) {
            QMutexLocker locker(&s_mutex);
            if (!s_instance) {
                s_instance = new Logger();
            }
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
            QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"),
            levelStr,
            message
        );

        // 输出到控制台
        qDebug() << logMsg;

        // 写入文件
        writeToFile(logMsg);
    }

    static void trace(const QString& message) { instance()->log(Trace, message); }
    static void debug(const QString& message) { instance()->log(Debug, message); }
    static void info(const QString& message) { instance()->log(Info, message); }
    static void warn(const QString& message) { instance()->log(Warn, message); }
    static void error(const QString& message) { instance()->log(Error, message); }
    static void critical(const QString& message) { instance()->log(Critical, message); }

private:
    Logger() {
        QDir().mkdir("logs");
        m_logFile.setFileName("logs/" + QDateTime::currentDateTime().toString("yyyyMMdd") + ".log");
    }

    void writeToFile(const QString& message) {
        if (m_logFile.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&m_logFile);
            out << message << "\n";
            m_logFile.close();
        }
    }

    static Logger* s_instance;
    static QMutex s_mutex;
    QFile m_logFile;
};

} // namespace QDV