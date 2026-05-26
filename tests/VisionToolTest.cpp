#include <catch2/catch_all.hpp>
#include "VisionTool.h"
#include "TemplateMatchTool.h"
#include "ToolFactory.h"

TEST_CASE("VisionTool Factory", "[VisionTool]") {
    SECTION("Create tool from factory") {
        VisionTool* tool = ToolFactory::instance()->createTool("TemplateMatch");
        REQUIRE(tool != nullptr);
        REQUIRE(tool->type() == "TemplateMatch");
        delete tool;
    }
    
    SECTION("Unknown tool type returns null") {
        VisionTool* tool = ToolFactory::instance()->createTool("UnknownTool");
        REQUIRE(tool == nullptr);
    }
}

TEST_CASE("TemplateMatch Tool", "[TemplateMatchTool]") {
    SECTION("Configure tool") {
        TemplateMatchTool tool;
        QJsonObject params;
        params["threshold"] = 0.9;
        params["method"] = "CCOEFF_NORMED";
        
        bool result = tool.configure(params);
        REQUIRE(result == true);
    }
    
    SECTION("Serialize tool") {
        TemplateMatchTool tool;
        tool.setId("tool-001");
        
        QJsonObject obj = tool.serialize();
        REQUIRE(obj["id"].toString() == "tool-001");
        REQUIRE(obj["type"].toString() == "TemplateMatch");
    }
}