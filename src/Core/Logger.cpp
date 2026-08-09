#include "Core/Logger.h"

namespace QDV {

Logger* Logger::s_instance = nullptr;
QMutex Logger::s_instanceMutex;

// 注册 ErrorRecord 元类型，确保跨线程信号传递时参数正常拷贝
// 注意：Q_DECLARE_METATYPE 在 Logger.h 末尾声明，此处调用注册函数
static int _errorRecordMetaType = qRegisterMetaType<QDV::Logger::ErrorRecord>();

// 构造函数/析构函数已在 Logger.h 中内联实现，此处仅实现 log() 方法
void Logger::log(LogLevel level, const QString& message) {
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

    // 当级别达到 Error 及以上时，构造 ErrorRecord 并发射信号，供监控模块捕获
    if (level >= Error) {
        Logger::ErrorRecord record;
        record.timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
        record.level = level;
        record.message = message;
        emit errorOccurred(record);
    }
}

} // namespace QDV
