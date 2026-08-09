#ifndef TCPCOMMUNICATOR_H
#define TCPCOMMUNICATOR_H

#include <QObject>
#include <QTcpSocket>
#include <QHostAddress>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslError>
#include <QList>

/**
 * @class TCPCommunicator
 * @brief TCP 通信器，支持明文与 TLS 加密两种模式（向后兼容）
 *
 * 【TLS 模式使用场景】
 * - 生产环境：强烈建议启用 TLS（enableTls(true) + connectToHostSecure），
 *   防止中间人攻击、数据窃听与篡改。部署时需配置受信任的 CA 证书，
 *   双向认证场景还需提供客户端证书与私钥。
 * - 开发环境：可保持默认明文模式（connectToHost），便于本地调试与抓包分析。
 *
 * 【证书管理方式】
 * - CA 证书（caCertificate）：用于校验服务端证书签名，TLS 启用时必填。
 * - 客户端证书（clientCertificate）：双向认证时提供，可选。
 * - 客户端私钥（clientPrivateKey）：与客户端证书配对，可选。
 * - 证书格式：PEM 或 DER，由 QSslCertificate 自动识别。
 * - 证书文件应放置于受保护目录（建议 config/certs/），文件权限设为仅所有者可读。
 *
 * 【安全注意事项】
 * - 默认启用对端证书验证（QSslSocket::VerifyPeer），严禁在生产环境关闭。
 * - 证书钉扎（pinning）：可通过 setPinnedCertificateSha256 设置预期证书指纹，
 *   握手完成后校验，防止 CA 被攻破后的中间人攻击。
 * - 私钥文件严禁提交到版本控制系统，应通过安全渠道分发。
 * - 明文模式（connectToHost）仅为向后兼容保留，新代码应优先使用 TLS。
 * - TLS 是可选功能，默认关闭（m_useTls=false），不影响现有明文行为。
 */
class TCPCommunicator : public QObject {
    Q_OBJECT

public:
    explicit TCPCommunicator(QObject* parent = nullptr);
    ~TCPCommunicator();

    // 明文连接（向后兼容：签名与行为保持不变）
    bool connectToHost(const QString& host, int port);

    // TLS 安全连接：先建立 TCP 连接，再启动 SSL 握手。
    // 调用前需先 enableTls(true) 并加载证书；未启用 TLS 时返回 false。
    bool connectToHostSecure(const QString& host, int port);

    void disconnect();

    bool sendData(const QByteArray& data);
    bool sendJson(const QJsonObject& obj);

    // 启用/禁用 TLS 模式。默认 false（明文）。
    // 启用后内部切换为 QSslSocket；禁用时回退到明文 QTcpSocket。
    void enableTls(bool enabled);
    bool isTlsEnabled() const { return m_useTls; }

    // 加载证书与私钥。
    // - caCertPath:     CA 证书路径（TLS 启用时必填）
    // - clientCertPath: 客户端证书路径（可选，双向认证时提供）
    // - clientKeyPath:  客户端私钥路径（可选，与 clientCertPath 配对）
    // 返回 true 表示全部已提供项加载成功；任一必填项失败返回 false 并通过 errorOccurred 上报。
    bool loadCertificates(const QString& caCertPath,
                          const QString& clientCertPath = {},
                          const QString& clientKeyPath = {});

    // 设置证书钉扎指纹（SHA-256 十六进制字符串，可选）。
    // 设置后，TLS 握手成功后会校验对端证书指纹，不匹配则断开连接。
    void setPinnedCertificateSha256(const QString& sha256) { m_pinnedSha256 = sha256; }
    QString pinnedCertificateSha256() const { return m_pinnedSha256; }

    bool isConnected() const { return m_socket->state() == QTcpSocket::ConnectedState; }
    void setMaxBufferSize(qint64 maxSize) { m_maxBufferSize = maxSize; }
    qint64 maxBufferSize() const { return m_maxBufferSize; }

signals:
    void connected();
    void disconnected();
    void dataReceived(const QByteArray& data);
    void errorOccurred(const QString& error);
    void bufferOverflow(qint64 receivedSize, qint64 maxSize);
    // TLS 握手完成信号，携带对端证书摘要（subject/issuer/有效期）
    void sslHandshakeCompleted(const QString& peerCertificateSummary);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);
    // 处理 SSL 握手/证书错误（默认不忽略，安全优先）
    void onSslErrors(const QList<QSslError>& errors);
    // SSL 握手成功：校验证书钉扎并发射 sslHandshakeCompleted
    void onSslHandshakeCompleted();

private:
    QTcpSocket* m_socket = nullptr;      // 统一读写接口（明文或 TLS 均通过它）
    QSslSocket* m_sslSocket = nullptr;   // TLS 模式下指向 SSL socket（此时 m_socket == m_sslSocket）
    bool m_useTls = false;               // TLS 模式开关，默认关闭
    qint64 m_maxBufferSize = 10 * 1024 * 1024;
    QByteArray m_readBuffer;

    // TLS 证书与配置
    QSslCertificate m_caCertificate;        // CA 证书（验证服务端）
    QSslCertificate m_clientCertificate;    // 客户端证书（双向认证）
    QSslKey m_clientPrivateKey;             // 客户端私钥
    QString m_pinnedSha256;                 // 证书钉扎指纹（SHA-256 十六进制）

    // 销毁当前 socket（断开信号、abort、释放）
    void destroySocket();
    // 初始化明文 QTcpSocket
    void setupPlainSocket();
    // 初始化 QSslSocket 并配置证书/验证模式
    void setupSslSocket();
    // 重新连接基类 socket 信号到本类槽
    void reconnectSocketSignals();
};

#endif // TCPCOMMUNICATOR_H
