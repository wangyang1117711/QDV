#include <catch2/catch_all.hpp>
#include "Scheme.h"
#include "SchemeManager.h"
#include "Logger.h"

TEST_CASE("Scheme Creation", "[Scheme]") {
    Logger::initialize();
    
    SECTION("Create scheme with name") {
        Scheme scheme("TestScheme");
        REQUIRE(scheme.name() == "TestScheme");
        REQUIRE(!scheme.id().isEmpty());
    }
    
    SECTION("Serialize and deserialize scheme") {
        Scheme scheme("TestScheme");
        CameraConfig config;
        config.ip = "192.168.1.100";
        config.exposure = 10000;
        scheme.setCameraConfig(new CameraConfig(config));
        
        QJsonObject obj = scheme.serialize();
        REQUIRE(obj["name"].toString() == "TestScheme");
        REQUIRE(obj["camera"].toObject()["ip"].toString() == "192.168.1.100");
        
        Scheme loadedScheme;
        REQUIRE(loadedScheme.deserialize(obj));
        REQUIRE(loadedScheme.name() == "TestScheme");
    }
}

TEST_CASE("Scheme Manager", "[SchemeManager]") {
    SECTION("Instance is singleton") {
        SchemeManager* instance1 = SchemeManager::instance();
        SchemeManager* instance2 = SchemeManager::instance();
        REQUIRE(instance1 == instance2);
    }
}