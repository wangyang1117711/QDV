#ifndef AUTHSERVICE_H
#define AUTHSERVICE_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QDateTime>
#include <QMutex>

class AuthService : public QObject {
    Q_OBJECT
    
public:
    static AuthService* instance();
    
    bool login(const QString& username, const QString& password);
    void logout();
    
    bool isAuthenticated() const { return m_authenticated; }
    QString currentUser() const { return m_currentUser; }
    bool isFirstRun() const { return m_firstRun; }
    int remainingAttempts(const QString& username) const;
    qint64 lockoutSecondsRemaining(const QString& username) const;

    // P1-C4 修复（综合测评 S6）：补全 isAdmin 持久化与查询接口
    // 之前 createUser(isAdmin) 参数被完全忽略，权限模型形同虚设
    bool isAdmin(const QString& username) const;

    bool createUser(const QString& username, const QString& password, bool isAdmin = false);
    bool changePassword(const QString& username, const QString& oldPassword, const QString& newPassword);
    
    QString registerToken(const QString& username);
    bool verifyToken(const QString& username, const QString& tokenBase64);
    bool loginWithToken(const QString& username, const QString& tokenBase64);
    bool revokeToken(const QString& username);
    
    QByteArray encryptForStorage(const QByteArray& plaintext) const;
    QByteArray decryptFromStorage(const QByteArray& ciphertext) const;
    
signals:
    void loginSuccess(const QString& username);
    void loginFailed(const QString& error);
    void logoutPerformed();
    void accountLocked(const QString& username, int secondsRemaining);
    
private:
    AuthService(QObject* parent = nullptr);
    
    QString hashPassword(const QString& password, const QByteArray& salt);
    QByteArray generateSalt();
    bool verifyPassword(const QString& password, const QString& storedValue);
    void loadUsers();
    void saveUsers();
    bool isLockedOut(const QString& username);
    void recordFailedAttempt(const QString& username);
    void clearFailedAttempts(const QString& username);

    // S1 修复：密钥派生（新版 AES-256-GCM 替代方案：HMAC-SHA256 流密码+认证标签）
    // 主密钥来源 = 机器特征码 + 安装时随机生成的盐值（存储在 QSettings，非硬编码）
    QByteArray deriveEncryptionKey() const;
    // 安装盐：首次运行生成 32 字节随机值并持久化，后续读取复用
    QByteArray getOrCreateInstallSalt() const;
    // 子密钥派生：从主密钥派生加密密钥/认证密钥（标签化分离）
    static QByteArray deriveSubKey(const QByteArray& masterKey, const QByteArray& label);
    // 旧版 XOR 派生（仅用于解密历史数据，向后兼容）
    QByteArray deriveEncryptionKeyLegacy() const;
    // 旧版 XOR 解密（向后兼容，解密 v2: 前缀出现前的历史数据）
    QByteArray decryptLegacyXOR(const QByteArray& ciphertext) const;
    
    static const int MAX_FAILED_ATTEMPTS = 5;
    static const int LOCKOUT_WINDOW_SECS = 900;
    static const int BASE_DELAY_MS = 500;
    
    bool m_authenticated = false;
    bool m_firstRun = false;
    QString m_currentUser;
    QMap<QString, QString> m_passwordHashes;
    QMap<QString, bool> m_adminFlags;  ///< P1-C4: 用户管理员标记（持久化到 QSettings）
    QMap<QString, QPair<QByteArray, QDateTime>> m_tokens;
    QMap<QString, QPair<int, QDateTime>> m_failedAttempts;
    mutable QMutex m_lockoutMutex;
};

#endif // AUTHSERVICE_H