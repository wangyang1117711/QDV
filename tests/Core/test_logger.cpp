#include "../catch2/catch2_minimal.hpp"
#include "Core/Logger.h"
#include <QString>

using namespace QDV;

TEST_CASE("Logger singleton模式", "[logger]") {
    Logger* l1 = Logger::instance();
    Logger* l2 = Logger::instance();
    REQUIRE(l1 == l2);
    REQUIRE(l1 != nullptr);
}

TEST_CASE("Logger debug级别日志", "[logger]") {
    REQUIRE_NOTHROW(Logger::debug("debug level test message"));
}

TEST_CASE("Logger info级别日志", "[logger]") {
    REQUIRE_NOTHROW(Logger::info("info level test message"));
}

TEST_CASE("Logger warn级别日志", "[logger]") {
    REQUIRE_NOTHROW(Logger::warn("warn level test message"));
}

TEST_CASE("Logger error级别日志", "[logger]") {
    REQUIRE_NOTHROW(Logger::error("error level test message"));
}

TEST_CASE("Logger critical级别日志", "[logger]") {
    REQUIRE_NOTHROW(Logger::critical("critical level test message"));
}

TEST_CASE("Logger trace级别日志", "[logger]") {
    REQUIRE_NOTHROW(Logger::trace("trace level test message"));
}

TEST_CASE("Logger中文消息日志", "[logger]") {
    REQUIRE_NOTHROW(Logger::info(QString::fromLatin1("中文测试消息")));
    REQUIRE_NOTHROW(Logger::warn(QString::fromLatin1("警告消息")));
    REQUIRE_NOTHROW(Logger::error(QString::fromLatin1("错误校验")));
}

TEST_CASE("Logger数字消息日志", "[logger]") {
    REQUIRE_NOTHROW(Logger::info("数字测试 1234567890"));
    REQUIRE_NOTHROW(Logger::debug(QString("pi = %1").arg(3.1415926535)));
    REQUIRE_NOTHROW(Logger::info(QString("max int = %1").arg(INT_MAX)));
}

TEST_CASE("Logger特殊字符消息日志", "[logger]") {
    REQUIRE_NOTHROW(Logger::info("特殊字符 !@#$%^&*()_+-=[]{}|;:',.<>?/~`"));
    REQUIRE_NOTHROW(Logger::debug("转义字符 backslash n t r"));
    REQUIRE_NOTHROW(Logger::warn("Unicode: (c) (r) (tm)"));
}

TEST_CASE("Logger连续大量日志1000条", "[logger]") {
    for (int i = 0; i < 1000; ++i) {
        Logger::info(QString("Log message number %1").arg(i));
    }
    SUCCEED("1000条日志写入完成");
}

TEST_CASE("Logger shutdown后操作安全", "[logger]") {
    Logger::shutdown();
    REQUIRE_NOTHROW(Logger::info("after shutdown info"));
    REQUIRE_NOTHROW(Logger::debug("after shutdown debug"));
    REQUIRE_NOTHROW(Logger::warn("after shutdown warn"));
    REQUIRE_NOTHROW(Logger::error("after shutdown error"));
}

TEST_CASE("Logger连续大量日志2000条", "[logger]") {
    for (int i = 0; i < 2000; ++i) {
        Logger::info(QString("Bulk log entry %1 of 2000").arg(i + 1));
    }
    SUCCEED("2000条日志写入完成");
}