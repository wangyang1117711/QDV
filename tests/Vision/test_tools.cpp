#include "../catch2/catch2_minimal.hpp"
#include "Core/VisionTool.h"
#include "Vision/ToolFactory.h"
#include "Vision/ToolChainExecutor.h"
#include <opencv2/core/mat.hpp>
#include <QList>

using namespace QDV;

TEST_CASE("ToolFactory singleton", "[tools]") {
    ToolFactory* factory = ToolFactory::instance();
    REQUIRE(factory != nullptr);
    ToolFactory* factory2 = ToolFactory::instance();
    REQUIRE(factory == factory2);
}

TEST_CASE("ToolFactory creates all 13 tool types", "[tools]") {
    ToolFactory* factory = ToolFactory::instance();
    REQUIRE(factory != nullptr);

    const char* types[] = {
        "TemplateMatch", "EdgeDetect", "BlobDetect", "ColorDetect",
        "Threshold", "ImagePreprocess", "ContourAnalyze", "BranchControl",
        "GeometryMeasure", "LineCircleDetect", "ImageArithmetic",
        "ImageTransform", "ImageMerge"
    };

    for (const char* type : types) {
        VisionTool* tool = factory->createTool(QString::fromLatin1(type));
        CHECK(tool != nullptr);
        if (tool) {
            REQUIRE_FALSE(tool->id().isEmpty());
            delete tool;
        }
    }
}

TEST_CASE("ToolFactory invalid type returns nullptr", "[tools]") {
    ToolFactory* factory = ToolFactory::instance();
    VisionTool* tool = factory->createTool("NonExistentTool");
    REQUIRE(tool == nullptr);
}

TEST_CASE("ToolChainExecutor construction and empty execute", "[tools]") {
    ToolChainExecutor executor;
    QList<VisionTool*> emptyList;
    executor.setTools(emptyList);

    cv::Mat emptyMat;
    REQUIRE_FALSE(executor.execute(emptyMat));
}

TEST_CASE("ToolChainExecutor execute with single tool", "[tools]") {
    ToolChainExecutor executor;
    VisionTool* tool = ToolFactory::instance()->createTool("EdgeDetect");
    REQUIRE(tool != nullptr);

    QList<VisionTool*> tools;
    tools.append(tool);
    executor.setTools(tools);

    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(0, 0, 0));
    bool result = executor.execute(testMat);
    CHECK(result);

    ToolResult toolResult = executor.getResult(tool->id());
    CHECK(toolResult.elapsedMs >= 0);
    delete tool;
}

TEST_CASE("ToolChainExecutor stop without crash", "[tools]") {
    ToolChainExecutor executor;
    executor.stop();
    SUCCEED("stop() called without crash");
}

TEST_CASE("ToolChainExecutor execute with multiple tools", "[tools]") {
    ToolChainExecutor executor;
    VisionTool* t1 = ToolFactory::instance()->createTool("EdgeDetect");
    VisionTool* t2 = ToolFactory::instance()->createTool("Threshold");
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
    ToolResult r2 = executor.getResult(t2->id());
    CHECK(r1.elapsedMs >= 0);
    CHECK(r2.elapsedMs >= 0);
    delete t1;
    delete t2;
}