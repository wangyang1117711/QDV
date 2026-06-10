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
    QByteArray deriveEncryptionKey() const;
    
    static const int MAX_FAILED_ATTEMPTS = 5;
    static const int LOCKOUT_WINDOW_SECS = 900;
    static const int BASE_DELAY_MS = 500;
    
    bool m_authenticated = false;
    bool m_firstRun = false;
    QString m_currentUser;
    QMap<QString, QString> m_passwordHashes;
    QMap<QString, QPair<QByteArray, QDateTime>> m_tokens;
    QMap<QString, QPair<int, QDateTime>> m_failedAttempts;
    mutable QMutex m_lockoutMutex;
};

#endif // AUTHSERVICE_H