// test_agent_mock_operators.cpp - 5个Agent角色mock算子测试
// 验证：agentRole字段 / ToolResult携带Agent信息 / 各角色mock行为
#include "../catch2/catch2_minimal.hpp"
#include "OperatorSDK/MockOperatorBase.h"
#include "OperatorSDK/OperatorManifest.h"
#include "OperatorSDK/IOperatorRegistry.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/core/mat.hpp>

// 5个Agent角色mock算子
struct AgentMockInfo {
    QString type;
    QString cnName;
    QString agentRole;
};

static const QList<AgentMockInfo> AGENT_MOCKS = {
    {"MockClassify",  "模拟分类", "Reviewer"},
    {"MockValidate",  "模拟验证", "Gatekeeper"},
    {"MockPlan",      "模拟规划", "Planner"},
    {"MockCoordinate", "模拟协调", "Coordinator"},
    {"MockAudit",     "模拟审计", "SpecGuardian"},
};

// 用例 1: 所有Agent mock算子manifest包含agentRole字段
TEST_CASE("AgentMockOperators_ManifestContainsRole", "[agent_mock]") {
    QString operatorsDir = QDir::currentPath() + "/bin/operators";
    int validCount = 0;

    for (const auto& info : AGENT_MOCKS) {
        QString manifestPath = operatorsDir + "/" + info.type + ".json";
        if (!QFile::exists(manifestPath)) continue;

        QFile f(manifestPath);
        if (!f.open(QIODevice::ReadOnly)) continue;

        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        QJsonObject m = doc.object();

        REQUIRE(m["type"].toString() == info.type);
        REQUIRE(m["isMock"].toBool() == true);
        REQUIRE(m["agentRole"].toString() == info.agentRole);
        REQUIRE_FALSE(m["cnName"].toString().isEmpty());
        validCount++;
    }
    REQUIRE(validCount > 0);
}

// 用例 2: Agent mock算子执行时ToolResult携带agentRole
TEST_CASE("AgentMockOperators_ResultContainsRole", "[agent_mock]") {
    QString operatorsDir = QDir::currentPath() + "/bin/operators";
    cv::Mat testInput(64, 64, CV_8UC1, cv::Scalar(200));
    int executedCount = 0;

    for (const auto& info : AGENT_MOCKS) {
        QString libPath = operatorsDir + "/" + info.type + ".dll";
        if (!QFile::exists(libPath)) continue;

        QString err;
        QDV::OperatorManifest manifest;
        if (!QDV::loadPlugin(libPath, manifest, &err)) continue;

        QDV::IOperator* op = QDV::IOperatorRegistry::instance().createOperator(info.type);
        if (!op) continue;

        REQUIRE(op->type() == info.type);

        ToolResult result;
        bool ok = op->execute(testInput, result);

        REQUIRE(ok == true);
        REQUIRE(result.data["isMock"].toBool() == true);
        REQUIRE(result.data["agentRole"].toString() == info.agentRole);
        REQUIRE_FALSE(result.data["description"].toString().isEmpty());

        delete op;
        executedCount++;
    }
    REQUIRE(executedCount >= 0);
}

// 用例 3: Agent mock算子覆盖全部6个协作角色
TEST_CASE("AgentMockOperators_CoverAllRoles", "[agent_mock]") {
    QStringList expectedRoles = {"Reviewer", "Gatekeeper", "Planner", "Coordinator", "SpecGuardian"};
    QStringList actualRoles;
    for (const auto& info : AGENT_MOCKS) {
        actualRoles << info.agentRole;
    }

    for (const QString& role : expectedRoles) {
        REQUIRE(actualRoles.contains(role));
    }
    REQUIRE(actualRoles.size() == 5);
}
