#include "../catch2/catch2_minimal.hpp"
#include "Vision/ToolChainExecutor.h"
#include "Vision/ToolFactory.h"
#include "Core/VisionTool.h"
#include "Core/BranchNode.h"
#include <opencv2/core/mat.hpp>
#include <QList>
#include <QApplication>
#include <QTimer>
#include <QFuture>
#include <QtConcurrent>

using namespace QDV;

static int s_argc = 0;
static QApplication s_app(s_argc, nullptr);

TEST_CASE("ToolChainExecutor execute with all tool types", "[toolchain]") {
    ToolChainExecutor executor;
    const char* types[] = {
        "TemplateMatch", "EdgeDetect", "BlobDetect", "ColorDetect",
        "Threshold", "ImagePreprocess", "ContourAnalyze", "BranchControl",
        "GeometryMeasure", "LineCircleDetect", "ImageArithmetic",
        "ImageTransform", "ImageMerge"
    };
    QList<VisionTool*> tools;
    for (const char* t : types) {
        VisionTool* tool = ToolFactory::instance()->createTool(QString::fromLatin1(t));
        REQUIRE(tool != nullptr);
        tools.append(tool);
    }
    executor.setTools(tools);
    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(64, 64, 64));
    bool result = executor.execute(testMat);
    CHECK(result);
    for (VisionTool* t : tools) {
        ToolResult r = executor.getResult(t->id());
        CHECK(r.elapsedMs >= 0);
        delete t;
    }
}

TEST_CASE("ToolChainExecutor execute with branch tools", "[toolchain]") {
    ToolChainExecutor executor;
    VisionTool* t1 = ToolFactory::instance()->createTool("BlobDetect");
    VisionTool* t2 = ToolFactory::instance()->createTool("BranchControl");
    REQUIRE(t1 != nullptr);
    REQUIRE(t2 != nullptr);
    QList<VisionTool*> tools;
    tools.append(t1);
    tools.append(t2);
    executor.setTools(tools);
    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
    bool result = executor.execute(testMat);
    CHECK(result);
    ToolResult r1 = executor.getResult(t1->id());
    CHECK(r1.elapsedMs >= 0);
    delete t1;
    delete t2;
}

TEST_CASE("ToolChainExecutor stop mid-execution", "[toolchain]") {
    ToolChainExecutor executor;
    QList<VisionTool*> tools;
    for (int i = 0; i < 20; ++i) {
        VisionTool* t = ToolFactory::instance()->createTool("EdgeDetect");
        REQUIRE(t != nullptr);
        tools.append(t);
    }
    executor.setTools(tools);
    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(0, 0, 0));
    QFuture<bool> future = QtConcurrent::run([&executor, &testMat]() {
        return executor.execute(testMat);
    });
    QTimer::singleShot(10, [&executor]() { executor.stop(); });
    future.waitForFinished();
    SUCCEED("stop did not crash");
    for (VisionTool* t : tools) delete t;
}

TEST_CASE("ToolChainExecutor executeAsync with QFuture", "[toolchain]") {
    ToolChainExecutor executor;
    VisionTool* t = ToolFactory::instance()->createTool("BlobDetect");
    REQUIRE(t != nullptr);
    QList<VisionTool*> tools;
    tools.append(t);
    executor.setTools(tools);
    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
    QFuture<bool> future = executor.executeAsync(testMat);
    future.waitForFinished();
    CHECK(future.result());
    delete t;
}

TEST_CASE("ToolChainExecutor setTools with empty list", "[toolchain]") {
    ToolChainExecutor executor;
    QList<VisionTool*> emptyList;
    executor.setTools(emptyList);
    cv::Mat emptyMat;
    REQUIRE_FALSE(executor.execute(emptyMat));
}

TEST_CASE("ToolChainExecutor execute with empty cv::Mat", "[toolchain]") {
    ToolChainExecutor executor;
    VisionTool* t = ToolFactory::instance()->createTool("EdgeDetect");
    REQUIRE(t != nullptr);
    QList<VisionTool*> tools;
    tools.append(t);
    executor.setTools(tools);
    cv::Mat emptyMat;
    REQUIRE_FALSE(executor.execute(emptyMat));
    delete t;
}

TEST_CASE("ToolChainExecutor setTools replaces previous tools", "[toolchain]") {
    ToolChainExecutor executor;
    VisionTool* t1 = ToolFactory::instance()->createTool("EdgeDetect");
    VisionTool* t2 = ToolFactory::instance()->createTool("BlobDetect");
    VisionTool* t3 = ToolFactory::instance()->createTool("Threshold");
    REQUIRE(t1 != nullptr);
    REQUIRE(t2 != nullptr);
    REQUIRE(t3 != nullptr);
    QList<VisionTool*> tools1;
    tools1.append(t1);
    executor.setTools(tools1);
    QList<VisionTool*> tools2;
    tools2.append(t2);
    tools2.append(t3);
    executor.setTools(tools2);
    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(64, 64, 64));
    executor.execute(testMat);
    ToolResult r2 = executor.getResult(t2->id());
    ToolResult r3 = executor.getResult(t3->id());
    CHECK(r2.elapsedMs >= 0);
    CHECK(r3.elapsedMs >= 0);
    delete t1;
    delete t2;
    delete t3;
}

TEST_CASE("ToolChainExecutor executionProgress signal", "[toolchain]") {
    ToolChainExecutor executor;
    VisionTool* t = ToolFactory::instance()->createTool("BlobDetect");
    REQUIRE(t != nullptr);
    QList<VisionTool*> tools;
    tools.append(t);
    executor.setTools(tools);
    int progressCount = 0;
    int lastCurrent = -1;
    QObject::connect(&executor, &ToolChainExecutor::executionProgress,
        [&](int current, int total) {
            progressCount++;
            lastCurrent = current;
            CHECK(total >= 1);
        });
    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(0, 0, 0));
    executor.execute(testMat);
    CHECK(progressCount >= 1);
    CHECK(lastCurrent >= 0);
    delete t;
}

TEST_CASE("ToolChainExecutor chainCompleted signal", "[toolchain]") {
    ToolChainExecutor executor;
    VisionTool* t = ToolFactory::instance()->createTool("EdgeDetect");
    REQUIRE(t != nullptr);
    QList<VisionTool*> tools;
    tools.append(t);
    executor.setTools(tools);
    bool completed = false;
    bool success = false;
    QObject::connect(&executor, &ToolChainExecutor::chainCompleted,
        [&](bool ok) {
            completed = true;
            success = ok;
        });
    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
    executor.execute(testMat);
    CHECK(completed);
    CHECK(success);
    delete t;
}

TEST_CASE("ToolChainExecutor toolExecuted signal per-tool", "[toolchain]") {
    ToolChainExecutor executor;
    VisionTool* t1 = ToolFactory::instance()->createTool("EdgeDetect");
    VisionTool* t2 = ToolFactory::instance()->createTool("Threshold");
    REQUIRE(t1 != nullptr);
    REQUIRE(t2 != nullptr);
    QList<VisionTool*> tools;
    tools.append(t1);
    tools.append(t2);
    executor.setTools(tools);
    int signalCount = 0;
    QObject::connect(&executor, &ToolChainExecutor::toolExecuted,
        [&](const QString&, const ToolResult&) {
            signalCount++;
        });
    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(255, 255, 255));
    executor.execute(testMat);
    REQUIRE_EQUAL(signalCount, 2);
    delete t1;
    delete t2;
}