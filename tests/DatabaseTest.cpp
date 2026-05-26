#include <catch2/catch_all.hpp>
#include "ResultDatabase.h"
#include <QDir>
#include <QFile>

TEST_CASE("ResultDatabase Operations", "[ResultDatabase]") {
    QString testDbPath = "./data/test_results.db";
    
    QDir dir("./data");
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    QFile::remove(testDbPath);
    
    SECTION("Open and close database") {
        bool openResult = ResultDatabase::instance()->open(testDbPath);
        REQUIRE(openResult == true);
        
        ResultDatabase::instance()->close();
    }
    
    SECTION("Insert single result") {
        ResultDatabase::instance()->open(testDbPath);
        
        bool insertResult = ResultDatabase::instance()->insertResult(
            "scheme-001", 
            "TestScheme", 
            true, 
            0.95, 
            "/path/to/image.png",
            "2024-01-01 12:00:00"
        );
        REQUIRE(insertResult == true);
        
        int count = ResultDatabase::instance()->getResultCount();
        REQUIRE(count == 1);
        
        ResultDatabase::instance()->close();
    }
    
    SECTION("Insert multiple results") {
        ResultDatabase::instance()->open(testDbPath);
        
        for (int i = 0; i < 5; ++i) {
            ResultDatabase::instance()->insertResult(
                "scheme-001",
                "TestScheme",
                i % 2 == 0,
                0.8 + i * 0.03,
                QString("/path/to/image%1.png").arg(i)
            );
        }
        
        int count = ResultDatabase::instance()->getResultCount();
        REQUIRE(count == 5);
        
        ResultDatabase::instance()->close();
    }
    
    SECTION("Query results by scheme ID") {
        ResultDatabase::instance()->open(testDbPath);
        
        ResultDatabase::instance()->insertResult("scheme-001", "Scheme1", true, 0.9, "img1.png");
        ResultDatabase::instance()->insertResult("scheme-002", "Scheme2", false, 0.5, "img2.png");
        ResultDatabase::instance()->insertResult("scheme-001", "Scheme1", true, 0.85, "img3.png");
        
        QList<QMap<QString, QVariant>> results = ResultDatabase::instance()->queryResults("scheme-001");
        REQUIRE(results.size() == 2);
        
        ResultDatabase::instance()->close();
    }
    
    SECTION("Safe delete with confirmation") {
        ResultDatabase::instance()->open(testDbPath);
        
        for (int i = 0; i < 15; ++i) {
            ResultDatabase::instance()->insertResult("scheme-001", "Test", true, 0.9, "img.png");
        }
        
        bool deleteResult = ResultDatabase::instance()->deleteResults("", true);
        REQUIRE(deleteResult == false);
        
        ResultDatabase::instance()->close();
    }
    
    SECTION("Delete specific scheme results") {
        ResultDatabase::instance()->open(testDbPath);
        
        ResultDatabase::instance()->insertResult("scheme-001", "Scheme1", true, 0.9, "img1.png");
        ResultDatabase::instance()->insertResult("scheme-002", "Scheme2", false, 0.5, "img2.png");
        
        bool deleteResult = ResultDatabase::instance()->deleteResults("scheme-001", false);
        REQUIRE(deleteResult == true);
        
        int count = ResultDatabase::instance()->getResultCount();
        REQUIRE(count == 1);
        
        ResultDatabase::instance()->close();
    }
    
    SECTION("Singleton instance") {
        ResultDatabase* instance1 = ResultDatabase::instance();
        ResultDatabase* instance2 = ResultDatabase::instance();
        REQUIRE(instance1 == instance2);
    }
    
    QFile::remove(testDbPath);
}