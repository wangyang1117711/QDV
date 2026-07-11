#include "TCPCommunicator.h"
#include "Core/Logger.h"
#include <QJsonDocument>
#include <QHostAddress>

using namespace QDV;

TCPCommunicator::TCPCommunicator(QObject* parent) : QObject(parent) {
    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::connected, this, &TCPCommunicator::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &TCPCommunicator::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &TCPCommunicator::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, &TCPCommunicator::onError);
}

TCPCommunicator::~TCPCommunicator() {
    disconnect();
}

bool TCPCommunicator::connectToHost(const QString& host, int port) {
    // P1-C2 修复（PreReleaseReviewReport Minor 11）：错误处理改进
    // 之前 connectToHost 静默返回 false，调用方无法区分"网络不通"还是"参数错误"
    if (host.isEmpty()) {
        const QString msg = "TCPCommunicator::connectToHost: host is empty";
        Logger::error(msg);
        emit errorOccurred(msg);
        return false;
    }
    if (port <= 0 || port > 65535) {
        const QString msg = QString("TCPCommunicator::connectToHost: invalid port %1").arg(port);
        Logger::error(msg);
        emit errorOccurred(msg);
        return false;
    }

    if (isConnected()) {
        Logger::info("TCPCommunicator: already connected, disconnect first");
        disconnect();
    }

    Logger::info(QString("TCPCommunicator: connecting to %1:%2...").arg(host).arg(port));
    m_socket->connectToHost(host, port);
    const bool ok = m_socket->waitForConnected(3000);
    if (!ok) {
        // P1-C2 修复：记录详细错误上下文（host/port/socket error string/错误码）
        const QString err = QString("TCP connect to %1:%2 failed: %3 (socket error=%4)")
            .arg(host).arg(port)
            .arg(m_socket->errorString())
            .arg(static_cast<int>(m_socket->error()));
        Logger::error(err);
        emit errorOccurred(err);
    } else {
        Logger::info(QString("TCPCommunicator: connected to %1:%2").arg(host).arg(port));
    }
    return ok;
}

void TCPCommunicator::disconnect() {
    if (m_socket->state() == QTcpSocket::ConnectedState) {
        Logger::info("TCPCommunicator: disconnecting");
        m_socket->disconnectFromHost();
    }
}

bool TCPCommunicator::sendData(const QByteArray& data) {
    // P1-C2 修复：静默 return false → 详细错误日志
    if (!isConnected()) {
        Logger::warn("TCPCommunicator::sendData: not connected, drop " +
                     QString::number(data.size()) + " bytes");
        return false;
    }

    const qint64 bytesWritten = m_socket->write(data);
    if (bytesWritten != data.size()) {
        const QString err = QString("TCPCommunicator::sendData: partial write %1/%2 bytes (error=%3)")
            .arg(bytesWritten).arg(data.size()).arg(m_socket->errorString());
        Logger::error(err);
        emit errorOccurred(err);
        return false;
    }
    // 立即触发底层 flush，减少发送延迟（不阻塞等待 ack）
    m_socket->flush();
    return true;
}

bool TCPCommunicator::sendJson(const QJsonObject& obj) {
    QJsonDocument doc(obj);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    return sendData(data);
}

void TCPCommunicator::onConnected() {
    Logger::info(QString("TCPCommunicator: connected to %1:%2")
        .arg(m_socket->peerAddress().toString()).arg(m_socket->peerPort()));
    emit connected();
}

void TCPCommunicator::onDisconnected() {
    Logger::info("TCPCommunicator: disconnected");
    emit disconnected();
}

void TCPCommunicator::onReadyRead() {
    qint64 available = m_socket->bytesAvailable();
    if (m_readBuffer.size() + available > m_maxBufferSize) {
        // P1-C2 修复：缓冲区溢出错误日志补全上下文
        const QString err = QString("TCPCommunicator: 缓冲区溢出 %1 > %2 字节（peer=%3:%4）")
            .arg(m_readBuffer.size() + available)
            .arg(m_maxBufferSize)
            .arg(m_socket->peerAddress().toString())
            .arg(m_socket->peerPort());
        Logger::error(err);
        emit bufferOverflow(m_readBuffer.size() + available, m_maxBufferSize);
        emit errorOccurred(err);
        m_readBuffer.clear();
        m_socket->disconnectFromHost();
        return;
    }

    QByteArray data = m_socket->readAll();
    m_readBuffer.append(data);
    emit dataReceived(data);
}

void TCPCommunicator::onError(QAbstractSocket::SocketError error) {
    // P1-C2 修复：错误处理补全 — 之前仅 emit errorString，未记录日志
    const QString err = QString("TCPCommunicator: socket error %1 (%2:%3): %4")
        .arg(static_cast<int>(error))
        .arg(m_socket->peerAddress().toString())
        .arg(m_socket->peerPort())
        .arg(m_socket->errorString());
    Logger::error(err);
    emit errorOccurred(m_socket->errorString());
}