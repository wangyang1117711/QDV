#include "TCPCommunicator.h"
#include "Core/Logger.h"
#include <QJsonDocument>
#include <QHostAddress>
#include <QFile>
#include <QSslConfiguration>
#include <QCryptographicHash>

using namespace QDV;

TCPCommunicator::TCPCommunicator(QObject* parent) : QObject(parent) {
    // 默认明文模式：构造明文 QTcpSocket
    setupPlainSocket();
}

TCPCommunicator::~TCPCommunicator() {
    disconnect();
}

// ---------------------------------------------------------------------------
// 内部 socket 管理
// ---------------------------------------------------------------------------

void TCPCommunicator::destroySocket() {
    if (!m_socket) {
        return;
    }
    // 先断开所有信号→本类槽的连接，避免销毁过程中触发槽
    m_socket->disconnect(this);
    m_socket->abort();
    // m_sslSocket 与 m_socket 可能指向同一对象，统一处理
    if (m_sslSocket) {
        delete m_sslSocket;
        m_sslSocket = nullptr;
        m_socket = nullptr;
    } else {
        delete m_socket;
        m_socket = nullptr;
    }
}

void TCPCommunicator::setupPlainSocket() {
    destroySocket();
    m_socket = new QTcpSocket(this);
    m_sslSocket = nullptr;
    reconnectSocketSignals();
}

void TCPCommunicator::setupSslSocket() {
    destroySocket();
    m_sslSocket = new QSslSocket(this);
    m_socket = m_sslSocket;  // 向上转型，复用统一读写接口

    // 配置 CA 证书（验证服务端）
    // Qt 6 移除了 QSslSocket::addCaCertificate，改用 QSslConfiguration::setCaCertificates
    if (!m_caCertificate.isNull()) {
        QSslConfiguration config = m_sslSocket->sslConfiguration();
        config.setCaCertificates({m_caCertificate});
        m_sslSocket->setSslConfiguration(config);
    }
    // 配置客户端证书与私钥（双向认证）
    if (!m_clientCertificate.isNull() && !m_clientPrivateKey.isNull()) {
        m_sslSocket->setLocalCertificate(m_clientCertificate);
        m_sslSocket->setPrivateKey(m_clientPrivateKey);
    }
    // 默认强制对端证书验证（安全优先）
    m_sslSocket->setPeerVerifyMode(QSslSocket::VerifyPeer);

    // 基类信号
    reconnectSocketSignals();
    // SSL 特定信号
    connect(m_sslSocket, &QSslSocket::sslErrors,
            this, &TCPCommunicator::onSslErrors);
    connect(m_sslSocket, &QSslSocket::encrypted,
            this, &TCPCommunicator::onSslHandshakeCompleted);
}

void TCPCommunicator::reconnectSocketSignals() {
    Q_ASSERT(m_socket);
    connect(m_socket, &QTcpSocket::connected, this, &TCPCommunicator::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &TCPCommunicator::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &TCPCommunicator::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, &TCPCommunicator::onError);
}

// ---------------------------------------------------------------------------
// 连接管理
// ---------------------------------------------------------------------------

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

    // 明文模式：确保使用 QTcpSocket（防止误在 TLS 模式下调用明文连接）
    if (m_useTls || m_sslSocket) {
        Logger::warn("TCPCommunicator::connectToHost: TLS 模式已启用，建议改用 connectToHostSecure；"
                     "为安全起见已切换回明文 socket 执行本次连接");
        setupPlainSocket();
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

bool TCPCommunicator::connectToHostSecure(const QString& host, int port) {
    // 未启用 TLS：拒绝安全连接，提示调用方先 enableTls(true)
    // 这是"安全降级"策略——不静默回退到明文，避免调用方误以为已加密
    if (!m_useTls) {
        const QString err = "TCPCommunicator::connectToHostSecure: TLS 未启用，"
                            "请先调用 enableTls(true) 并加载证书";
        Logger::error(err);
        emit errorOccurred(err);
        return false;
    }

    if (host.isEmpty()) {
        const QString msg = "TCPCommunicator::connectToHostSecure: host is empty";
        Logger::error(msg);
        emit errorOccurred(msg);
        return false;
    }
    if (port <= 0 || port > 65535) {
        const QString msg = QString("TCPCommunicator::connectToHostSecure: invalid port %1").arg(port);
        Logger::error(msg);
        emit errorOccurred(msg);
        return false;
    }

    if (isConnected()) {
        Logger::info("TCPCommunicator: already connected, disconnect first");
        disconnect();
    }

    // 确保 SSL socket 已就绪（enableTls 已创建，但防御性处理）
    if (!m_sslSocket) {
        setupSslSocket();
    }

    Logger::info(QString("TCPCommunicator: secure connecting to %1:%2...").arg(host).arg(port));

    // connectToHostEncrypted 内部先建立 TCP 连接，再启动 SSL 握手
    m_sslSocket->connectToHostEncrypted(host, port);

    // 1) 等待 TCP 连接建立
    const bool tcpOk = m_sslSocket->waitForConnected(3000);
    if (!tcpOk) {
        const QString err = QString("TLS TCP connect to %1:%2 failed: %3 (socket error=%4)")
            .arg(host).arg(port)
            .arg(m_sslSocket->errorString())
            .arg(static_cast<int>(m_sslSocket->error()));
        Logger::error(err);
        emit errorOccurred(err);
        return false;
    }

    // 2) 等待 SSL 握手完成（encrypted 信号会在阻塞期间触发 onSslHandshakeCompleted）
    const bool handshakeOk = m_sslSocket->waitForEncrypted(5000);
    if (!handshakeOk) {
        const QString err = QString("TLS handshake to %1:%2 failed: %3 (socket error=%4)")
            .arg(host).arg(port)
            .arg(m_sslSocket->errorString())
            .arg(static_cast<int>(m_sslSocket->error()));
        Logger::error(err);
        emit errorOccurred(err);
        m_sslSocket->abort();
        return false;
    }

    Logger::info(QString("TCPCommunicator: TLS established to %1:%2").arg(host).arg(port));
    return true;
}

void TCPCommunicator::disconnect() {
    if (m_socket && m_socket->state() == QTcpSocket::ConnectedState) {
        Logger::info("TCPCommunicator: disconnecting");
        m_socket->disconnectFromHost();
    }
}

// ---------------------------------------------------------------------------
// 数据发送
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// TLS 配置
// ---------------------------------------------------------------------------

void TCPCommunicator::enableTls(bool enabled) {
    if (m_useTls == enabled) {
        return;
    }
    m_useTls = enabled;

    if (enabled) {
        setupSslSocket();
        Logger::info("TCPCommunicator: TLS 模式已启用");
    } else {
        setupPlainSocket();
        Logger::info("TCPCommunicator: TLS 模式已禁用，回退到明文");
    }
}

bool TCPCommunicator::loadCertificates(const QString& caCertPath,
                                       const QString& clientCertPath,
                                       const QString& clientKeyPath) {
    bool ok = true;

    // 加载 CA 证书（TLS 启用时必填）
    if (!caCertPath.isEmpty()) {
        QFile caFile(caCertPath);
        if (!caFile.open(QIODevice::ReadOnly)) {
            const QString err = QString("TLS: 无法打开 CA 证书文件: %1").arg(caCertPath);
            Logger::error(err);
            emit errorOccurred(err);
            return false;
        }
        // 优先尝试 PEM，失败再尝试 DER
        m_caCertificate = QSslCertificate(&caFile, QSsl::Pem);
        if (m_caCertificate.isNull()) {
            caFile.seek(0);
            m_caCertificate = QSslCertificate(&caFile, QSsl::Der);
        }
        caFile.close();
        if (m_caCertificate.isNull()) {
            const QString err = QString("TLS: CA 证书解析失败: %1").arg(caCertPath);
            Logger::error(err);
            emit errorOccurred(err);
            ok = false;
        }
    }

    // 加载客户端证书（可选，双向认证）
    if (!clientCertPath.isEmpty()) {
        QFile certFile(clientCertPath);
        if (!certFile.open(QIODevice::ReadOnly)) {
            const QString err = QString("TLS: 无法打开客户端证书文件: %1").arg(clientCertPath);
            Logger::error(err);
            emit errorOccurred(err);
            return false;
        }
        m_clientCertificate = QSslCertificate(&certFile, QSsl::Pem);
        if (m_clientCertificate.isNull()) {
            certFile.seek(0);
            m_clientCertificate = QSslCertificate(&certFile, QSsl::Der);
        }
        certFile.close();
        if (m_clientCertificate.isNull()) {
            const QString err = QString("TLS: 客户端证书解析失败: %1").arg(clientCertPath);
            Logger::error(err);
            emit errorOccurred(err);
            ok = false;
        }
    }

    // 加载客户端私钥（可选）
    if (!clientKeyPath.isEmpty()) {
        QFile keyFile(clientKeyPath);
        if (!keyFile.open(QIODevice::ReadOnly)) {
            const QString err = QString("TLS: 无法打开私钥文件: %1").arg(clientKeyPath);
            Logger::error(err);
            emit errorOccurred(err);
            return false;
        }
        // 依次尝试 RSA/EC × PEM/DER（无密码）
        m_clientPrivateKey = QSslKey(&keyFile, QSsl::Rsa, QSsl::Pem);
        if (m_clientPrivateKey.isNull()) { keyFile.seek(0); m_clientPrivateKey = QSslKey(&keyFile, QSsl::Ec, QSsl::Pem); }
        if (m_clientPrivateKey.isNull()) { keyFile.seek(0); m_clientPrivateKey = QSslKey(&keyFile, QSsl::Rsa, QSsl::Der); }
        if (m_clientPrivateKey.isNull()) { keyFile.seek(0); m_clientPrivateKey = QSslKey(&keyFile, QSsl::Ec, QSsl::Der); }
        keyFile.close();
        if (m_clientPrivateKey.isNull()) {
            const QString err = QString("TLS: 私钥解析失败: %1").arg(clientKeyPath);
            Logger::error(err);
            emit errorOccurred(err);
            ok = false;
        }
    }

    // 若 SSL socket 已存在，实时更新其证书配置
    if (m_sslSocket) {
        if (!m_caCertificate.isNull()) {
            QSslConfiguration config = m_sslSocket->sslConfiguration();
            config.setCaCertificates({m_caCertificate});
            m_sslSocket->setSslConfiguration(config);
        }
        if (!m_clientCertificate.isNull() && !m_clientPrivateKey.isNull()) {
            m_sslSocket->setLocalCertificate(m_clientCertificate);
            m_sslSocket->setPrivateKey(m_clientPrivateKey);
        }
    }

    if (ok) {
        Logger::info("TCPCommunicator: 证书加载完成");
    }
    return ok;
}

// ---------------------------------------------------------------------------
// 槽函数
// ---------------------------------------------------------------------------

void TCPCommunicator::onConnected() {
    // TLS 模式下，TCP 连接建立时尚未完成 SSL 握手，推迟 emit connected()
    if (m_useTls) {
        Logger::info(QString("TCPCommunicator: TCP connected to %1:%2, waiting for TLS handshake...")
            .arg(m_socket->peerAddress().toString()).arg(m_socket->peerPort()));
    } else {
        Logger::info(QString("TCPCommunicator: connected to %1:%2")
            .arg(m_socket->peerAddress().toString()).arg(m_socket->peerPort()));
        emit connected();
    }
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

void TCPCommunicator::onSslErrors(const QList<QSslError>& errors) {
    // 默认不忽略 SSL 错误（安全优先）：仅记录并上报，握手将因此失败
    QStringList errList;
    for (const QSslError& e : errors) {
        errList << e.errorString();
    }
    const QString err = QString("TCPCommunicator: SSL 错误: %1").arg(errList.join("; "));
    Logger::error(err);
    emit errorOccurred(err);
}

void TCPCommunicator::onSslHandshakeCompleted() {
    QString summary;
    if (m_sslSocket) {
        const QSslCertificate peerCert = m_sslSocket->peerCertificate();
        if (!peerCert.isNull()) {
            summary = QString("subject=%1, issuer=%2, effective=%3, expiry=%4")
                .arg(peerCert.subjectDisplayName())
                .arg(peerCert.issuerDisplayName())
                .arg(peerCert.effectiveDate().toString(Qt::ISODate))
                .arg(peerCert.expiryDate().toString(Qt::ISODate));
        }

        // 证书钉扎校验（可选）：指纹不匹配则断开连接
        if (!m_pinnedSha256.isEmpty() && !peerCert.isNull()) {
            const QByteArray digest = peerCert.digest(QCryptographicHash::Sha256).toHex();
            const QString actualSha256 = QString::fromLatin1(digest);
            if (actualSha256.compare(m_pinnedSha256, Qt::CaseInsensitive) != 0) {
                const QString err = QString("TLS 证书钉扎校验失败: expected=%1, actual=%2")
                    .arg(m_pinnedSha256, actualSha256);
                Logger::error(err);
                emit errorOccurred(err);
                m_sslSocket->abort();
                return;
            }
            Logger::info("TCPCommunicator: 证书钉扎校验通过");
        }
    }
    Logger::info(QString("TCPCommunicator: SSL 握手完成 (%1)").arg(summary));
    emit sslHandshakeCompleted(summary);
    // TLS 模式下，connected 信号在握手成功时才发射
    emit connected();
}
