#include <catch2/catch_all.hpp>
#include "SchemeManager.h"
#include "Scheme.h"
#include "ResultDatabase.h"
#include "ToolChainExecutor.h"
#include "AuthService.h"
#include "Logger.h"
#include "TemplateMatchTool.h"
#include "EdgeDetectTool.h"
#include "ThresholdTool.h"
#include <QFile>
#include <QDir>

TEST_CASE("System: Complete Inspection Workflow", "[System]") {
    Logger::initialize();
    
    QString testDbPath = "./data/system_test.db";
    QDir dir("./data");
    if (!dir.exists()) dir.mkpath(".");
    QFile::remove(testDbPath);
    
    SECTION("Full inspection cycle") {
        bool loginResult = AuthService::instance()->login("admin", "admin123");
        REQUIRE(loginResult == true);
        
        Scheme* inspectionScheme = new Scheme("PCB_Inspection");
        inspectionScheme->setVersion("1.0.0");
        
        CameraConfig* cameraConfig = new CameraConfig();
        cameraConfig->ip = "192.168.1.10";
        cameraConfig->exposure = 15000;
        cameraConfig->gain = 1.5;
        inspectionScheme->setCameraConfig(cameraConfig);
        
        TriggerConfig* triggerConfig = new TriggerConfig();
        triggerConfig->mode = "external";
        inspectionScheme->setTriggerConfig(triggerConfig);
        
        OutputConfig* outputConfig = new OutputConfig();
        outputConfig->type = "tcp";
        outputConfig->address = "192.168.1.100";
        outputConfig->port = 502;
        inspectionScheme->setOutputConfig(outputConfig);
        
        bool saveResult = SchemeManager::instance()->saveScheme(inspectionScheme);
        REQUIRE(saveResult == true);
        
        Scheme* loadedScheme = SchemeManager::instance()->loadScheme(inspectionScheme->id());
        REQUIRE(loadedScheme != nullptr);
        REQUIRE(loadedScheme->name() == "PCB_Inspection");
        REQUIRE(loadedScheme->version() == "1.0.0");
        
        ResultDatabase::instance()->open(testDbPath);
        
        for (int i = 0; i < 100; ++i) {
            bool isPass = (i % 7 != 0);
            double score = isPass ? (0.85 + (rand() % 10) * 0.015) : (0.4 + (rand() % 20) * 0.02);
            
            ResultDatabase::instance()->insertResult(
                loadedScheme->id(),
                loadedScheme->name(),
                isPass,
                score,
                QString("/pcb_images/board_%1.png").arg(i + 1)
            );
        }
        
        int totalCount = ResultDatabase::instance()->getResultCount();
        REQUIRE(totalCount == 100);
        
        QList<QMap<QString, QVariant>> allResults = ResultDatabase::instance()->queryResults();
        REQUIRE(allResults.size() == 100);
        
        int failCount = 0;
        for (const auto& result : allResults) {
            if (!result["ok"].toBool()) {
                failCount++;
            }
        }
        REQUIRE(failCount > 0);
        
        ToolChainExecutor executor;
        QList<VisionTool*> tools;
        
        ThresholdTool* thresholdTool = new ThresholdTool();
        thresholdTool->setId("threshold-001");
        QJsonObject thresholdParams;
        thresholdParams["threshold"] = 127;
        thresholdParams["maxValue"] = 255;
        thresholdParams["type"] = "BINARY";
        thresholdTool->configure(thresholdParams);
        tools.append(thresholdTool);
        
        EdgeDetectTool* edgeTool = new EdgeDetectTool();
        edgeTool->setId("edge-001");
        QJsonObject edgeParams;
        edgeParams["lowThreshold"] = 50;
        edgeParams["highThreshold"] = 150;
        edgeTool->configure(edgeParams);
        tools.append(edgeTool);
        
        TemplateMatchTool* templateTool = new TemplateMatchTool();
        templateTool->setId("template-001");
        QJsonObject templateParams;
        templateParams["threshold"] = 0.8;
        templateParams["method"] = "CCOEFF_NORMED";
        templateTool->configure(templateParams);
        tools.append(templateTool);
        
        executor.setTools(tools);
        
        cv::Mat testImage(800, 600, CV_8UC3, cv::Scalar(100, 100, 100));
        cv::rectangle(testImage, cv::Rect(100, 100, 50, 50), cv::Scalar(255, 0, 0), 2);
        
        bool execResult = executor.execute(testImage);
        REQUIRE(execResult == true);
        
        bool deleteResult = ResultDatabase::instance()->deleteResults(loadedScheme->id(), false);
        REQUIRE(deleteResult == true);
        
        int remainingCount = ResultDatabase::instance()->getResultCount();
        REQUIRE(remainingCount == 0);
        
        AuthService::instance()->logout();
        
        ResultDatabase::instance()->close();
        QFile::remove(testDbPath);
        delete loadedScheme;
        delete inspectionScheme;
        delete thresholdTool;
        delete edgeTool;
        delete templateTool;
    }
}

TEST_CASE("System: Multi-Scheme Management", "[System]") {
    Logger::initialize();
    
    QString testDbPath = "./data/multi_scheme_test.db";
    QFile::remove(testDbPath);
    
    SECTION("Manage multiple inspection schemes") {
        AuthService::instance()->login("admin", "admin123");
        
        QStringList schemeNames = {"PCB_Line1", "PCB_Line2", "FPC_Inspection", "Solder_Joint"};
        
        for (const QString& name : schemeNames) {
            Scheme* scheme = new Scheme(name);
            scheme->setVersion("1.0");
            SchemeManager::instance()->saveScheme(scheme);
            delete scheme;
        }
        
        QList<Scheme*> schemes = SchemeManager::instance()->getAllSchemes();
        REQUIRE(schemes.size() == schemeNames.size());
        
        for (Scheme* scheme : schemes) {
            REQUIRE(schemeNames.contains(scheme->name()));
        }
        
        ResultDatabase::instance()->open(testDbPath);
        
        for (Scheme* scheme : schemes) {
            for (int i = 0; i < 20; ++i) {
                ResultDatabase::instance()->insertResult(
                    scheme->id(),
                    scheme->name(),
                    (i % 5 != 0),
                    0.7 + (rand() % 30) * 0.01,
                    QString("/images/%1_%2.png").arg(scheme->name()).arg(i)
                );
            }
        }
        
        int totalResults = ResultDatabase::instance()->getResultCount();
        REQUIRE(totalResults == schemeNames.size() * 20);
        
        for (const QString& name : schemeNames) {
            Scheme* scheme = SchemeManager::instance()->getSchemeByName(name);
            REQUIRE(scheme != nullptr);
            
            QList<QMap<QString, QVariant>> results = ResultDatabase::instance()->queryResults(scheme->id());
            REQUIRE(results.size() == 20);
        }
        
        bool deleteAllResult = ResultDatabase::instance()->deleteResults("", true);
        REQUIRE(deleteAllResult == false);
        
        AuthService::instance()->logout();
        
        ResultDatabase::instance()->close();
        QFile::remove(testDbPath);
    }
}

TEST_CASE("System: Security and Session Management", "[System]") {
    Logger::initialize();
    
    SECTION("Session lifecycle") {
        REQUIRE(AuthService::instance()->isAuthenticated() == false);
        
        bool loginResult = AuthService::instance()->login("admin", "admin123");
        REQUIRE(loginResult == true);
        REQUIRE(AuthService::instance()->isAuthenticated() == true);
        REQUIRE(AuthService::instance()->currentUser() == "admin");
        
        Scheme* scheme = new Scheme("SessionTest");
        bool saveResult = SchemeManager::instance()->saveScheme(scheme);
        REQUIRE(saveResult == true);
        delete scheme;
        
        AuthService::instance()->logout();
        REQUIRE(AuthService::instance()->isAuthenticated() == false);
        REQUIRE(AuthService::instance()->currentUser().isEmpty());
        
        bool failedLogin = AuthService::instance()->login("admin", "wrongpassword");
        REQUIRE(failedLogin == false);
        REQUIRE(AuthService::instance()->isAuthenticated() == false);
    }
    
    SECTION("Concurrent access protection") {
        AuthService::instance()->login("admin", "admin123");
        
        SchemeManager* instance1 = SchemeManager::instance();
        SchemeManager* instance2 = SchemeManager::instance();
        REQUIRE(instance1 == instance2);
        
        ResultDatabase* db1 = ResultDatabase::instance();
        ResultDatabase* db2 = ResultDatabase::instance();
        REQUIRE(db1 == db2);
        
        AuthService::instance()->logout();
    }
}