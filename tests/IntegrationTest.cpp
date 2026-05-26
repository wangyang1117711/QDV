#include <catch2/catch_all.hpp>
#include "SchemeManager.h"
#include "Scheme.h"
#include "ResultDatabase.h"
#include "ToolChainExecutor.h"
#include "TemplateMatchTool.h"
#include "EdgeDetectTool.h"
#include "AuthService.h"
#include "Logger.h"
#include <QFile>

TEST_CASE("Integration: SchemeManager and Database", "[Integration]") {
    Logger::initialize();
    
    QString testDbPath = "./data/integration_test.db";
    QFile::remove(testDbPath);
    
    SECTION("Save scheme to database") {
        SchemeManager* manager = SchemeManager::instance();
        ResultDatabase::instance()->open(testDbPath);
        
        Scheme scheme("IntegrationTest");
        scheme.setVersion("1.0");
        
        bool saveResult = manager->saveScheme(&scheme);
        REQUIRE(saveResult == true);
        
        bool insertResult = ResultDatabase::instance()->insertResult(
            scheme.id(),
            scheme.name(),
            true,
            0.95,
            "/path/to/image.png"
        );
        REQUIRE(insertResult == true);
        
        QList<QMap<QString, QVariant>> results = ResultDatabase::instance()->queryResults(scheme.id());
        REQUIRE(results.size() == 1);
        
        ResultDatabase::instance()->close();
        QFile::remove(testDbPath);
    }
}

TEST_CASE("Integration: AuthService and SchemeManager", "[Integration]") {
    Logger::initialize();
    
    SECTION("Access scheme manager after authentication") {
        bool loginResult = AuthService::instance()->login("admin", "admin123");
        REQUIRE(loginResult == true);
        
        SchemeManager* manager = SchemeManager::instance();
        REQUIRE(manager != nullptr);
        
        AuthService::instance()->logout();
    }
    
    SECTION("Scheme operations after logout") {
        AuthService::instance()->login("admin", "admin123");
        
        Scheme scheme("TestScheme");
        SchemeManager::instance()->saveScheme(&scheme);
        
        AuthService::instance()->logout();
        
        QList<Scheme*> schemes = SchemeManager::instance()->getAllSchemes();
        REQUIRE(!schemes.isEmpty());
    }
}

TEST_CASE("Integration: ToolChain and VisionTools", "[Integration]") {
    Logger::initialize();
    
    SECTION("Execute tool chain with multiple tools") {
        ToolChainExecutor executor;
        QList<VisionTool*> tools;
        
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
        
        cv::Mat input(200, 200, CV_8UC3, cv::Scalar(100, 100, 100));
        bool result = executor.execute(input);
        
        REQUIRE(result == true);
        
        delete edgeTool;
        delete templateTool;
    }
    
    SECTION("Tool chain with empty tools") {
        ToolChainExecutor executor;
        cv::Mat input(100, 100, CV_8UC3);
        
        bool result = executor.execute(input);
        REQUIRE(result == true);
    }
}

TEST_CASE("Integration: Full Workflow", "[Integration]") {
    Logger::initialize();
    
    QString testDbPath = "./data/full_workflow.db";
    QFile::remove(testDbPath);
    
    SECTION("End-to-end workflow") {
        bool loginResult = AuthService::instance()->login("admin", "admin123");
        REQUIRE(loginResult == true);
        
        Scheme* scheme = new Scheme("ProductionScheme");
        scheme->setVersion("2.0");
        
        CameraConfig* cameraConfig = new CameraConfig();
        cameraConfig->ip = "192.168.1.200";
        cameraConfig->exposure = 20000;
        scheme->setCameraConfig(cameraConfig);
        
        bool saveResult = SchemeManager::instance()->saveScheme(scheme);
        REQUIRE(saveResult == true);
        
        ResultDatabase::instance()->open(testDbPath);
        
        for (int i = 0; i < 10; ++i) {
            bool pass = i % 3 != 0;
            ResultDatabase::instance()->insertResult(
                scheme->id(),
                scheme->name(),
                pass,
                0.7 + i * 0.03,
                QString("/images/img%1.png").arg(i)
            );
        }
        
        int count = ResultDatabase::instance()->getResultCount(scheme->id());
        REQUIRE(count == 10);
        
        QList<QMap<QString, QVariant>> results = ResultDatabase::instance()->queryResults(scheme->id());
        REQUIRE(results.size() == 10);
        
        ToolChainExecutor executor;
        cv::Mat testImage(500, 500, CV_8UC3, cv::Scalar(128, 128, 128));
        bool execResult = executor.execute(testImage);
        REQUIRE(execResult == true);
        
        AuthService::instance()->logout();
        
        ResultDatabase::instance()->close();
        QFile::remove(testDbPath);
        delete scheme;
    }
}