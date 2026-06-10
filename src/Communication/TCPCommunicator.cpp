#include "TCPCommunicator.h"
#include <QJsonDocument>

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
    if (isConnected()) {
        disconnect();
    }
    
    m_socket->connectToHost(host, port);
    return m_socket->waitForConnected(3000);
}

void TCPCommunicator::disconnect() {
    if (m_socket->state() == QTcpSocket::ConnectedState) {
        m_socket->disconnectFromHost();
    }
}

bool TCPCommunicator::sendData(const QByteArray& data) {
    if (!isConnected()) {
        return false;
    }
    
    qint64 bytesWritten = m_socket->write(data);
    return bytesWritten == data.size();
}

bool TCPCommunicator::sendJson(const QJsonObject& obj) {
    QJsonDocument doc(obj);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    return sendData(data);
}

void TCPCommunicator::onConnected() {
    emit connected();
}

void TCPCommunicator::onDisconnected() {
    emit disconnected();
}

void TCPCommunicator::onReadyRead() {
    qint64 available = m_socket->bytesAvailable();
    if (m_readBuffer.size() + available > m_maxBufferSize) {
        emit bufferOverflow(m_readBuffer.size() + available, m_maxBufferSize);
        emit errorOccurred(QString("接收缓冲区溢出: %1 > %2 字节")
            .arg(m_readBuffer.size() + available)
            .arg(m_maxBufferSize));
        m_readBuffer.clear();
        m_socket->disconnectFromHost();
        return;
    }
    
    QByteArray data = m_socket->readAll();
    m_readBuffer.append(data);
    emit dataReceived(data);
}

void TCPCommunicator::onError(QAbstractSocket::SocketError error) {
    emit errorOccurred(m_socket->errorString());
}