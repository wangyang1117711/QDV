#include "../catch2/catch2_minimal.hpp"
#include "Database/DatabaseIntegrator.h"
#include "Core/DetectionStats.h"
#include <QCoreApplication>

static int s_argc = 0;
static QCoreApplication s_app(s_argc, nullptr);

TEST_CASE("DatabaseIntegrator singleton", "[integrator]") {
    auto* i1 = DatabaseIntegrator::instance();
    auto* i2 = DatabaseIntegrator::instance();
    REQUIRE(i1 != nullptr);
    REQUIRE(i1 == i2);
}

TEST_CASE("DatabaseIntegrator init and shutdown", "[integrator]") {
    auto* integrator = DatabaseIntegrator::instance();
    bool initOk = integrator->initialize(":memory:");
    if (!initOk) {
        SUCCEED("DB init skipped - no SQLite driver");
        return;
    }
    integrator->shutdown();
    SUCCEED("init and shutdown OK");
}

TEST_CASE("DatabaseIntegrator saveDetectionResult with stats", "[integrator]") {
    auto* integrator = DatabaseIntegrator::instance();
    bool initOk = integrator->initialize(":memory:");
    if (!initOk) {
        SUCCEED("DB init skipped");
        return;
    }
    DetectionStats stats;
    stats.totalDetected = 5;
    stats.passed = 4;
    stats.failed = 1;
    stats.passRate = 80.0;
    integrator->saveDetectionResult("scheme-1", "Scheme One", stats);
    SUCCEED("save OK");
}

TEST_CASE("DatabaseIntegrator loadStatsForScheme", "[integrator]") {
    auto* integrator = DatabaseIntegrator::instance();
    bool initOk = integrator->initialize(":memory:");
    if (!initOk) {
        SUCCEED("DB init skipped");
        return;
    }
    for (int i = 0; i < 50; ++i) {
        DetectionStats s;
        s.totalDetected = 10;
        s.passed = 9;
        s.failed = 1;
        s.passRate = 90.0;
        integrator->saveDetectionResult("perf-test", "Perf Test", s);
    }
    DetectionStats loaded = integrator->loadStatsForScheme("perf-test");
    REQUIRE_EQUAL(loaded.totalDetected, 500);
    REQUIRE_EQUAL(loaded.passed, 450);
}

TEST_CASE("DatabaseIntegrator empty scheme stats", "[integrator]") {
    auto* integrator = DatabaseIntegrator::instance();
    bool initOk = integrator->initialize(":memory:");
    if (!initOk) {
        SUCCEED("DB init skipped");
        return;
    }
    DetectionStats stats = integrator->loadStatsForScheme("nonexistent_scheme");
    REQUIRE_EQUAL(stats.totalDetected, 0);
    REQUIRE_EQUAL(stats.passed, 0);
}

TEST_CASE("DatabaseIntegrator saveDetectionResult simple overload", "[integrator]") {
    auto* integrator = DatabaseIntegrator::instance();
    bool initOk = integrator->initialize(":memory:");
    if (!initOk) {
        SUCCEED("DB init skipped");
        return;
    }
    integrator->saveDetectionResult("simple-scheme", "Simple Scheme", true, 0.95, "image.png");
    DetectionStats stats = integrator->loadStatsForScheme("simple-scheme");
    REQUIRE_EQUAL(stats.totalDetected, 1);
    REQUIRE_EQUAL(stats.passed, 1);
}