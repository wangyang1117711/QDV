#include "../catch2/catch2_minimal.hpp"
#include "Core/Result.h"
#include <QString>

using namespace QDV;

TEST_CASE("Result ok with value", "[result]") {
    auto r = Result<int>::ok(42);
    REQUIRE(r.isOk());
    REQUIRE_FALSE(r.isErr());
    REQUIRE_EQUAL(r.value(), 42);
    REQUIRE_EQUAL(r.unwrapOr(0), 42);
}

TEST_CASE("Result error with value type", "[result]") {
    auto r = Result<QString>::err("something went wrong");
    REQUIRE(r.isErr());
    REQUIRE_FALSE(r.isOk());
    REQUIRE_EQUAL(r.error(), "something went wrong");
}

TEST_CASE("Result unwrapOr on error", "[result]") {
    auto r = Result<int>::err("failed");
    REQUIRE_EQUAL(r.unwrapOr(99), 99);
}

TEST_CASE("Result void ok", "[result]") {
    auto r = Result<void>::ok();
    REQUIRE(r.isOk());
    REQUIRE_FALSE(r.isErr());
}

TEST_CASE("Result void error", "[result]") {
    auto r = Result<void>::err("operation failed");
    REQUIRE(r.isErr());
    REQUIRE_EQUAL(r.error(), "operation failed");
}

TEST_CASE("Result map success", "[result]") {
    auto r = Result<int>::ok(10);
    auto mapped = r.map<int>([](int x) { return x * 2; });
    REQUIRE(mapped.isOk());
    REQUIRE_EQUAL(mapped.value(), 20);
}

TEST_CASE("Result map error propagates", "[result]") {
    auto r = Result<int>::err("bad input");
    auto mapped = r.map<int>([](int x) { return x * 2; });
    REQUIRE(mapped.isErr());
    REQUIRE_EQUAL(mapped.error(), "bad input");
}

TEST_CASE("Result andThen chains success", "[result]") {
    auto r = Result<int>::ok(5);
    auto chained = r.andThen<int>([](int x) {
        return Result<int>::ok(x + 3);
    });
    REQUIRE(chained.isOk());
    REQUIRE_EQUAL(chained.value(), 8);
}

TEST_CASE("Result andThen chains with early error", "[result]") {
    auto r = Result<int>::ok(5);
    auto chained = r.andThen<int>([](int x) {
        if (x < 10) return Result<int>::err("too small");
        return Result<int>::ok(x * 2);
    });
    REQUIRE(chained.isErr());
    REQUIRE_EQUAL(chained.error(), "too small");
}

TEST_CASE("Result bool conversion", "[result]") {
    auto ok = Result<int>::ok(1);
    auto err = Result<int>::err("fail");
    if (ok) {
        SUCCEED("ok converts to true");
    }
    if (!err) {
        SUCCEED("err converts to false");
    }
}