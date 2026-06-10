#ifndef TCPCOMMUNICATOR_H
#define TCPCOMMUNICATOR_H

#include <QObject>
#include <QTcpSocket>
#include <QHostAddress>

class TCPCommunicator : public QObject {
    Q_OBJECT
    
public:
    explicit TCPCommunicator(QObject* parent = nullptr);
    ~TCPCommunicator();
    
    bool connectToHost(const QString& host, int port);
    void disconnect();
    
    bool sendData(const QByteArray& data);
    bool sendJson(const QJsonObject& obj);
    
    bool isConnected() const { return m_socket->state() == QTcpSocket::ConnectedState; }
    void setMaxBufferSize(qint64 maxSize) { m_maxBufferSize = maxSize; }
    qint64 maxBufferSize() const { return m_maxBufferSize; }
    
signals:
    void connected();
    void disconnected();
    void dataReceived(const QByteArray& data);
    void errorOccurred(const QString& error);
    void bufferOverflow(qint64 receivedSize, qint64 maxSize);
    
private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);
    
private:
    QTcpSocket* m_socket;
    qint64 m_maxBufferSize = 10 * 1024 * 1024;
    QByteArray m_readBuffer;
};

#endif // TCPCOMMUNICATOR_H