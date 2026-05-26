#include <catch2/catch_all.hpp>
#include "Scheme.h"
#include "CameraConfig.h"
#include "TriggerConfig.h"
#include "OutputConfig.h"
#include "Logger.h"

TEST_CASE("Scheme JSON Validation", "[Scheme]") {
    Logger::initialize();
    
    SECTION("Deserialize with valid JSON") {
        QJsonObject obj;
        obj["id"] = "scheme-001";
        obj["name"] = "TestScheme";
        obj["version"] = "1.0";
        
        QJsonObject cameraObj;
        cameraObj["ip"] = "192.168.1.100";
        cameraObj["exposure"] = 10000;
        cameraObj["gain"] = 1.0;
        obj["camera"] = cameraObj;
        
        Scheme scheme;
        bool result = scheme.deserialize(obj);
        REQUIRE(result == true);
        REQUIRE(scheme.id() == "scheme-001");
        REQUIRE(scheme.name() == "TestScheme");
    }
    
    SECTION("Deserialize missing required field") {
        QJsonObject obj;
        obj["id"] = "scheme-001";
        obj["version"] = "1.0";
        
        Scheme scheme;
        bool result = scheme.deserialize(obj);
        REQUIRE(result == false);
    }
    
    SECTION("Deserialize invalid field type") {
        QJsonObject obj;
        obj["id"] = "scheme-001";
        obj["name"] = 123;
        obj["version"] = "1.0";
        
        Scheme scheme;
        bool result = scheme.deserialize(obj);
        REQUIRE(result == false);
    }
    
    SECTION("Deserialize empty ID") {
        QJsonObject obj;
        obj["id"] = "";
        obj["name"] = "TestScheme";
        obj["version"] = "1.0";
        
        Scheme scheme;
        bool result = scheme.deserialize(obj);
        REQUIRE(result == false);
    }
    
    SECTION("Deserialize invalid camera IP") {
        QJsonObject obj;
        obj["id"] = "scheme-001";
        obj["name"] = "TestScheme";
        obj["version"] = "1.0";
        
        QJsonObject cameraObj;
        cameraObj["ip"] = "invalid-ip";
        cameraObj["exposure"] = 10000;
        obj["camera"] = cameraObj;
        
        Scheme scheme;
        bool result = scheme.deserialize(obj);
        REQUIRE(result == false);
    }
    
    SECTION("Deserialize negative exposure") {
        QJsonObject obj;
        obj["id"] = "scheme-001";
        obj["name"] = "TestScheme";
        obj["version"] = "1.0";
        
        QJsonObject cameraObj;
        cameraObj["ip"] = "192.168.1.100";
        cameraObj["exposure"] = -100;
        obj["camera"] = cameraObj;
        
        Scheme scheme;
        bool result = scheme.deserialize(obj);
        REQUIRE(result == false);
    }
    
    SECTION("Serialize and deserialize roundtrip") {
        Scheme original("OriginalScheme");
        original.setVersion("2.0");
        
        CameraConfig* camera = new CameraConfig();
        camera->ip = "10.0.0.1";
        camera->exposure = 5000;
        camera->gain = 2.0;
        original.setCameraConfig(camera);
        
        TriggerConfig* trigger = new TriggerConfig();
        trigger->mode = "continuous";
        original.setTriggerConfig(trigger);
        
        QJsonObject serialized = original.serialize();
        
        Scheme deserialized;
        bool result = deserialized.deserialize(serialized);
        
        REQUIRE(result == true);
        REQUIRE(deserialized.name() == "OriginalScheme");
        REQUIRE(deserialized.version() == "2.0");
        REQUIRE(deserialized.cameraConfig()->ip == "10.0.0.1");
    }
    
    SECTION("Validate version format") {
        Scheme scheme("TestScheme");
        
        scheme.setVersion("1.0.0");
        REQUIRE(scheme.version() == "1.0.0");
        
        bool valid = scheme.validateVersion("1.0");
        REQUIRE(valid == true);
        
        valid = scheme.validateVersion("v1.0");
        REQUIRE(valid == false);
        
        valid = scheme.validateVersion("1");
        REQUIRE(valid == false);
    }
}