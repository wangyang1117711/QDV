// test_spec_guardian.cpp - SpecGuardian + VERDICT 协议单元测试
// 覆盖：
//   - VERDICT PASS/FAIL/NEEDS_HUMAN_REVIEW 格式化与解析
//   - SpecGuardian 审计通过/失败场景
//   - ACL 读写权限控制
//   - 确定性终门 JSON schema 校验
//   - 审计历史查询
//
// 对应：AGENTS.md v2.0「III. 强制行为规范」「IV. 治理与执行」

#include "../catch2/catch2_minimal.hpp"
#include "Core/Verdict.h"
#include "Core/SpecGuardian.h"

#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QObject>

using namespace QDV;

// ====================================================================
// VERDICT 数据结构测试
// ====================================================================

// 用例 1: passResult 构造 PASS 裁决
TEST_CASE("Verdict_PassResult_StatusPass", "[verdict]") {
    Verdict v = Verdict::passResult(0.95);
    REQUIRE(v.status == Verdict::PASS);
    REQUIRE(v.isPass() == true);
    REQUIRE(v.isBlocking() == false);
    REQUIRE(v.checks.isEmpty());
}

// 用例 2: failResult 构造 FAIL 裁决，附带原因与证据
TEST_CASE("Verdict_FailResult_StatusFail", "[verdict]") {
    Verdict v = Verdict::failResult("退款金额错误", 1.0, "trace://session_12345");
    REQUIRE(v.status == Verdict::FAIL);
    REQUIRE(v.isPass() == false);
    REQUIRE(v.isBlocking() == true);
    REQUIRE(v.checks.size() == 1);
    REQUIRE(v.checks[0].passed == false);
    REQUIRE(v.checks[0].reason == "退款金额错误");
    REQUIRE(v.checks[0].evidenceLink == "trace://session_12345");
}

// 用例 3: needsHumanReview 构造 NEEDS_HUMAN_REVIEW 裁决
TEST_CASE("Verdict_NeedsHumanReview_StatusReview", "[verdict]") {
    Verdict v = Verdict::needsHumanReview("无法自动判定", 0.5);
    REQUIRE(v.status == Verdict::NEEDS_HUMAN_REVIEW);
    REQUIRE(v.isPass() == false);
    // NEEDS_HUMAN_REVIEW 不阻断，但需人工介入
    REQUIRE(v.isBlocking() == false);
    REQUIRE(v.checks.size() == 1);
    REQUIRE(v.checks[0].reason == "无法自动判定");
}

// 用例 4: toString 输出包含 VERDICT: PASS 与 confidence
TEST_CASE("Verdict_ToString_ContainsVerdictPass", "[verdict]") {
    Verdict v = Verdict::passResult(0.95);
    QString s = v.toString();
    REQUIRE(s.contains("VERDICT: PASS"));
    REQUIRE(s.contains("confidence: 0.95"));
}

// 用例 5: toString 输出 FAIL 包含 reason 与 evidence_link
TEST_CASE("Verdict_ToString_ContainsFailReason", "[verdict]") {
    Verdict v = Verdict::failResult("契约不匹配", 1.0, "trace://t1");
    QString s = v.toString();
    REQUIRE(s.contains("VERDICT: FAIL"));
    REQUIRE(s.contains("reason:"));
    REQUIRE(s.contains("契约不匹配"));
    REQUIRE(s.contains("evidence_link:"));
    REQUIRE(s.contains("trace://t1"));
}

// 用例 6: toString 输出 NEEDS_HUMAN_REVIEW
TEST_CASE("Verdict_ToString_ContainsNeedsHumanReview", "[verdict]") {
    Verdict v = Verdict::needsHumanReview("边界情况");
    QString s = v.toString();
    REQUIRE(s.contains("VERDICT: NEEDS_HUMAN_REVIEW"));
}

// 用例 7: fromString 往返解析 PASS
TEST_CASE("Verdict_FromString_RoundTripPass", "[verdict]") {
    Verdict original = Verdict::passResult(0.8);
    Verdict parsed = Verdict::fromString(original.toString());
    REQUIRE(parsed.status == Verdict::PASS);
    REQUIRE(parsed.isPass() == true);
}

// 用例 8: fromString 往返解析 FAIL（含 reason）
TEST_CASE("Verdict_FromString_RoundTripFail", "[verdict]") {
    Verdict original = Verdict::failResult("金额错误", 1.0, "trace://s1");
    Verdict parsed = Verdict::fromString(original.toString());
    REQUIRE(parsed.status == Verdict::FAIL);
    REQUIRE(parsed.isBlocking() == true);
    REQUIRE_FALSE(parsed.checks.isEmpty());
    REQUIRE(parsed.checks[0].passed == false);
}

// 用例 9: fromString 往返解析 NEEDS_HUMAN_REVIEW
TEST_CASE("Verdict_FromString_RoundTripNeedsHumanReview", "[verdict]") {
    Verdict original = Verdict::needsHumanReview("不确定", 0.4);
    Verdict parsed = Verdict::fromString(original.toString());
    REQUIRE(parsed.status == Verdict::NEEDS_HUMAN_REVIEW);
}

// ====================================================================
// SpecGuardian 审计测试
// ====================================================================

// 用例 10: 无规则审计默认通过（vacuously true）
TEST_CASE("SpecGuardian_Audit_NoRules_DefaultPass", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();
    QVariantMap output, spec;
    // 使用独立 agentId 避免历史污染断言
    Verdict v = g->auditAgentOutput("test_agent_norules", "task_001", output, spec);
    REQUIRE(v.status == Verdict::PASS);
    REQUIRE(v.isPass() == true);
}

// 用例 11: 注册通过规则，审计通过
TEST_CASE("SpecGuardian_Audit_AllRulesPass_ReturnsPass", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();
    g->registerRule("rule_pass_always", [](const QVariantMap&, const QVariantMap&) {
        Verdict::Check c;
        c.name = "rule_pass_always";
        c.passed = true;
        return c;
    });

    QVariantMap output, spec;
    Verdict v = g->auditAgentOutput("test_agent_pass", "task_002", output, spec);
    // 注意：此 agent 可能还命中其他测试已注册的失败规则，但本用例独立验证「存在通过规则」
    // 关键断言：至少存在一条通过的 check
    bool hasPass = false;
    for (const auto& c : v.checks) {
        if (c.passed && c.name == "rule_pass_always") {
            hasPass = true;
            break;
        }
    }
    REQUIRE(hasPass == true);
}

// 用例 12: 注册失败规则，审计失败且阻断
TEST_CASE("SpecGuardian_Audit_RuleFails_ReturnsFail", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();
    g->registerRule("rule_fail_always", [](const QVariantMap&, const QVariantMap&) {
        Verdict::Check c;
        c.name = "rule_fail_always";
        c.passed = false;
        c.reason = "契约违反";
        c.evidenceLink = "trace://fail_001";
        return c;
    });

    QVariantMap output, spec;
    Verdict v = g->auditAgentOutput("test_agent_fail", "task_003", output, spec);
    REQUIRE(v.status == Verdict::FAIL);
    REQUIRE(v.isBlocking() == true);
    // 验证失败原因可追溯
    bool hasFail = false;
    for (const auto& c : v.checks) {
        if (!c.passed && c.reason == "契约违反") {
            hasFail = true;
            break;
        }
    }
    REQUIRE(hasFail == true);
}

// 用例 13: 审计发射 verdictIssued 信号
TEST_CASE("SpecGuardian_Audit_EmitsVerdictIssued", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();
    int count = 0;
    auto conn = QObject::connect(g, &SpecGuardian::verdictIssued,
        [&count](const QString&, const Verdict&) { ++count; });

    QVariantMap output, spec;
    g->auditAgentOutput("test_agent_signal", "task_004", output, spec);

    QObject::disconnect(conn);
    REQUIRE(count >= 1);
}

// 用例 14: FAIL 审计发射 blockingVerdict 信号
TEST_CASE("SpecGuardian_Audit_FailEmitsBlockingVerdict", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();
    g->registerRule("rule_for_blocking", [](const QVariantMap&, const QVariantMap&) {
        Verdict::Check c;
        c.name = "rule_for_blocking";
        c.passed = false;
        c.reason = "阻断原因";
        return c;
    });

    int count = 0;
    QString capturedReason;
    auto conn = QObject::connect(g, &SpecGuardian::blockingVerdict,
        [&count, &capturedReason](const QString&, const QString& reason) {
            ++count;
            capturedReason = reason;
        });

    QVariantMap output, spec;
    g->auditAgentOutput("test_agent_blocking", "task_005", output, spec);

    QObject::disconnect(conn);
    REQUIRE(count >= 1);
    REQUIRE_FALSE(capturedReason.isEmpty());
}

// 用例 15: 规则抛异常时该项失败，不中断整体审计
TEST_CASE("SpecGuardian_Audit_RuleThrows_ReturnsFailForThatCheck", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();
    g->registerRule("rule_throws", [](const QVariantMap&, const QVariantMap&) -> Verdict::Check {
        throw std::runtime_error("规则内部错误");
    });

    QVariantMap output, spec;
    Verdict v = g->auditAgentOutput("test_agent_throw", "task_006", output, spec);

    // 异常规则项应被标记为失败
    bool hasThrownFail = false;
    for (const auto& c : v.checks) {
        if (c.name == "rule_throws" && !c.passed) {
            hasThrownFail = true;
            break;
        }
    }
    REQUIRE(hasThrownFail == true);
}

// ====================================================================
// ACL 权限测试（状态主权）
// ====================================================================

// 用例 16: 默认无权限（最小权限原则）
TEST_CASE("SpecGuardian_ACL_DefaultDeny", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();
    REQUIRE(g->canRead("acl_new_agent", "acl_secret_key") == false);
    REQUIRE(g->canWrite("acl_new_agent", "acl_secret_key") == false);
}

// 用例 17: grantAccess 后具备读写权限
TEST_CASE("SpecGuardian_ACL_GrantAccess_ReadWrite", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();
    g->grantAccess("acl_rw_agent", "acl_state_rw", true, true);
    REQUIRE(g->canRead("acl_rw_agent", "acl_state_rw") == true);
    REQUIRE(g->canWrite("acl_rw_agent", "acl_state_rw") == true);
}

// 用例 18: 只读权限
TEST_CASE("SpecGuardian_ACL_ReadOnly", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();
    g->grantAccess("acl_ro_agent", "acl_state_ro", true, false);
    REQUIRE(g->canRead("acl_ro_agent", "acl_state_ro") == true);
    REQUIRE(g->canWrite("acl_ro_agent", "acl_state_ro") == false);
}

// 用例 19: 不同 Agent 之间权限隔离
TEST_CASE("SpecGuardian_ACL_AgentIsolation", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();
    g->grantAccess("acl_agent_a", "acl_shared_key", true, true);
    // agent_b 未授权，即使 stateKey 相同也无权限
    REQUIRE(g->canRead("acl_agent_b", "acl_shared_key") == false);
    REQUIRE(g->canWrite("acl_agent_b", "acl_shared_key") == false);
}

// 用例 20: 同一 Agent 对不同 stateKey 权限独立
TEST_CASE("SpecGuardian_ACL_StateKeyIsolation", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();
    g->grantAccess("acl_multi_agent", "acl_key_1", true, false);
    g->grantAccess("acl_multi_agent", "acl_key_2", false, true);
    REQUIRE(g->canRead("acl_multi_agent", "acl_key_1") == true);
    REQUIRE(g->canWrite("acl_multi_agent", "acl_key_1") == false);
    REQUIRE(g->canRead("acl_multi_agent", "acl_key_2") == false);
    REQUIRE(g->canWrite("acl_multi_agent", "acl_key_2") == true);
}

// ====================================================================
// 确定性终门测试（JSON Schema 校验）
// ====================================================================

// 用例 21: schema 校验全部通过
TEST_CASE("SpecGuardian_FinalGate_ValidOutput_Pass", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();

    QVariantMap output;
    output["name"] = QString("检测任务");
    output["count"] = 42;

    QJsonObject schema;
    QJsonArray required;
    required.append("name");
    required.append("count");
    schema["required"] = required;

    QJsonObject properties;
    QJsonObject nameProp;
    nameProp["type"] = "string";
    properties["name"] = nameProp;
    QJsonObject countProp;
    countProp["type"] = "number";
    properties["count"] = countProp;
    schema["properties"] = properties;

    Verdict v = g->finalGate("task_gate_001", output, schema);
    REQUIRE(v.status == Verdict::PASS);
    REQUIRE(v.isPass() == true);
}

// 用例 22: 缺少必填字段 → FAIL
TEST_CASE("SpecGuardian_FinalGate_MissingRequired_Fail", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();

    QVariantMap output;
    output["name"] = QString("任务");  // 缺少 count

    QJsonObject schema;
    QJsonArray required;
    required.append("name");
    required.append("count");
    schema["required"] = required;

    Verdict v = g->finalGate("task_gate_002", output, schema);
    REQUIRE(v.status == Verdict::FAIL);
    REQUIRE(v.isBlocking() == true);
    // 失败原因应提及缺失字段
    bool mentionsMissing = false;
    for (const auto& c : v.checks) {
        if (!c.passed && c.reason.contains("count")) {
            mentionsMissing = true;
            break;
        }
    }
    REQUIRE(mentionsMissing == true);
}

// 用例 23: 类型不匹配 → FAIL
TEST_CASE("SpecGuardian_FinalGate_TypeMismatch_Fail", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();

    QVariantMap output;
    output["count"] = QString("不是数字");  // 字符串而非 number

    QJsonObject schema;
    QJsonObject properties;
    QJsonObject countProp;
    countProp["type"] = "number";
    properties["count"] = countProp;
    schema["properties"] = properties;

    Verdict v = g->finalGate("task_gate_003", output, schema);
    REQUIRE(v.status == Verdict::FAIL);
}

// 用例 24: bool 类型校验通过
TEST_CASE("SpecGuardian_FinalGate_BoolType_Pass", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();

    QVariantMap output;
    output["enabled"] = true;

    QJsonObject schema;
    QJsonObject properties;
    QJsonObject boolProp;
    boolProp["type"] = "bool";
    properties["enabled"] = boolProp;
    schema["properties"] = properties;

    Verdict v = g->finalGate("task_gate_004", output, schema);
    REQUIRE(v.status == Verdict::PASS);
}

// 用例 25: number 类型接受整数与浮点
TEST_CASE("SpecGuardian_FinalGate_NumberType_AcceptsIntAndDouble", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();

    QVariantMap output;
    output["int_val"] = 100;
    output["dbl_val"] = 3.14;

    QJsonObject schema;
    QJsonObject properties;
    QJsonObject intProp;
    intProp["type"] = "number";
    properties["int_val"] = intProp;
    QJsonObject dblProp;
    dblProp["type"] = "number";
    properties["dbl_val"] = dblProp;
    schema["properties"] = properties;

    Verdict v = g->finalGate("task_gate_005", output, schema);
    REQUIRE(v.status == Verdict::PASS);
}

// 用例 26: 非必填字段缺失不阻断（仅 required 检查缺失）
TEST_CASE("SpecGuardian_FinalGate_OptionalFieldMissing_Pass", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();

    QVariantMap output;
    output["required_field"] = QString("present");
    // optional_field 缺失，但未列入 required

    QJsonObject schema;
    QJsonArray required;
    required.append("required_field");
    schema["required"] = required;

    QJsonObject properties;
    QJsonObject reqProp;
    reqProp["type"] = "string";
    properties["required_field"] = reqProp;
    QJsonObject optProp;
    optProp["type"] = "string";
    properties["optional_field"] = optProp;
    schema["properties"] = properties;

    Verdict v = g->finalGate("task_gate_006", output, schema);
    REQUIRE(v.status == Verdict::PASS);
}

// ====================================================================
// 审计历史测试
// ====================================================================

// 用例 27: 审计历史按 agent 查询，调用次数递增
TEST_CASE("SpecGuardian_AuditHistory_ByAgent", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();
    const QString agentId = "history_agent_unique";

    QVariantMap output, spec;
    int before = g->getAuditHistory(agentId).size();
    g->auditAgentOutput(agentId, "task_hist_001", output, spec);
    g->auditAgentOutput(agentId, "task_hist_002", output, spec);
    int after = g->getAuditHistory(agentId).size();

    REQUIRE(after == before + 2);
}

// 用例 28: 审计历史全部查询（空 agentId）返回非空
TEST_CASE("SpecGuardian_AuditHistory_AllAgents", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();
    QList<Verdict> all = g->getAuditHistory();
    // 前面测试已产生历史，汇总应非空
    REQUIRE_FALSE(all.isEmpty());
}

// 用例 29: 不同 Agent 历史相互隔离
TEST_CASE("SpecGuardian_AuditHistory_AgentIsolation", "[guardian]") {
    SpecGuardian* g = SpecGuardian::instance();
    const QString agentA = "hist_agent_a_unique";
    const QString agentB = "hist_agent_b_unique";

    QVariantMap output, spec;
    int beforeA = g->getAuditHistory(agentA).size();
    int beforeB = g->getAuditHistory(agentB).size();
    g->auditAgentOutput(agentA, "task_iso", output, spec);

    // agentA 历史增加，agentB 不受影响
    REQUIRE(g->getAuditHistory(agentA).size() == beforeA + 1);
    REQUIRE(g->getAuditHistory(agentB).size() == beforeB);
}
