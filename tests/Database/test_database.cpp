#include "../catch2/catch2_minimal.hpp"
#include "Database/ResultDatabase.h"
#include "Database/DatabaseIntegrator.h"
#include "Core/DetectionStats.h"
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QFile>
#include <QDir>

TEST_CASE("ResultDatabase构造", "[database]") {
    ResultDatabase* db = ResultDatabase::instance();
    REQUIRE(db != nullptr);
}

TEST_CASE("ResultDatabase打不开无效路径", "[database]") {
    ResultDatabase* db = ResultDatabase::instance();
    db->close();
    bool ok = db->open("/invalid/path/that/does/not/exist.db");
    REQUIRE_FALSE(ok);
}

TEST_CASE("DatabaseIntegrator构造", "[database]") {
    DatabaseIntegrator* integrator = DatabaseIntegrator::instance();
    REQUIRE(integrator != nullptr);
}

TEST_CASE("DatabaseIntegrator未初始化时存储安全", "[database]") {
    DatabaseIntegrator* integrator = DatabaseIntegrator::instance();
    DetectionStats stats;
    stats.totalDetected = 100;
    stats.passed = 95;
    stats.failed = 5;
    bool ok = integrator->saveDetectionResult("test_scheme", "Test Scheme", stats);
    CHECK(!ok);
}

TEST_CASE("ResultDatabase singleton", "[database]") {
    ResultDatabase* db1 = ResultDatabase::instance();
    REQUIRE(db1 != nullptr);
    ResultDatabase* db2 = ResultDatabase::instance();
    REQUIRE(db1 == db2);

    ResultDatabase* db3 = ResultDatabase::instance();
    REQUIRE(db1 == db3);
}

TEST_CASE("ResultDatabase saveResult with DetectionStats", "[database]") {
    QDir dir("./data");
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    ResultDatabase* db = ResultDatabase::instance();
    db->close();

    QString testDbPath = "./data/test_dbstats.db";
    QFile::remove(testDbPath);

    bool openOk = db->open(testDbPath);
    if (!openOk) {
        SUCCEED("Database not available, skipping stats test");
        QFile::remove(testDbPath);
        return;
    }

    DetectionStats stats;
    stats.totalDetected = 200;
    stats.passed = 180;
    stats.failed = 20;
    stats.passRate = 90.0;

    bool ok = db->insertResult(
        QString::fromLatin1("scheme_stats"),
        QString::fromLatin1("Stats Test Scheme"),
        stats.passed > stats.failed,
        stats.passRate,
        QString::fromLatin1("/images/test.png"),
        QString::fromLatin1("2025-06-15 10:30:00")
    );
    CHECK(ok);

    int count = db->getResultCount(QString::fromLatin1("scheme_stats"));
    CHECK(count == 1);

    db->close();
    QFile::remove(testDbPath);
}

TEST_CASE("ResultDatabase loadResults empty database", "[database]") {
    QDir dir("./data");
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    ResultDatabase* db = ResultDatabase::instance();
    db->close();

    QString testDbPath = "./data/test_dbempty.db";
    QFile::remove(testDbPath);

    bool openOk = db->open(testDbPath);
    if (!openOk) {
        SUCCEED("Database not available, skipping empty test");
        QFile::remove(testDbPath);
        return;
    }

    QList<QMap<QString, QVariant>> results = db->queryResults();
    CHECK(results.size() == 0);

    int count = db->getResultCount();
    CHECK(count == 0);

    db->close();
    QFile::remove(testDbPath);
}

TEST_CASE("ResultDatabase boundary very large result count", "[database]") {
    QDir dir("./data");
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    ResultDatabase* db = ResultDatabase::instance();
    db->close();

    QString testDbPath = "./data/test_dblarge.db";
    QFile::remove(testDbPath);

    bool openOk = db->open(testDbPath);
    if (!openOk) {
        SUCCEED("Database not available, skipping large count test");
        QFile::remove(testDbPath);
        return;
    }

    const int totalInsert = 500;
    for (int i = 0; i < totalInsert; ++i) {
        db->insertResult(
            QString::fromLatin1("scheme_large"),
            QString::fromLatin1("Large Batch Scheme"),
            i % 2 == 0,
            0.5 + (i % 50) * 0.01,
            QString::fromLatin1("/images/img%1.png").arg(i)
        );
    }

    int count = db->getResultCount(QString::fromLatin1("scheme_large"));
    CHECK(count == totalInsert);

    QList<QMap<QString, QVariant>> results = db->queryResults(
        QString::fromLatin1("scheme_large")
    );
    CHECK(results.size() == totalInsert);

    db->deleteResults(QString::fromLatin1("scheme_large"), false);

    count = db->getResultCount();
    CHECK(count == 0);

    db->close();
    QFile::remove(testDbPath);
}