#include <catch2/catch_all.hpp>
#include "BranchControlTool.h"
#include "GeometryMeasureTool.h"
#include "LineCircleDetectTool.h"
#include "ImageArithmeticTool.h"
#include "ImageTransformTool.h"
#include "ImageMergeTool.h"
#include "BranchNode.h"
#include "VisionTool.h"
#include <opencv2/opencv.hpp>

TEST_CASE("BranchNode Condition Evaluation", "[BranchNode]") {
    SECTION("OK condition evaluates to true") {
        BranchNode node;
        node.id = "branch-001";
        node.sourceToolId = "tool-001";
        node.conditionOp = "ok";
        node.trueBranchToolIds = {"tool-next"};

        REQUIRE(node.isValid());

        ToolResult result;
        result.ok = true;
        REQUIRE(node.evaluate(result) == true);
    }

    SECTION("NG condition evaluates to true when not ok") {
        BranchNode node;
        node.id = "branch-001";
        node.sourceToolId = "tool-001";
        node.conditionOp = "ng";
        node.falseBranchToolIds = {"tool-fail"};

        ToolResult result;
        result.ok = false;
        REQUIRE(node.evaluate(result) == true);
    }

    SECTION("Score comparison > operator") {
        BranchNode node;
        node.id = "branch-001";
        node.sourceToolId = "tool-001";
        node.conditionOp = ">";
        node.conditionValue = 0.8;
        node.trueBranchToolIds = {"tool-good"};

        REQUIRE(node.isValid());

        ToolResult result;
        result.score = 0.9;
        REQUIRE(node.evaluate(result) == true);

        result.score = 0.7;
        REQUIRE(node.evaluate(result) == false);
    }

    SECTION("Score comparison >= operator") {
        BranchNode node;
        node.id = "branch-001";
        node.sourceToolId = "tool-001";
        node.conditionOp = ">=";
        node.conditionValue = 0.8;
        node.trueBranchToolIds = {"tool-good"};

        ToolResult result;
        result.score = 0.8;
        REQUIRE(node.evaluate(result) == true);
    }

    SECTION("Score comparison < operator") {
        BranchNode node;
        node.id = "branch-001";
        node.sourceToolId = "tool-001";
        node.conditionOp = "<";
        node.conditionValue = 0.5;
        node.trueBranchToolIds = {"tool-bad"};

        ToolResult result;
        result.score = 0.3;
        REQUIRE(node.evaluate(result) == true);
    }

    SECTION("Equality check with boolean") {
        BranchNode node;
        node.id = "branch-001";
        node.sourceToolId = "tool-001";
        node.conditionOp = "==";
        node.conditionValue = true;
        node.trueBranchToolIds = {"tool-next"};

        ToolResult result;
        result.ok = true;
        REQUIRE(node.evaluate(result) == true);
    }

    SECTION("Inequality check with boolean") {
        BranchNode node;
        node.id = "branch-001";
        node.sourceToolId = "tool-001";
        node.conditionOp = "!=";
        node.conditionValue = true;
        node.falseBranchToolIds = {"tool-fail"};

        ToolResult result;
        result.ok = false;
        REQUIRE(node.evaluate(result) == true);
    }

    SECTION("Invalid node (missing id)") {
        BranchNode node;
        node.sourceToolId = "tool-001";
        node.conditionOp = "ok";
        REQUIRE(node.isValid() == false);
    }

    SECTION("Serialization roundtrip") {
        BranchNode node;
        node.id = "branch-001";
        node.sourceToolId = "tool-001";
        node.conditionOp = ">";
        node.conditionValue = 0.8;
        node.trueBranchToolIds = {"tool-a", "tool-b"};
        node.falseBranchToolIds = {"tool-c"};

        QJsonObject data = node.serialize();

        BranchNode restore;
        restore.deserialize(data);

        REQUIRE(restore.id == "branch-001");
        REQUIRE(restore.conditionOp == ">");
        REQUIRE(restore.trueBranchToolIds.size() == 2);
        REQUIRE(restore.falseBranchToolIds.size() == 1);
    }
}

TEST_CASE("BranchControlTool Operations", "[BranchControlTool]") {
    SECTION("Configure with valid branch parameters") {
        BranchControlTool tool;
        QJsonObject params;
        params["id"] = "branch-001";
        params["source"] = "tool-001";
        params["op"] = "ok";
        params["value"] = true;
        QJsonArray trueBranch;
        trueBranch.append("tool-next");
        params["trueBranch"] = trueBranch;

        bool result = tool.configure(params);
        REQUIRE(result == true);
        REQUIRE(tool.type() == "BranchControl");
    }

    SECTION("Configure with invalid parameters") {
        BranchControlTool tool;
        QJsonObject params;
        bool result = tool.configure(params);
        REQUIRE(result == false);
    }

    SECTION("Execute passes through input") {
        BranchControlTool tool;
        QJsonObject params;
        params["id"] = "branch-001";
        params["source"] = "tool-001";
        params["op"] = "ok";
        QJsonArray arr;
        arr.append("tool-next");
        params["trueBranch"] = arr;
        tool.configure(params);

        cv::Mat input(100, 100, CV_8UC3, cv::Scalar(100, 100, 100));
        ToolResult result;
        bool success = tool.execute(input, result);
        REQUIRE(success == true);
        REQUIRE(result.ok == true);
    }

    SECTION("Serialize and deserialize") {
        BranchControlTool tool;
        QJsonObject params;
        params["id"] = "branch-001";
        params["source"] = "tool-001";
        params["op"] = "ok";
        QJsonArray arr;
        arr.append("tool-next");
        params["trueBranch"] = arr;
        tool.configure(params);

        QJsonObject serialized = tool.serialize();
        REQUIRE(serialized["type"] == "BranchControl");
        REQUIRE(serialized["id"] == "branch-001");
    }
}

TEST_CASE("GeometryMeasureTool Operations", "[GeometryMeasureTool]") {
    SECTION("Configure with valid parameters") {
        GeometryMeasureTool tool;
        QJsonObject params;
        params["measureType"] = "distance";
        params["minThreshold"] = 10.0;
        params["maxThreshold"] = 500.0;

        bool result = tool.configure(params);
        REQUIRE(result == true);
        REQUIRE(tool.type() == "GeometryMeasure");
    }

    SECTION("Configure with invalid measure type") {
        GeometryMeasureTool tool;
        QJsonObject params;
        params["measureType"] = "invalid";

        bool result = tool.configure(params);
        REQUIRE(result == false);
    }

    SECTION("Configure with invalid threshold range") {
        GeometryMeasureTool tool;
        QJsonObject params;
        params["measureType"] = "area";
        params["minThreshold"] = 100.0;
        params["maxThreshold"] = 50.0;

        bool result = tool.configure(params);
        REQUIRE(result == false);
    }

    SECTION("Execute distance measurement") {
        GeometryMeasureTool tool;
        QJsonObject params;
        params["measureType"] = "distance";
        params["minThreshold"] = 0.0;
        params["maxThreshold"] = 1000.0;
        tool.configure(params);

        cv::Mat input(200, 200, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::rectangle(input, cv::Rect(50, 50, 100, 100), cv::Scalar(255, 255, 255), -1);

        ToolResult result;
        bool success = tool.execute(input, result);
        REQUIRE(success == true);
        REQUIRE(result.data.contains("distance_px"));
    }

    SECTION("Execute area measurement") {
        GeometryMeasureTool tool;
        QJsonObject params;
        params["measureType"] = "area";
        params["minThreshold"] = 0.0;
        params["maxThreshold"] = 50000.0;
        tool.configure(params);

        cv::Mat input(200, 200, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::rectangle(input, cv::Rect(50, 50, 100, 100), cv::Scalar(255, 255, 255), -1);

        ToolResult result;
        bool success = tool.execute(input, result);
        REQUIRE(success == true);
        REQUIRE(result.data.contains("area_px"));
    }
}

TEST_CASE("LineCircleDetectTool Operations", "[LineCircleDetectTool]") {
    SECTION("Detect lines in image") {
        LineCircleDetectTool tool;
        QJsonObject params;
        params["detectType"] = "line";
        params["threshold"] = 50;

        bool configResult = tool.configure(params);
        REQUIRE(configResult == true);

        cv::Mat input(200, 200, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::line(input, cv::Point(50, 100), cv::Point(150, 100), cv::Scalar(255, 255, 255), 2);

        ToolResult result;
        bool success = tool.execute(input, result);
        REQUIRE(success == true);
        REQUIRE(result.data.contains("count"));
    }

    SECTION("Detect linesP in image") {
        LineCircleDetectTool tool;
        QJsonObject params;
        params["detectType"] = "lineP";
        params["threshold"] = 30;
        params["minLineLength"] = 20.0;
        params["maxLineGap"] = 10.0;

        bool configResult = tool.configure(params);
        REQUIRE(configResult == true);

        cv::Mat input(200, 200, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::line(input, cv::Point(30, 100), cv::Point(170, 100), cv::Scalar(255, 255, 255), 2);

        ToolResult result;
        bool success = tool.execute(input, result);
        REQUIRE(success == true);
    }

    SECTION("Detect circles") {
        LineCircleDetectTool tool;
        QJsonObject params;
        params["detectType"] = "circle";
        params["param1"] = 50.0;
        params["param2"] = 30.0;
        params["minRadius"] = 10.0;
        params["maxRadius"] = 100.0;

        bool configResult = tool.configure(params);
        REQUIRE(configResult == true);

        cv::Mat input(200, 200, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::circle(input, cv::Point(100, 100), 50, cv::Scalar(255, 255, 255), 2);

        ToolResult result;
        bool success = tool.execute(input, result);
        REQUIRE(success == true);
    }
}

TEST_CASE("ImageArithmeticTool Operations", "[ImageArithmeticTool]") {
    SECTION("Add scalar to image") {
        ImageArithmeticTool tool;
        QJsonObject params;
        params["operation"] = "add";
        params["scalar"] = 50.0;

        bool configResult = tool.configure(params);
        REQUIRE(configResult == true);

        cv::Mat input(100, 100, CV_8UC1, cv::Scalar(100));
        ToolResult result;
        bool success = tool.execute(input, result);
        REQUIRE(success == true);
    }

    SECTION("Bitwise NOT operation") {
        ImageArithmeticTool tool;
        QJsonObject params;
        params["operation"] = "not";

        bool configResult = tool.configure(params);
        REQUIRE(configResult == true);

        cv::Mat input(100, 100, CV_8UC1, cv::Scalar(100));
        ToolResult result;
        bool success = tool.execute(input, result);
        REQUIRE(success == true);
    }
}

TEST_CASE("ImageTransformTool Operations", "[ImageTransformTool]") {
    SECTION("Resize image") {
        ImageTransformTool tool;
        QJsonObject params;
        params["transformType"] = "resize";
        params["targetWidth"] = 50;
        params["targetHeight"] = 50;

        bool configResult = tool.configure(params);
        REQUIRE(configResult == true);

        cv::Mat input(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
        ToolResult result;
        bool success = tool.execute(input, result);
        REQUIRE(success == true);
        REQUIRE(result.data["outputWidth"].toInt() == 50);
        REQUIRE(result.data["outputHeight"].toInt() == 50);
    }

    SECTION("Flip image horizontally") {
        ImageTransformTool tool;
        QJsonObject params;
        params["transformType"] = "flip";
        params["flipCode"] = 1;

        bool configResult = tool.configure(params);
        REQUIRE(configResult == true);

        cv::Mat input(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
        ToolResult result;
        bool success = tool.execute(input, result);
        REQUIRE(success == true);
    }

    SECTION("Rotate image") {
        ImageTransformTool tool;
        QJsonObject params;
        params["transformType"] = "rotate";
        params["angle"] = 45.0;

        bool configResult = tool.configure(params);
        REQUIRE(configResult == true);

        cv::Mat input(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
        ToolResult result;
        bool success = tool.execute(input, result);
        REQUIRE(success == true);
    }
}

TEST_CASE("ImageMergeTool Operations", "[ImageMergeTool]") {
    SECTION("Horizontal merge") {
        ImageMergeTool tool;
        QJsonObject params;
        params["mergeType"] = "horizontal";

        bool configResult = tool.configure(params);
        REQUIRE(configResult == true);

        cv::Mat img1(100, 100, CV_8UC3, cv::Scalar(255, 0, 0));
        ToolResult result1;
        tool.execute(img1, result1);

        cv::Mat img2(100, 100, CV_8UC3, cv::Scalar(0, 255, 0));
        ToolResult result2;
        bool success = tool.execute(img2, result2);
        REQUIRE(success == true);
    }

    SECTION("Vertical merge") {
        ImageMergeTool tool;
        QJsonObject params;
        params["mergeType"] = "vertical";

        bool configResult = tool.configure(params);
        REQUIRE(configResult == true);

        cv::Mat img1(100, 100, CV_8UC3, cv::Scalar(255, 0, 0));
        ToolResult result1;
        tool.execute(img1, result1);

        cv::Mat img2(100, 100, CV_8UC3, cv::Scalar(0, 255, 0));
        ToolResult result2;
        bool success = tool.execute(img2, result2);
        REQUIRE(success == true);
    }
}

TEST_CASE("All New Tools Registered in Factory", "[ToolFactory]") {
    SECTION("New tools are registered") {
        QStringList expectedTools = {
            "TemplateMatch", "EdgeDetect", "BlobDetect",
            "ColorDetect", "Threshold", "ImagePreprocess",
            "ContourAnalyze", "BranchControl", "GeometryMeasure",
            "LineCircleDetect", "ImageArithmetic", "ImageTransform", "ImageMerge"
        };

        QStringList availableTools = ToolFactory::instance()->getAvailableToolTypes();

        for (const QString& tool : expectedTools) {
            REQUIRE(availableTools.contains(tool));
        }
    }

    SECTION("Create each new tool via factory") {
        QStringList newTools = {
            "BranchControl", "GeometryMeasure", "LineCircleDetect",
            "ImageArithmetic", "ImageTransform", "ImageMerge"
        };

        for (const QString& type : newTools) {
            VisionTool* tool = ToolFactory::instance()->createTool(type);
            REQUIRE(tool != nullptr);
            REQUIRE(tool->type() == type);
            delete tool;
        }
    }
}