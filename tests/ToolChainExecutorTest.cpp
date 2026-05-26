#include <catch2/catch_all.hpp>
#include "ToolChainExecutor.h"
#include "TemplateMatchTool.h"
#include "EdgeDetectTool.h"

TEST_CASE("ToolChain Executor", "[ToolChainExecutor]") {
    SECTION("Execute empty tool chain") {
        ToolChainExecutor executor;
        cv::Mat input(100, 100, CV_8UC3);
        
        bool result = executor.execute(input);
        REQUIRE(result == true);
    }
    
    SECTION("Set and get tools") {
        ToolChainExecutor executor;
        QList<VisionTool*> tools;
        
        TemplateMatchTool* tool1 = new TemplateMatchTool();
        tool1->setId("tool-001");
        tools.append(tool1);
        
        EdgeDetectTool* tool2 = new EdgeDetectTool();
        tool2->setId("tool-002");
        tools.append(tool2);
        
        executor.setTools(tools);
        
        delete tool1;
        delete tool2;
    }
}