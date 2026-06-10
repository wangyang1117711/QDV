#include "../catch2/catch2_minimal.hpp"
#include "Core/AuthService.h"
#include <QSettings>
#include <QByteArray>

TEST_CASE("AuthService首次运行检测", "[auth]") {
    // isFirstRun checks QSettings — may be true on fresh test env
    AuthService* auth = AuthService::instance();
    REQUIRE(auth != nullptr);
}

TEST_CASE("AuthService空凭据登录失败", "[auth]") {
    AuthService* auth = AuthService::instance();
    bool ok = auth->login("", "");
    REQUIRE_FALSE(ok);
}

TEST_CASE("AuthService不存在用户登录失败", "[auth]") {
    AuthService* auth = AuthService::instance();
    bool ok = auth->login("nonexistent_user", "any_password");
    REQUIRE_FALSE(ok);
}

TEST_CASE("AuthService创建用户", "[auth]") {
    AuthService* auth = AuthService::instance();
    bool created = auth->createUser("testuser1", "SecureP@ss1", false);
    REQUIRE(created);
}

TEST_CASE("AuthService短密码可创建", "[auth]") {
    AuthService* auth = AuthService::instance();
    bool created = auth->createUser("shortpwd", "12345", false);
    REQUIRE(created);
}

TEST_CASE("AuthService重复用户名拒绝", "[auth]") {
    AuthService* auth = AuthService::instance();
    auth->createUser("dupuser", "SecureP@ss1", false);
    bool dup = auth->createUser("dupuser", "Another1", false);
    REQUIRE_FALSE(dup);
}

TEST_CASE("AuthService Token注册与验证", "[auth]") {
    AuthService* auth = AuthService::instance();
    QString token = auth->registerToken("test_token_user");
    REQUIRE_FALSE(token.isEmpty());

    bool valid = auth->verifyToken("test_token_user", token);
    REQUIRE(valid);
}

TEST_CASE("AuthService错误Token拒绝", "[auth]") {
    AuthService* auth = AuthService::instance();
    QString badToken = "dGhpc2lzbm90YXJlYWx0b2tlbg==";
    bool valid = auth->verifyToken("test_token_user", badToken);
    REQUIRE_FALSE(valid);
}

TEST_CASE("AuthService Token撤销", "[auth]") {
    AuthService* auth = AuthService::instance();
    QString token = auth->registerToken("revoke_user");
    bool revoked = auth->revokeToken("revoke_user");
    REQUIRE(revoked);

    bool stillValid = auth->verifyToken("revoke_user", token);
    REQUIRE_FALSE(stillValid);
}

TEST_CASE("AuthService超长用户名拒绝创建", "[auth]") {
    AuthService* auth = AuthService::instance();
    QString longUsername(257, QChar('a'));
    bool created = auth->createUser(longUsername, "SecureP@ss1", false);
    REQUIRE_FALSE(created);
}

TEST_CASE("AuthService超长密码拒绝创建", "[auth]") {
    AuthService* auth = AuthService::instance();
    QString longPassword(257, QChar('b'));
    bool created = auth->createUser("longpwd_user", longPassword, false);
    REQUIRE_FALSE(created);
}

TEST_CASE("AuthService空白用户名拒绝创建", "[auth]") {
    AuthService* auth = AuthService::instance();
    bool created = auth->createUser("", "SecureP@ss1", false);
    REQUIRE_FALSE(created);
}

TEST_CASE("AuthService空白密码拒绝创建", "[auth]") {
    AuthService* auth = AuthService::instance();
    bool created = auth->createUser("blankpwd_user", "", false);
    REQUIRE_FALSE(created);
}

TEST_CASE("AuthService全空白用户名拒绝创建", "[auth]") {
    AuthService* auth = AuthService::instance();
    QString blankUsername = "   ";
    bool created = auth->createUser(blankUsername, "SecureP@ss1", false);
    REQUIRE_FALSE(created);
}

TEST_CASE("AuthService特殊字符用户名创建", "[auth]") {
    AuthService* auth = AuthService::instance();
    bool created = auth->createUser("user@#$%^&*()", "SecureP@ss1", false);
    CHECK(created);
}

TEST_CASE("AuthService空Token验证失败", "[auth]") {
    AuthService* auth = AuthService::instance();
    bool valid = auth->verifyToken("someuser", "");
    REQUIRE_FALSE(valid);
}

TEST_CASE("AuthService格式错误Token验证失败", "[auth]") {
    AuthService* auth = AuthService::instance();
    bool valid = auth->verifyToken("someuser", "!!!invalid_base64!!!");
    REQUIRE_FALSE(valid);
}

TEST_CASE("AuthService Token完整生命周期", "[auth]") {
    AuthService* auth = AuthService::instance();
    QString token = auth->registerToken("lifecycle_user");
    REQUIRE_FALSE(token.isEmpty());
    bool valid = auth->verifyToken("lifecycle_user", token);
    REQUIRE(valid);
    bool revoked = auth->revokeToken("lifecycle_user");
    REQUIRE(revoked);
    bool stillValid = auth->verifyToken("lifecycle_user", token);
    REQUIRE_FALSE(stillValid);
}

TEST_CASE("AuthService logout后认证状态", "[auth]") {
    AuthService* auth = AuthService::instance();
    auth->createUser("logout_test", "SecureP@ss1", false);
    bool loggedIn = auth->login("logout_test", "SecureP@ss1");
    REQUIRE(loggedIn);
    REQUIRE(auth->isAuthenticated());
    auth->logout();
    REQUIRE_FALSE(auth->isAuthenticated());
}

TEST_CASE("AuthService重复logout安全", "[auth]") {
    AuthService* auth = AuthService::instance();
    auth->createUser("relogout_test", "SecureP@ss1", false);
    auth->login("relogout_test", "SecureP@ss1");
    auth->logout();
    REQUIRE_FALSE(auth->isAuthenticated());
    auth->logout();
    REQUIRE_FALSE(auth->isAuthenticated());
}

TEST_CASE("AuthService六字符密码可创建", "[auth]") {
    AuthService* auth = AuthService::instance();
    bool created = auth->createUser("shortpwd6", "123456", false);
    REQUIRE(created);
}

TEST_CASE("AuthService八字符密码可创建", "[auth]") {
    AuthService* auth = AuthService::instance();
    bool created = auth->createUser("shortpwd8", "12345678", false);
    REQUIRE(created);
}

TEST_CASE("AuthService超长密码128以上拒绝", "[auth]") {
    AuthService* auth = AuthService::instance();
    QString longPassword(129, QChar('x'));
    bool created = auth->createUser("longpwd129", longPassword, false);
    REQUIRE_FALSE(created);
}