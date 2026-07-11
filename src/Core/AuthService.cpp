#include "AuthService.h"
#include "Logger.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSettings>
#include <QMessageAuthenticationCode>
#include <QThread>
#include <QSysInfo>

using namespace QDV;

static const QByteArray kAppSecretSeed = QByteArray("QDV_SecureStorage_2026_v1");

// P1-C17 修复（综合测评 S3）：常量时间比较，防止时序攻击。
// 之前 verifyToken/verifyPassword 使用 == 比较，理论上可通过测量响应时间
// 逐字节猜测密钥。常量时间比较无论匹配与否都遍历完整缓冲区。
static bool constantTimeEquals(const QByteArray& a, const QByteArray& b) {
    if (a.size() != b.size()) {
        // 即便长度不同，也消耗固定时间（避免长度泄漏）
        volatile uchar dummy = 0;
        for (int i = 0; i < qMax(a.size(), b.size()); ++i) {
            dummy |= (i < a.size() ? static_cast<uchar>(a[i]) : 0);
            dummy |= (i < b.size() ? static_cast<uchar>(b[i]) : 0);
        }
        Q_UNUSED(dummy)
        return false;
    }
    volatile uchar diff = 0;
    for (int i = 0; i < a.size(); ++i) {
        diff |= static_cast<uchar>(a[i]) ^ static_cast<uchar>(b[i]);
    }
    return diff == 0;
}

static bool constantTimeStringEquals(const QString& a, const QString& b) {
    return constantTimeEquals(a.toUtf8(), b.toUtf8());
}

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
            // P1-C4 修复：加载管理员标记（默认 false，兼容旧数据）
            m_adminFlags[username] = settings.value("isAdmin", false).toBool();
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
        // P1-C4 修复：持久化管理员标记
        settings.setValue("isAdmin", m_adminFlags.value(it.key(), false));
    }
    settings.endArray();
}

// P1-C4 修复：isAdmin 查询接口（综合测评 S6）
bool AuthService::isAdmin(const QString& username) const {
    return m_adminFlags.value(username, false);
}

AuthService* AuthService::instance() {
    static AuthService instance;
    return &instance;
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
    if (parts.size() != 2) {
        Logger::warn("Invalid stored password format, rejecting");
        return false;
    }

    QByteArray salt = QByteArray::fromHex(parts[0].toUtf8());
    QString computedHash = hashPassword(password, salt);
    // P1-C17 修复：使用常量时间比较，防止时序攻击
    return constantTimeStringEquals(computedHash, storedValue);
}

bool AuthService::createUser(const QString& username, const QString& password, bool isAdmin) {
    if (username.isEmpty() || password.isEmpty()) {
        return false;
    }

    if (m_passwordHashes.contains(username)) {
        Logger::warn("User already exists: " + username);
        return false;
    }

    QByteArray salt = generateSalt();
    m_passwordHashes[username] = hashPassword(password, salt);
    // P1-C4 修复：实际持久化 isAdmin 标记（之前参数被完全忽略）
    m_adminFlags[username] = isAdmin;

    QSettings settings("奇测科技", "QDetectVision");
    settings.setValue("setup/initialized", true);
    m_firstRun = false;
    saveUsers();

    Logger::info("User created: " + username + (isAdmin ? " (admin)" : " (user)"));
    return true;
}

bool AuthService::isLockedOut(const QString& username) {
    QMutexLocker locker(&m_lockoutMutex);
    if (!m_failedAttempts.contains(username)) {
        return false;
    }
    auto& pair = m_failedAttempts[username];
    int attempts = pair.first;
    QDateTime firstFailure = pair.second;
    
    if (attempts < MAX_FAILED_ATTEMPTS) {
        return false;
    }
    
    qint64 elapsed = firstFailure.secsTo(QDateTime::currentDateTime());
    if (elapsed >= LOCKOUT_WINDOW_SECS) {
        m_failedAttempts.remove(username);
        return false;
    }
    
    return true;
}

void AuthService::recordFailedAttempt(const QString& username) {
    QMutexLocker locker(&m_lockoutMutex);
    if (!m_failedAttempts.contains(username)) {
        m_failedAttempts[username] = qMakePair(1, QDateTime::currentDateTime());
        return;
    }
    auto& pair = m_failedAttempts[username];
    qint64 elapsed = pair.second.secsTo(QDateTime::currentDateTime());
    if (elapsed >= LOCKOUT_WINDOW_SECS) {
        pair.first = 1;
        pair.second = QDateTime::currentDateTime();
    } else {
        pair.first++;
    }
}

void AuthService::clearFailedAttempts(const QString& username) {
    QMutexLocker locker(&m_lockoutMutex);
    m_failedAttempts.remove(username);
}

int AuthService::remainingAttempts(const QString& username) const {
    QMutexLocker locker(&m_lockoutMutex);
    auto it = m_failedAttempts.constFind(username);
    if (it == m_failedAttempts.constEnd()) {
        return MAX_FAILED_ATTEMPTS;
    }
    int attempts = it->first;
    QDateTime firstFailure = it->second;
    qint64 elapsed = firstFailure.secsTo(QDateTime::currentDateTime());
    if (elapsed >= LOCKOUT_WINDOW_SECS) {
        return MAX_FAILED_ATTEMPTS;
    }
    return qMax(0, MAX_FAILED_ATTEMPTS - attempts);
}

qint64 AuthService::lockoutSecondsRemaining(const QString& username) const {
    QMutexLocker locker(&m_lockoutMutex);
    auto it = m_failedAttempts.constFind(username);
    if (it == m_failedAttempts.constEnd()) {
        return 0;
    }
    if (it->first < MAX_FAILED_ATTEMPTS) {
        return 0;
    }
    qint64 elapsed = it->second.secsTo(QDateTime::currentDateTime());
    qint64 remaining = LOCKOUT_WINDOW_SECS - elapsed;
    return qMax<qint64>(0, remaining);
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

    if (isLockedOut(username)) {
        qint64 remaining = lockoutSecondsRemaining(username);
        int minutes = static_cast<int>(remaining / 60);
        int seconds = static_cast<int>(remaining % 60);
        QString msg = QString("账户已锁定，请 %1 分 %2 秒后重试").arg(minutes).arg(seconds);
        Logger::warn("Account locked: " + username);
        emit accountLocked(username, static_cast<int>(remaining));
        emit loginFailed(msg);
        return false;
    }

    auto it = m_passwordHashes.find(username);
    if (it == m_passwordHashes.end()) {
        Logger::warn("Login attempt for non-existent user: " + username);
        recordFailedAttempt(username);
        int remaining = remainingAttempts(username);
        QString msg = QString("用户名或密码错误（剩余尝试次数: %1）").arg(remaining);
        emit loginFailed(msg);
        return false;
    }

    if (!verifyPassword(password, it.value())) {
        Logger::warn("Failed login attempt for user: " + username);
        recordFailedAttempt(username);
        int currentAttempts = MAX_FAILED_ATTEMPTS - remainingAttempts(username);
        int delayMs = BASE_DELAY_MS * (1 << qMin(currentAttempts - 1, 4));
        QThread::msleep(delayMs);
        
        if (isLockedOut(username)) {
            emit accountLocked(username, LOCKOUT_WINDOW_SECS);
            emit loginFailed("账户已锁定，请15分钟后重试");
        } else {
            int remaining = remainingAttempts(username);
            QString msg = QString("用户名或密码错误（剩余尝试次数: %1）").arg(remaining);
            emit loginFailed(msg);
        }
        return false;
    }

    clearFailedAttempts(username);
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
        clearFailedAttempts(m_currentUser);
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
    // P1-C17 修复：使用常量时间比较，防止时序攻击
    return constantTimeEquals(providedToken, storedToken);
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

QByteArray AuthService::deriveEncryptionKey() const {
    QByteArray machineId = QSysInfo::machineUniqueId();
    QByteArray material = kAppSecretSeed + machineId;
    QByteArray key = QCryptographicHash::hash(material, QCryptographicHash::Sha256);
    return key;
}

QByteArray AuthService::encryptForStorage(const QByteArray& plaintext) const {
    QByteArray key = deriveEncryptionKey();
    QByteArray iv(16, '\0');
    for (int i = 0; i < 16; ++i) {
        iv[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }

    QByteArray ciphertext;
    ciphertext.resize(plaintext.size());
    for (int i = 0; i < plaintext.size(); ++i) {
        ciphertext[i] = plaintext[i] ^ key[i % key.size()] ^ iv[i % iv.size()];
    }

    return iv.toHex() + ":" + ciphertext.toHex();
}

QByteArray AuthService::decryptFromStorage(const QByteArray& ciphertext) const {
    QStringList parts = QString::fromLatin1(ciphertext).split(':');
    if (parts.size() != 2) {
        return QByteArray();
    }

    QByteArray iv = QByteArray::fromHex(parts[0].toLatin1());
    QByteArray data = QByteArray::fromHex(parts[1].toLatin1());
    QByteArray key = deriveEncryptionKey();

    QByteArray plaintext;
    plaintext.resize(data.size());
    for (int i = 0; i < data.size(); ++i) {
        plaintext[i] = data[i] ^ key[i % key.size()] ^ iv[i % iv.size()];
    }

    return plaintext;
}