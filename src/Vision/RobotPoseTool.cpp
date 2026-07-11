#include "RobotPoseTool.h"
#include "Core/Logger.h"
#include <QTcpSocket>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <opencv2/imgproc.hpp>

using namespace QDV;

// Windows 串口 API
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// =====================================================
// 全局 PoseBuffer 静态成员初始化
// =====================================================
QMutex RobotPoseTool::s_poseMutex;
QQueue<RobotPoseData> RobotPoseTool::s_poseBuffer;
qint64 RobotPoseTool::s_nextFrameId = 1;

// -----------------------------------------------------
// 全局 PoseBuffer 操作
// -----------------------------------------------------

void RobotPoseTool::pushPose(const RobotPoseData& pose) {
    QMutexLocker locker(&s_poseMutex);
    // 缓冲区满时丢弃最旧数据
    while (s_poseBuffer.size() >= POSE_BUFFER_MAX) {
        s_poseBuffer.dequeue();
        Logger::warn("RobotPoseTool: PoseBuffer 满，丢弃最旧位姿");
    }
    s_poseBuffer.enqueue(pose);
}

RobotPoseData RobotPoseTool::popOldestPose() {
    QMutexLocker locker(&s_poseMutex);
    if (s_poseBuffer.isEmpty()) {
        RobotPoseData invalid;
        invalid.valid = false;
        return invalid;
    }
    return s_poseBuffer.dequeue();
}

RobotPoseData RobotPoseTool::takePose(qint64 frameId) {
    QMutexLocker locker(&s_poseMutex);
    // 在队列中查找指定 frameId
    for (int i = 0; i < s_poseBuffer.size(); ++i) {
        if (s_poseBuffer[i].frameId == frameId) {
            RobotPoseData found = s_poseBuffer[i];
            s_poseBuffer.removeAt(i);
            return found;
        }
    }
    RobotPoseData invalid;
    invalid.valid = false;
    return invalid;
}

int RobotPoseTool::poseBufferSize() {
    QMutexLocker locker(&s_poseMutex);
    return s_poseBuffer.size();
}

void RobotPoseTool::clearPoseBuffer() {
    QMutexLocker locker(&s_poseMutex);
    s_poseBuffer.clear();
}

void RobotPoseTool::purgeExpiredPoses(qint64 timeoutMs) {
    QMutexLocker locker(&s_poseMutex);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    while (!s_poseBuffer.isEmpty()) {
        const RobotPoseData& front = s_poseBuffer.head();
        if ((now - front.timestampMs) > timeoutMs) {
            s_poseBuffer.dequeue();
            Logger::warn(QString("RobotPoseTool: 丢弃超时位姿 frameId=%1").arg(front.frameId));
        } else {
            break;  // 队列有序，头部未超时则后续也不会超时
        }
    }
}

qint64 RobotPoseTool::nextFrameId() {
    QMutexLocker locker(&s_poseMutex);
    return s_nextFrameId++;
}

// -----------------------------------------------------
// 构造 / 析构
// -----------------------------------------------------

RobotPoseTool::RobotPoseTool() {
    m_name = "机器人位姿";
}

RobotPoseTool::~RobotPoseTool() {
    // 清理 TCP / 串口资源
    if (m_tcpSocket) {
        delete static_cast<QTcpSocket*>(m_tcpSocket);
    }
#ifdef Q_OS_WIN
    if (m_serial && m_serial != INVALID_HANDLE_VALUE) {
        CloseHandle(m_serial);
    }
#endif
}

// -----------------------------------------------------
// 参数配置
// -----------------------------------------------------

bool RobotPoseTool::configure(const QJsonObject& params) {
    if (params.contains("sourceType")) {
        const QString v = params["sourceType"].toString().toLower();
        if (v == "tcp" || v == "serial" || v == "file") {
            m_sourceType = v;
        } else {
            Logger::warn(QString("RobotPoseTool: sourceType '%1' 非法，回退为 'file'").arg(v));
            m_sourceType = "file";
        }
    }
    if (params.contains("host")) m_host = params["host"].toString();
    if (params.contains("port")) m_port = params["port"].toInt();
    if (params.contains("serialPort")) m_serialPort = params["serialPort"].toString();
    if (params.contains("baudRate")) m_baudRate = params["baudRate"].toInt();
    if (params.contains("filePath")) m_filePath = params["filePath"].toString();
    return true;
}

// -----------------------------------------------------
// 执行：读取一位姿 → 写入 PoseBuffer → 输出可视化
// -----------------------------------------------------

bool RobotPoseTool::execute(const cv::Mat& input, ToolResult& result) {
    RobotPoseData pose;
    pose.valid = false;

    // 按数据源读取位姿
    bool readOk = false;
    if (m_sourceType == "tcp") {
        readOk = readPoseFromTcp(pose);
    } else if (m_sourceType == "serial") {
        readOk = readPoseFromSerial(pose);
    } else {
        readOk = readPoseFromFile(pose);
    }

    if (!readOk || !pose.valid) {
        Logger::warn("RobotPoseTool: 读取位姿失败");
        result.ok = false;
        result.data["error"] = "read pose failed";
        // 输出黑图提示
        cv::Mat black(240, 480, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::putText(black, "RobotPose: read failed", cv::Point(10, 120),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 255), 2);
        result.overlayImage = black;
        return false;
    }

    // 分配帧号（若数据源未提供）
    if (pose.frameId == 0) {
        pose.frameId = nextFrameId();
    }
    if (pose.timestampMs == 0) {
        pose.timestampMs = QDateTime::currentMSecsSinceEpoch();
    }

    // 推入全局 PoseBuffer
    pushPose(pose);

    // 输出到 ToolResult.data
    result.ok = true;
    result.data["frameId"] = static_cast<qint64>(pose.frameId);
    result.data["x"] = pose.x;
    result.data["y"] = pose.y;
    result.data["z"] = pose.z;
    result.data["rx"] = pose.rx;
    result.data["ry"] = pose.ry;
    result.data["rz"] = pose.rz;
    result.data["timestampMs"] = pose.timestampMs;
    result.data["bufferSize"] = poseBufferSize();

    // 可视化 overlay（input 为空时用黑图）
    cv::Mat overlay;
    if (!input.empty()) {
        overlay = input.clone();
    } else {
        overlay = cv::Mat(240, 480, CV_8UC3, cv::Scalar(20, 20, 30));
    }
    const QString txt = QString("Pose #%1: (%.2f, %.2f, %.2f) R(%.3f, %.3f, %.3f)")
        .arg(pose.frameId).arg(pose.x, 0, 'f', 2).arg(pose.y, 0, 'f', 2).arg(pose.z, 0, 'f', 2)
        .arg(pose.rx, 0, 'f', 3).arg(pose.ry, 0, 'f', 3).arg(pose.rz, 0, 'f', 3);
    cv::putText(overlay, txt.toStdString(), cv::Point(10, 25),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
    result.overlayImage = overlay;

    Logger::info(QString("RobotPoseTool: 读取位姿 frameId=%1 bufferSize=%2")
                     .arg(pose.frameId).arg(poseBufferSize()));
    return true;
}

// -----------------------------------------------------
// TCP 读取位姿
// -----------------------------------------------------

bool RobotPoseTool::readPoseFromTcp(RobotPoseData& out) {
    QTcpSocket* socket = static_cast<QTcpSocket*>(m_tcpSocket);

    // 延迟连接
    if (!socket) {
        socket = new QTcpSocket();
        m_tcpSocket = socket;
        socket->connectToHost(m_host, m_port);
        if (!socket->waitForConnected(2000)) {
            Logger::warn(QString("RobotPoseTool: TCP 连接失败 %1:%2 - %3")
                             .arg(m_host).arg(m_port).arg(socket->errorString()));
            return false;
        }
        Logger::info(QString("RobotPoseTool: TCP 已连接 %1:%2").arg(m_host).arg(m_port));
    }

    // 连接断开时重连
    if (socket->state() != QAbstractSocket::ConnectedState) {
        socket->connectToHost(m_host, m_port);
        if (!socket->waitForConnected(2000)) {
            return false;
        }
    }

    // 发送查询命令（通用 ASCII 协议，部分控制器支持 "getpose\n"）
    socket->write("getpose\n");
    socket->waitForBytesWritten(1000);

    // 等待响应（一行 ASCII）
    if (!socket->waitForReadyRead(2000)) {
        Logger::warn("RobotPoseTool: TCP 读取超时");
        return false;
    }

    const QByteArray line = socket->readLine().trimmed();
    return parsePoseLine(QString::fromLatin1(line), out);
}

// -----------------------------------------------------
// 串口读取位姿（Windows API）
// -----------------------------------------------------

bool RobotPoseTool::readPoseFromSerial(RobotPoseData& out) {
#ifdef Q_OS_WIN
    // 延迟打开串口
    if (!m_serial || m_serial == INVALID_HANDLE_VALUE) {
        const std::wstring portName = m_serialPort.toStdWString();
        m_serial = CreateFileW(portName.c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (m_serial == INVALID_HANDLE_VALUE) {
            Logger::warn(QString("RobotPoseTool: 串口打开失败 %1").arg(m_serialPort));
            return false;
        }
        // 配置波特率
        DCB dcb = { sizeof(DCB) };
        if (GetCommState(m_serial, &dcb)) {
            dcb.BaudRate = m_baudRate;
            dcb.ByteSize = 8;
            dcb.Parity = NOPARITY;
            dcb.StopBits = ONESTOPBIT;
            SetCommState(m_serial, &dcb);
        }
        // 设置超时
        COMMTIMEOUTS timeouts = { 0 };
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 2000;
        timeouts.ReadTotalTimeoutMultiplier = 10;
        SetCommTimeouts(m_serial, &timeouts);
        Logger::info(QString("RobotPoseTool: 串口已打开 %1 @ %2").arg(m_serialPort).arg(m_baudRate));
    }

    // 读取一行
    char buf[256] = { 0 };
    DWORD bytesRead = 0;
    // 简化：读取到换行符或缓冲区满
    QString accumulated;
    for (int attempt = 0; attempt < 10; ++attempt) {
        DWORD oneByte = 0;
        if (!ReadFile(m_serial, buf, 1, &oneByte, nullptr) || oneByte == 0) {
            break;
        }
        if (buf[0] == '\n') break;
        accumulated.append(buf[0]);
    }
    accumulated = accumulated.trimmed();
    if (accumulated.isEmpty()) {
        Logger::warn("RobotPoseTool: 串口读取为空");
        return false;
    }
    return parsePoseLine(accumulated, out);
#else
    Logger::warn("RobotPoseTool: 串口模式仅支持 Windows");
    return false;
#endif
}

// -----------------------------------------------------
// 文件轮询读取位姿
// -----------------------------------------------------

bool RobotPoseTool::readPoseFromFile(RobotPoseData& out) {
    if (m_filePath.isEmpty()) {
        Logger::warn("RobotPoseTool: 文件路径为空");
        return false;
    }

    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Logger::warn(QString("RobotPoseTool: 无法打开位姿文件 %1").arg(m_filePath));
        return false;
    }

    // 跳到上次读取位置
    if (m_lastFilePos > 0 && m_lastFilePos < f.size()) {
        f.seek(m_lastFilePos);
    } else if (m_lastFilePos >= f.size()) {
        // 文件被截断或未更新
        Logger::info("RobotPoseTool: 位姿文件无新增数据");
        return false;
    }

    // 读取最后一行（最新位姿）
    QString lastLine;
    while (!f.atEnd()) {
        const QString line = f.readLine().trimmed();
        if (!line.isEmpty() && !line.startsWith('#')) {
            lastLine = line;
        }
    }
    m_lastFilePos = f.pos();

    if (lastLine.isEmpty()) {
        return false;
    }
    return parsePoseLine(lastLine, out);
}

// -----------------------------------------------------
// 解析位姿行
// 格式：x y z rx ry rz [frameId]
// 分隔符：空格/逗号/分号/Tab
// -----------------------------------------------------

bool RobotPoseTool::parsePoseLine(const QString& line, RobotPoseData& out) {
    const QStringList parts = line.split(QRegularExpression("[\\s,;\\t]+"), Qt::SkipEmptyParts);
    if (parts.size() < 6) {
        Logger::warn(QString("RobotPoseTool: 位姿行字段不足 6 个: '%1'").arg(line));
        return false;
    }

    bool ok = false;
    out.x = parts[0].toDouble(&ok);
    if (!ok) return false;
    out.y = parts[1].toDouble(&ok);
    if (!ok) return false;
    out.z = parts[2].toDouble(&ok);
    if (!ok) return false;
    out.rx = parts[3].toDouble(&ok);
    if (!ok) return false;
    out.ry = parts[4].toDouble(&ok);
    if (!ok) return false;
    out.rz = parts[5].toDouble(&ok);
    if (!ok) return false;

    // 可选第 7 列 frameId
    if (parts.size() >= 7) {
        out.frameId = parts[6].toLongLong(&ok);
        if (!ok) out.frameId = 0;
    }

    out.timestampMs = QDateTime::currentMSecsSinceEpoch();
    out.valid = true;
    return true;
}

// -----------------------------------------------------
// 序列化 / 反序列化
// -----------------------------------------------------

QJsonObject RobotPoseTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["sourceType"] = m_sourceType;
    obj["host"] = m_host;
    obj["port"] = m_port;
    obj["serialPort"] = m_serialPort;
    obj["baudRate"] = m_baudRate;
    obj["filePath"] = m_filePath;
    return obj;
}

bool RobotPoseTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("sourceType")) {
        const QString v = data["sourceType"].toString().toLower();
        if (v == "tcp" || v == "serial" || v == "file") m_sourceType = v;
    }
    if (data.contains("host")) m_host = data["host"].toString();
    if (data.contains("port")) m_port = data["port"].toInt();
    if (data.contains("serialPort")) m_serialPort = data["serialPort"].toString();
    if (data.contains("baudRate")) m_baudRate = data["baudRate"].toInt();
    if (data.contains("filePath")) m_filePath = data["filePath"].toString();
    return true;
}
