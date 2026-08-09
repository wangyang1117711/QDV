#ifndef CATCH2_MINIMAL_HPP
#define CATCH2_MINIMAL_HPP

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cmath>
#include <sstream>
#include <QString>

struct TestResult {
    std::string testName;
    bool passed = true;
    std::string message;
    std::function<void()> testFunc;  // 测试函数指针，由 main() 真正调用
};

inline std::vector<TestResult>& testResults() {
    static std::vector<TestResult> results;
    return results;
}

inline int& failedCount() {
    static int count = 0;
    return count;
}

inline int& passedCount() {
    static int count = 0;
    return count;
}

#define CONCAT_IMPL(a, b) a ## b
#define CONCAT(a, b) CONCAT_IMPL(a, b)

#define TEST_CASE(name, tag) \
    static void CONCAT(test_, __LINE__)(); \
    static struct CONCAT(TestRegistrar_, __LINE__) { \
        CONCAT(TestRegistrar_, __LINE__)() { \
            TestResult r; r.testName = name; \
            r.testFunc = CONCAT(test_, __LINE__); \
            testResults().push_back(r); \
        } \
    } CONCAT(testRegistrar_, __LINE__); \
    static void CONCAT(test_, __LINE__)()

#define SECTION(name) if (true)

#define REQUIRE(expr) do { \
    if (!(expr)) { \
        testResults().back().passed = false; \
        testResults().back().message = std::string("REQUIRE failed: ") + #expr \
            + " at " + __FILE__ + ":" + std::to_string(__LINE__); \
        failedCount()++; \
        return; \
    } \
} while(0)

#define CHECK(expr) do { \
    if (!(expr)) { \
        testResults().back().passed = false; \
        std::ostringstream oss; \
        oss << "CHECK failed: " << #expr << " at " << __FILE__ << ":" << __LINE__; \
        testResults().back().message = oss.str(); \
        failedCount()++; \
    } \
} while(0)

#define REQUIRE_EQUAL(a, b) do { \
    if ((a) != (b)) { \
        testResults().back().passed = false; \
        testResults().back().message = std::string("REQUIRE_EQUAL failed: ") + #a + " != " + #b; \
        failedCount()++; \
        return; \
    } \
} while(0)

#define REQUIRE_NEAR(a, b, epsilon) do { \
    if (std::abs((a) - (b)) > (epsilon)) { \
        testResults().back().passed = false; \
        std::ostringstream oss; \
        oss << "REQUIRE_NEAR failed: " << #a << " != " << #b \
            << " (|" << (a) << " - " << (b) << "| > " << (epsilon) << ")"; \
        testResults().back().message = oss.str(); \
        failedCount()++; \
        return; \
    } \
} while(0)

#define REQUIRE_FALSE(expr) do { \
    if (expr) { \
        testResults().back().passed = false; \
        testResults().back().message = std::string("REQUIRE_FALSE failed: ") + #expr + " is true"; \
        failedCount()++; \
        return; \
    } \
} while(0)

#define REQUIRE_NOTHROW(expr) do { \
    try { expr; } catch (...) { \
        testResults().back().passed = false; \
        testResults().back().message = std::string("REQUIRE_NOTHROW failed: ") + #expr + " threw"; \
        failedCount()++; \
        return; \
    } \
} while(0)

#define SUCCEED(msg) do { (void)(msg); } while(0)

#endif