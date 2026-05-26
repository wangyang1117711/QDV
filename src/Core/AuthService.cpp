#include "AuthService.h"
#include "Logger.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSettings>
#include <QMessageAuthenticationCode>

using namespace QDV;

AuthService* AuthService::s_instance = nullptr;

AuthService::AuthService(QObject* parent) : QObject(parent) {
    QSettings settings("奇测科技", "QDetectVision");
    if (!settings.contains("setup/initialized")) {
        m_firstRun = true;
    } else {
        loadUsers();
    }
}

void AuthService::loadUsers() {
    QSettings settings("奇测科技", "QDetectVision");
    int size = settings.beginReadArray("users");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QString username = settings.value("name").toString();
        QString hash = settings.value("hash").toString();
        if (!username.isEmpty() && !hash.isEmpty()) {
            m_passwordHashes[username] = hash;
        }
    }
    settings.endArray();
}

void AuthService::saveUsers() {
    QSettings settings("奇测科技", "QDetectVision");
    settings.beginWriteArray("users", m_passwordHashes.size());
    int i = 0;
    for (auto it = m_passwordHashes.begin(); it != m_passwordHashes.end(); ++it, ++i) {
        settings.setArrayIndex(i);
        settings.setValue("name", it.key());
        settings.setValue("hash", it.value());
    }
    settings.endArray();
}

AuthService* AuthService::instance() {
    if (!s_instance) {
        s_instance = new AuthService();
    }
    return s_instance;
}

QByteArray AuthService::generateSalt() {
    QByteArray salt(16, '\0');
    for (int i = 0; i < 16; ++i) {
        salt[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return salt;
}

QString AuthService::hashPassword(const QString& password, const QByteArray& salt) {
    const int iterations = 100000;
    const int dkLen = 32;

    QByteArray derived = QMessageAuthenticationCode::hash(
        password.toUtf8(), salt, QCryptographicHash::Sha256
    );

    for (int i = 1; i < iterations; ++i) {
        derived = QMessageAuthenticationCode::hash(
            derived, salt, QCryptographicHash::Sha256
        );
    }

    return (salt.toHex() + ":" + derived.toHex());
}

bool AuthService::verifyPassword(const QString& password, const QString& storedValue) {
    QStringList parts = storedValue.split(':');
    if (parts.size() == 1) {
        QByteArray legacyHash = QCryptographicHash::hash(
            password.toUtf8(), QCryptographicHash::Sha256
        );
        return legacyHash.toHex() == parts[0];
    }

    QByteArray salt = QByteArray::fromHex(parts[0].toUtf8());
    QString computedHash = hashPassword(password, salt);
    return computedHash == storedValue;
}

bool AuthService::createUser(const QString& username, const QString& password, bool isAdmin) {
    if (username.isEmpty() || password.isEmpty()) {
        return false;
    }

    if (password.length() < 8) {
        Logger::warn("Password too short for user: " + username);
        return false;
    }

    if (m_passwordHashes.contains(username)) {
        Logger::warn("User already exists: " + username);
        return false;
    }

    QByteArray salt = generateSalt();
    m_passwordHashes[username] = hashPassword(password, salt);

    QSettings settings("奇测科技", "QDetectVision");
    settings.setValue("setup/initialized", true);
    m_firstRun = false;
    saveUsers();

    Logger::info("User created: " + username + (isAdmin ? " (admin)" : ""));
    return true;
}

bool AuthService::login(const QString& username, const QString& password) {
    if (m_firstRun) {
        Logger::warn("Login attempt before setup completion");
        emit loginFailed("请先创建管理员账户");
        return false;
    }

    if (username.isEmpty() || password.isEmpty()) {
        Logger::warn("Login attempt with empty credentials");
        emit loginFailed("用户名和密码不能为空");
        return false;
    }

    auto it = m_passwordHashes.find(username);
    if (it == m_passwordHashes.end()) {
        Logger::warn("Login attempt for non-existent user: " + username);
        emit loginFailed("用户名或密码错误");
        return false;
    }

    if (!verifyPassword(password, it.value())) {
        Logger::warn("Failed login attempt for user: " + username);
        emit loginFailed("用户名或密码错误");
        return false;
    }

    m_authenticated = true;
    m_currentUser = username;
    Logger::info("User logged in: " + username);
    emit loginSuccess(username);
    return true;
}

void AuthService::logout() {
    if (m_authenticated) {
        Logger::info("User logged out: " + m_currentUser);
        if (m_tokens.contains(m_currentUser)) {
            m_tokens.remove(m_currentUser);
        }
        m_currentUser.clear();
        m_authenticated = false;
        emit logoutPerformed();
    }
}

bool AuthService::changePassword(const QString& username, const QString& oldPassword, const QString& newPassword) {
    if (!isAuthenticated() || m_currentUser != username) {
        return false;
    }

    auto it = m_passwordHashes.find(username);
    if (it == m_passwordHashes.end()) {
        return false;
    }

    if (!verifyPassword(oldPassword, it.value())) {
        return false;
    }

    if (newPassword.length() < 8) {
        return false;
    }

    QByteArray salt = generateSalt();
    m_passwordHashes[username] = hashPassword(newPassword, salt);
    saveUsers();
    Logger::info("Password changed for user: " + username);
    return true;
}

QString AuthService::registerToken(const QString& username) {
    QByteArray token(32, '\0');
    for (int i = 0; i < 32; ++i) {
        token[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }

    QDateTime expiry = QDateTime::currentDateTime().addDays(30);
    m_tokens[username] = qMakePair(token, expiry);

    return token.toBase64();
}

bool AuthService::verifyToken(const QString& username, const QString& tokenBase64) {
    if (!m_tokens.contains(username)) {
        return false;
    }

    auto& pair = m_tokens[username];
    QByteArray storedToken = pair.first;
    QDateTime expiry = pair.second;

    if (QDateTime::currentDateTime() > expiry) {
        m_tokens.remove(username);
        return false;
    }

    QByteArray providedToken = QByteArray::fromBase64(tokenBase64.toUtf8());
    return providedToken == storedToken;
}

bool AuthService::loginWithToken(const QString& username, const QString& tokenBase64) {
    if (!verifyToken(username, tokenBase64)) {
        return false;
    }

    if (!m_passwordHashes.contains(username)) {
        return false;
    }

    m_authenticated = true;
    m_currentUser = username;
    Logger::info("User auto-logged in via token: " + username);
    emit loginSuccess(username);
    return true;
}

bool AuthService::revokeToken(const QString& username) {
    if (m_tokens.contains(username)) {
        m_tokens.remove(username);
        return true;
    }
    return false;
}