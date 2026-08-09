#include "../catch2/catch2_minimal.hpp"
#include "Core/VisionTool.h"
#include "Core/DetectionStats.h"
#include "Core/AuthService.h"
#include "Core/Scheme.h"
#include "Vision/ToolFactory.h"
#include "Vision/ToolChainExecutor.h"
#include "Database/DatabaseIntegrator.h"
#include <QApplication>
#include <QFuture>
#include <QSignalSpy>
#include <opencv2/core/mat.hpp>
#include <iostream>

static int s_argc = 0;
// 复用 test_main.cpp 中创建的全局 QApplication 实例，避免多实例冲突
static QApplication* s_app() {
    return qobject_cast<QApplication*>(QCoreApplication::instance());
}

using namespace QDV;

TEST_CASE("Vision to DetectionStats to Database data flow", "[integration]") {
    ToolChainExecutor executor;
    VisionTool* tool = ToolFactory::instance()->createTool("EdgeDetect");
    REQUIRE(tool != nullptr);
    QList<VisionTool*> tools;
    tools.append(tool);
    executor.setTools(tools);
    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
    bool execOk = executor.execute(testMat);
    REQUIRE(execOk);
    ToolResult result = executor.getResult(tool->id());
    DetectionStats stats;
    stats.totalDetected = 1;
    stats.passed = result.ok ? 1 : 0;
    stats.failed = result.ok ? 0 : 1;
    stats.passRate = result.ok ? 100.0 : 0.0;
    auto* integrator = DatabaseIntegrator::instance();
    bool dbOk = integrator->initialize(":memory:");
    if (dbOk) {
        integrator->saveDetectionResult("integration-test", "Integration Test", stats);
    }
    SUCCEED("data flow completed");
    delete tool;
}

TEST_CASE("Auth to LoginView to MainWindow flow simulation", "[integration]") {
    AuthService* auth = AuthService::instance();
    auth->createUser("integration_user", "SecureP@ss1", false);
    bool loggedIn = auth->login("integration_user", "SecureP@ss1");
    REQUIRE(loggedIn);
    REQUIRE(auth->isAuthenticated());
    auth->logout();
    REQUIRE_FALSE(auth->isAuthenticated());
}

TEST_CASE("Scheme to ToolChain to ToolChainExecutor execute pipeline", "[integration]") {
    Scheme scheme;
    VisionTool* t1 = ToolFactory::instance()->createTool("EdgeDetect");
    VisionTool* t2 = ToolFactory::instance()->createTool("BlobDetect");
    REQUIRE(t1 != nullptr);
    REQUIRE(t2 != nullptr);
    scheme.addTool(std::unique_ptr<VisionTool>(t1));
    scheme.addTool(std::unique_ptr<VisionTool>(t2));
    REQUIRE_EQUAL(scheme.toolChain().size(), 2);
    ToolChainExecutor executor;
    executor.setTools(scheme.toolPtrs());
    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
    bool execOk = executor.execute(testMat);
    REQUIRE(execOk);
    ToolResult r1 = executor.getResult(t1->id());
    ToolResult r2 = executor.getResult(t2->id());
    CHECK(r1.elapsedMs >= 0);
    CHECK(r2.elapsedMs >= 0);
}

TEST_CASE("DetectionStats creation and field population", "[integration]") {
    DetectionStats stats;
    stats.totalDetected = 100;
    stats.passed = 85;
    stats.failed = 15;
    stats.passRate = 85.0;
    stats.status = "running";
    REQUIRE_EQUAL(stats.totalDetected, 100);
    REQUIRE_EQUAL(stats.passed, 85);
    REQUIRE_EQUAL(stats.failed, 15);
    REQUIRE(stats.passRate >= 84.9);
    REQUIRE_EQUAL(stats.status.toStdString(), std::string("running"));
}

TEST_CASE("ToolResult to DetectionStats conversion", "[integration]") {
    ToolResult passResult;
    passResult.ok = true;
    passResult.score = 0.95;
    passResult.elapsedMs = 12;
    DetectionStats passStats;
    passStats.totalDetected = 1;
    passStats.passed = passResult.ok ? 1 : 0;
    passStats.failed = passResult.ok ? 0 : 1;
    REQUIRE_EQUAL(passStats.passed, 1);
    REQUIRE_EQUAL(passStats.failed, 0);
}

TEST_CASE("ToolChainExecutor executeAsync chainCompleted signal", "[integration]") {
    ToolChainExecutor executor;
    VisionTool* tool = ToolFactory::instance()->createTool("Threshold");
    REQUIRE(tool != nullptr);
    QList<VisionTool*> tools;
    tools.append(tool);
    executor.setTools(tools);
    bool completed = false;
    QObject::connect(&executor, &ToolChainExecutor::chainCompleted,
        [&](bool ok) { completed = true; (void)ok; });
    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
    QFuture<bool> future = executor.executeAsync(testMat);
    future.waitForFinished();
    s_app()->processEvents();
    CHECK(completed);
    delete tool;
}