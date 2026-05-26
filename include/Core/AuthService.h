#ifndef AUTHSERVICE_H
#define AUTHSERVICE_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QDateTime>

class AuthService : public QObject {
    Q_OBJECT
    
public:
    static AuthService* instance();
    
    bool login(const QString& username, const QString& password);
    void logout();
    
    bool isAuthenticated() const { return m_authenticated; }
    QString currentUser() const { return m_currentUser; }
    bool isFirstRun() const { return m_firstRun; }
    
    bool createUser(const QString& username, const QString& password, bool isAdmin = false);
    bool changePassword(const QString& username, const QString& oldPassword, const QString& newPassword);
    
    QString registerToken(const QString& username);
    bool verifyToken(const QString& username, const QString& tokenBase64);
    bool loginWithToken(const QString& username, const QString& tokenBase64);
    bool revokeToken(const QString& username);
    
signals:
    void loginSuccess(const QString& username);
    void loginFailed(const QString& error);
    void logoutPerformed();
    
private:
    AuthService(QObject* parent = nullptr);
    
    QString hashPassword(const QString& password, const QByteArray& salt);
    QByteArray generateSalt();
    bool verifyPassword(const QString& password, const QString& storedValue);
    void loadUsers();
    void saveUsers();
    
    bool m_authenticated = false;
    bool m_firstRun = false;
    QString m_currentUser;
    QMap<QString, QString> m_passwordHashes;
    QMap<QString, QPair<QByteArray, QDateTime>> m_tokens;
    
    static AuthService* s_instance;
};

#endif // AUTHSERVICE_H