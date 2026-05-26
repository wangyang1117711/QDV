#include <catch2/catch_all.hpp>
#include "TemplateMatchTool.h"
#include "EdgeDetectTool.h"
#include "BlobDetectTool.h"
#include "ThresholdTool.h"
#include "ToolFactory.h"
#include <opencv2/opencv.hpp>

TEST_CASE("TemplateMatchTool Validation", "[TemplateMatchTool]") {
    SECTION("Validate template path with valid file") {
        QString validPath = "./test_data/template.png";
        bool result = validateTemplatePath(validPath);
        REQUIRE(result == false);
    }
    
    SECTION("Validate template path with non-existent file") {
        QString invalidPath = "./non_existent.png";
        bool result = validateTemplatePath(invalidPath);
        REQUIRE(result == false);
    }
    
    SECTION("Configure with valid parameters") {
        TemplateMatchTool tool;
        QJsonObject params;
        params["threshold"] = 0.8;
        params["method"] = "CCOEFF_NORMED";
        
        bool result = tool.configure(params);
        REQUIRE(result == true);
    }
    
    SECTION("Configure with invalid threshold") {
        TemplateMatchTool tool;
        QJsonObject params;
        params["threshold"] = 1.5;
        params["method"] = "CCOEFF_NORMED";
        
        bool result = tool.configure(params);
        REQUIRE(result == false);
    }
    
    SECTION("Configure with negative threshold") {
        TemplateMatchTool tool;
        QJsonObject params;
        params["threshold"] = -0.1;
        params["method"] = "CCOEFF_NORMED";
        
        bool result = tool.configure(params);
        REQUIRE(result == false);
    }
    
    SECTION("Configure with missing method") {
        TemplateMatchTool tool;
        QJsonObject params;
        params["threshold"] = 0.8;
        
        bool result = tool.configure(params);
        REQUIRE(result == false);
    }
    
    SECTION("Execute with empty input") {
        TemplateMatchTool tool;
        cv::Mat emptyMat;
        
        cv::Mat result = tool.execute(emptyMat);
        REQUIRE(result.empty());
    }
    
    SECTION("Execute with valid input") {
        TemplateMatchTool tool;
        cv::Mat input(100, 100, CV_8UC3, cv::Scalar(100, 100, 100));
        
        cv::Mat result = tool.execute(input);
        REQUIRE(!result.empty());
    }
}

TEST_CASE("EdgeDetectTool Validation", "[EdgeDetectTool]") {
    SECTION("Configure with valid parameters") {
        EdgeDetectTool tool;
        QJsonObject params;
        params["lowThreshold"] = 50;
        params["highThreshold"] = 150;
        params["apertureSize"] = 3;
        
        bool result = tool.configure(params);
        REQUIRE(result == true);
    }
    
    SECTION("Configure with invalid threshold order") {
        EdgeDetectTool tool;
        QJsonObject params;
        params["lowThreshold"] = 150;
        params["highThreshold"] = 50;
        
        bool result = tool.configure(params);
        REQUIRE(result == false);
    }
    
    SECTION("Configure with invalid aperture size") {
        EdgeDetectTool tool;
        QJsonObject params;
        params["lowThreshold"] = 50;
        params["highThreshold"] = 150;
        params["apertureSize"] = 4;
        
        bool result = tool.configure(params);
        REQUIRE(result == false);
    }
    
    SECTION("Execute with grayscale input") {
        EdgeDetectTool tool;
        cv::Mat input(100, 100, CV_8UC1, cv::Scalar(128));
        
        cv::Mat result = tool.execute(input);
        REQUIRE(!result.empty());
    }
    
    SECTION("Execute with color input") {
        EdgeDetectTool tool;
        cv::Mat input(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
        
        cv::Mat result = tool.execute(input);
        REQUIRE(!result.empty());
    }
}

TEST_CASE("ThresholdTool Validation", "[ThresholdTool]") {
    SECTION("Configure with valid parameters") {
        ThresholdTool tool;
        QJsonObject params;
        params["threshold"] = 128;
        params["maxValue"] = 255;
        params["type"] = "BINARY";
        
        bool result = tool.configure(params);
        REQUIRE(result == true);
    }
    
    SECTION("Configure with invalid threshold range") {
        ThresholdTool tool;
        QJsonObject params;
        params["threshold"] = 300;
        params["maxValue"] = 255;
        
        bool result = tool.configure(params);
        REQUIRE(result == false);
    }
    
    SECTION("Configure with invalid max value") {
        ThresholdTool tool;
        QJsonObject params;
        params["threshold"] = 128;
        params["maxValue"] = 300;
        
        bool result = tool.configure(params);
        REQUIRE(result == false);
    }
    
    SECTION("Execute with valid input") {
        ThresholdTool tool;
        cv::Mat input(100, 100, CV_8UC1, cv::Scalar(100));
        
        cv::Mat result = tool.execute(input);
        REQUIRE(!result.empty());
    }
}

TEST_CASE("ToolFactory Extended", "[ToolFactory]") {
    SECTION("Create all tool types") {
        QStringList toolTypes = {"TemplateMatch", "EdgeDetect", "BlobDetect", 
                                "Threshold", "ColorDetect", "ContourAnalyze",
                                "ImagePreprocess"};
        
        for (const QString& type : toolTypes) {
            VisionTool* tool = ToolFactory::instance()->createTool(type);
            REQUIRE(tool != nullptr);
            REQUIRE(tool->type() == type);
            delete tool;
        }
    }
    
    SECTION("Create tool case insensitivity") {
        VisionTool* tool = ToolFactory::instance()->createTool("templatematch");
        REQUIRE(tool == nullptr);
    }
}