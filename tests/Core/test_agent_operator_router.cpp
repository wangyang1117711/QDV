// test_agent_operator_router.cpp - AgentOperatorRouter 单元测试
// 覆盖：12类任务路由 / 空任务处理 / 优先级 / firstChoice / JSON 序列化
#include "../catch2/catch2_minimal.hpp"
#include "AgentOperatorRouter.h"

// 用例 1: 12类任务全部返回非空映射
TEST_CASE("AgentOperatorRouter_12TaskTypes_AllNonEmpty", "[router]") {
    QDV::AgentOperatorRouter router;
    QStringList expectedTasks = {
        "图像采集", "滤波预处理", "形态学", "图像分割",
        "Blob分析", "特征提取", "匹配定位", "几何测量",
        "3D视觉", "深度学习", "评审验证", "审计检查"
    };

    for (const QString& task : expectedTasks) {
        auto mappings = router.route(task);
        REQUIRE_FALSE(mappings.isEmpty());
        REQUIRE(mappings.size() >= 2);
    }
}

// 用例 2: 空任务返回空列表不崩溃
TEST_CASE("AgentOperatorRouter_EmptyTask_ReturnsEmpty", "[router]") {
    QDV::AgentOperatorRouter router;
    auto mappings = router.route("");
    REQUIRE(mappings.isEmpty());
}

// 用例 3: 未知任务返回空列表不崩溃
TEST_CASE("AgentOperatorRouter_UnknownTask_ReturnsEmpty", "[router]") {
    QDV::AgentOperatorRouter router;
    auto mappings = router.route("不存在的任务类型");
    REQUIRE(mappings.isEmpty());
}

// 用例 4: 支持的任务类型数量为12
TEST_CASE("AgentOperatorRouter_SupportedTaskTypes_Count12", "[router]") {
    QDV::AgentOperatorRouter router;
    QStringList types = router.supportedTaskTypes();
    REQUIRE(types.size() == 12);
}

// 用例 5: isSupported 正确识别支持的任务
TEST_CASE("AgentOperatorRouter_IsSupported_Correct", "[router]") {
    QDV::AgentOperatorRouter router;
    REQUIRE(router.isSupported("图像采集") == true);
    REQUIRE(router.isSupported("滤波预处理") == true);
    REQUIRE(router.isSupported("不存在") == false);
    REQUIRE(router.isSupported("") == false);
}

// 用例 6: firstChoice 返回 priority=1 的映射
TEST_CASE("AgentOperatorRouter_FirstChoice_Priority1", "[router]") {
    QDV::AgentOperatorRouter router;
    QDV::OperatorAgentMapping m = router.firstChoice("图像采集");

    REQUIRE_FALSE(m.operatorType.isEmpty());
    REQUIRE(m.priority == 1);
    REQUIRE(m.operatorType == "ReadImage");
    REQUIRE(m.agentRole == "Developer");
}

// 用例 7: 各任务类型的 Agent 角色符合 Halcon 映射规则
TEST_CASE("AgentOperatorRouter_AgentRoles_MatchHalconMapping", "[router]") {
    QDV::AgentOperatorRouter router;

    // Developer 角色
    REQUIRE(router.firstChoice("图像采集").agentRole == "Developer");
    REQUIRE(router.firstChoice("滤波预处理").agentRole == "Developer");
    REQUIRE(router.firstChoice("形态学").agentRole == "Developer");
    REQUIRE(router.firstChoice("图像分割").agentRole == "Developer");
    REQUIRE(router.firstChoice("特征提取").agentRole == "Developer");
    REQUIRE(router.firstChoice("匹配定位").agentRole == "Developer");
    REQUIRE(router.firstChoice("3D视觉").agentRole == "Developer");

    // Reviewer 角色
    REQUIRE(router.firstChoice("Blob分析").agentRole == "Reviewer");
    REQUIRE(router.firstChoice("几何测量").agentRole == "Reviewer");
    REQUIRE(router.firstChoice("深度学习").agentRole == "Reviewer");

    // Gatekeeper 角色
    REQUIRE(router.firstChoice("评审验证").agentRole == "Gatekeeper");

    // SpecGuardian 角色
    REQUIRE(router.firstChoice("审计检查").agentRole == "SpecGuardian");
}

// 用例 8: routeAsJson 返回有效 JSON 数组
TEST_CASE("AgentOperatorRouter_RouteAsJson_ValidArray", "[router]") {
    QDV::AgentOperatorRouter router;
    QJsonArray arr = router.routeAsJson("形态学");

    REQUIRE(arr.size() == 6);
    QJsonObject first = arr[0].toObject();
    REQUIRE(first["operatorType"].toString() == "Erosion");
    REQUIRE(first["agentRole"].toString() == "Developer");
    REQUIRE(first["priority"].toInt() == 1);
}
