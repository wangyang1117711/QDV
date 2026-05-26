#include <catch2/catch_all.hpp>
#include "AuthService.h"

TEST_CASE("AuthService Login", "[AuthService]") {
    SECTION("Successful login with valid credentials") {
        bool result = AuthService::instance()->login("admin", "admin123");
        REQUIRE(result == true);
        REQUIRE(AuthService::instance()->isAuthenticated() == true);
        REQUIRE(AuthService::instance()->currentUser() == "admin");
    }
    
    SECTION("Failed login with invalid username") {
        bool result = AuthService::instance()->login("nonexistent", "password");
        REQUIRE(result == false);
        REQUIRE(AuthService::instance()->isAuthenticated() == false);
    }
    
    SECTION("Failed login with wrong password") {
        bool result = AuthService::instance()->login("admin", "wrongpassword");
        REQUIRE(result == false);
        REQUIRE(AuthService::instance()->isAuthenticated() == false);
    }
    
    SECTION("Logout after login") {
        AuthService::instance()->login("admin", "admin123");
        REQUIRE(AuthService::instance()->isAuthenticated() == true);
        
        AuthService::instance()->logout();
        REQUIRE(AuthService::instance()->isAuthenticated() == false);
        REQUIRE(AuthService::instance()->currentUser().isEmpty());
    }
    
    SECTION("Login with empty credentials") {
        bool result = AuthService::instance()->login("", "");
        REQUIRE(result == false);
    }
    
    SECTION("Login with empty username") {
        bool result = AuthService::instance()->login("", "password");
        REQUIRE(result == false);
    }
    
    SECTION("Login with empty password") {
        bool result = AuthService::instance()->login("admin", "");
        REQUIRE(result == false);
    }
}

TEST_CASE("AuthService Password Change", "[AuthService]") {
    SECTION("Change password with correct old password") {
        bool loginResult = AuthService::instance()->login("admin", "admin123");
        REQUIRE(loginResult == true);
        
        bool changeResult = AuthService::instance()->changePassword("admin", "admin123", "newpassword123");
        REQUIRE(changeResult == true);
        
        AuthService::instance()->logout();
        
        bool newLoginResult = AuthService::instance()->login("admin", "newpassword123");
        REQUIRE(newLoginResult == true);
        
        AuthService::instance()->changePassword("admin", "newpassword123", "admin123");
        AuthService::instance()->logout();
    }
    
    SECTION("Change password with wrong old password") {
        bool changeResult = AuthService::instance()->changePassword("admin", "wrong", "newpassword");
        REQUIRE(changeResult == false);
    }
    
    SECTION("Change password for non-existent user") {
        bool changeResult = AuthService::instance()->changePassword("nonexistent", "old", "new");
        REQUIRE(changeResult == false);
    }
    
    SECTION("Change password with empty new password") {
        bool loginResult = AuthService::instance()->login("admin", "admin123");
        REQUIRE(loginResult == true);
        
        bool changeResult = AuthService::instance()->changePassword("admin", "admin123", "");
        REQUIRE(changeResult == false);
        
        AuthService::instance()->logout();
    }
}

TEST_CASE("AuthService Singleton", "[AuthService]") {
    SECTION("Instance is singleton") {
        AuthService* instance1 = AuthService::instance();
        AuthService* instance2 = AuthService::instance();
        REQUIRE(instance1 == instance2);
    }
}