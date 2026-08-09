// BranchControlTool 单元测试（v2.7.0 升级覆盖）
//
// 覆盖范围：
//  - 基本比较运算：>/</==/!=/>=/<=/ok/ng
//  - P1-4c 扩展：ge/le/ne/contains/startswith/endswith/in
//  - v2.7.0 逻辑运算：AND/OR/NOT/XOR
//  - v2.7.0 switch 多分支选择
//  - parseLogicInputs / evaluateLogicInput（通过 AND/OR 间接测试）
//  - evaluateCondition 各分支
//  - serialize/deserialize 往返
//  - 输出端口 verdict/switchValue/branchCases/logicInputs
//
// 注意：evaluateCondition / parseLogicInputs / evaluateLogicInput 均为 private，
//       通过 configure + execute 后检查 result.ports["conditionMet"] 间接验证。
//
// 测试框架：项目自制 catch2_minimal.hpp

#include "../catch2/catch2_minimal.hpp"
#include "Vision/BranchControlTool.h"
#include "Core/BranchNode.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <opencv2/core.hpp>

using namespace QDV;

// 构造一个非空测试图像
static cv::Mat makeTestImage() {
    return cv::Mat(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
}

// 构造一个有效的分支配置 JSON
// 默认 conditionOp=">"，conditionValue=5（result.score=0 时 → 0>5=false）
static QJsonObject makeBranchParams(const QString& op, const QJsonValue& value,
                                     const QString& id = "branch1",
                                     const QString& source = "src1") {
    QJsonObject params;
    params["id"] = id;
    params["source"] = source;
    params["conditionOp"] = op;
    params["conditionValue"] = value;
    params["trueBranch"] = QJsonArray{"t1"};
    params["falseBranch"] = QJsonArray{"f1"};
    return params;
}

// =====================================================
// 基本属性
// =====================================================

TEST_CASE("BranchControlTool: type() 返回 'BranchControl'", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.type() == "BranchControl");
}

TEST_CASE("BranchControlTool: 默认 branchNode 为空", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.branchNode() == nullptr);
}

// =====================================================
// configure
// =====================================================

TEST_CASE("BranchControlTool.configure: 空参数返回 false", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject empty;
    REQUIRE_FALSE(tool.configure(empty));
}

TEST_CASE("BranchControlTool.configure: 有效参数创建 BranchNode", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    const QJsonObject params = makeBranchParams(">", 5);
    REQUIRE(tool.configure(params));
    REQUIRE(tool.branchNode() != nullptr);
    REQUIRE(tool.branchNode()->isValid());
    REQUIRE(tool.branchNode()->conditionOp == ">");
}

TEST_CASE("BranchControlTool.configure: conditionOp/conditionValue 映射到 op/value", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    const QJsonObject params = makeBranchParams("<", 10);
    REQUIRE(tool.configure(params));
    // BranchNode 内部字段：conditionOp/conditionValue（已映射）
    REQUIRE(tool.branchNode()->conditionOp == "<");
    REQUIRE(tool.branchNode()->conditionValue.toInt() == 10);
}

TEST_CASE("BranchControlTool.configure: 缺少 trueBranch/falseBranch 导致 invalid", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params;
    params["id"] = "b1";
    params["source"] = "s1";
    params["conditionOp"] = ">";
    params["conditionValue"] = 5;
    // 不提供 trueBranch/falseBranch
    REQUIRE_FALSE(tool.configure(params));
}

TEST_CASE("BranchControlTool.configure: v2.7.0 switchValue/branchCases/logicInputs", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("switch", 0);
    params["switchValue"] = "1";
    params["branchCases"] = "case1:tool1;case2:tool2";
    params["logicInputs"] = "n1:true,n2:false";
    REQUIRE(tool.configure(params));
    // serialize 后能读回
    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("switchValue").toString() == "1");
    REQUIRE(saved.value("branchCases").toString() == "case1:tool1;case2:tool2");
    REQUIRE(saved.value("logicInputs").toString() == "n1:true,n2:false");
}

TEST_CASE("BranchControlTool.configure: 重复 configure 释放旧 BranchNode", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams(">", 1)));
    BranchNode* old = tool.branchNode();
    REQUIRE(old != nullptr);
    // 再次 configure，旧节点应被释放
    REQUIRE(tool.configure(makeBranchParams("<", 2)));
    BranchNode* now = tool.branchNode();
    REQUIRE(now != nullptr);
    REQUIRE(now->conditionOp == "<");
    // 旧指针不应再被使用（仅校验新节点正确）
    REQUIRE(now != old);
}

// =====================================================
// 基本比较运算（legacy 算子：==/!=/>/>=/</<=/ok/ng）
// 这些通过 BranchNode::evaluate 实现
// 注意：execute 调用 evaluateCondition 时 result.score=0.0, result.data 为空
// =====================================================

TEST_CASE("BranchControlTool.execute: '>' 5 → false（0>5）", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams(">", 5)));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ok);
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

TEST_CASE("BranchControlTool.execute: '<' 5 → true（0<5）", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams("<", 5)));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: '>=' 0 → true（0>=0）", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams(">=", 0)));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: '<=' 0 → true（0<=0）", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams("<=", 0)));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: '==' 0.0 → true", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams("==", 0.0)));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: '==' 5.0 → false", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams("==", 5.0)));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

TEST_CASE("BranchControlTool.execute: '!=' 5.0 → true", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams("!=", 5.0)));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: 'ok' → true（result.ok=true）", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams("ok", QJsonValue::Null)));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: 'ng' → false（result.ok=true）", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams("ng", QJsonValue::Null)));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

// =====================================================
// P1-4c 扩展：ge/le/ne/contains/startswith/endswith/in
// 注意：左值默认为 "0"（result.data 为空时 leftStr = QString::number(result.score=0)）
// =====================================================

TEST_CASE("BranchControlTool.execute: 'ge' 5 → false（0>=5 false）", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams("ge", 5)));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

TEST_CASE("BranchControlTool.execute: 'ge' 0 → true（0>=0 true）", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams("ge", 0)));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: 'le' 5 → true（0<=5 true）", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams("le", 5)));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: 'ne' 5 → true（0!=5）", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams("ne", 5)));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: 'ne' 字符串比较", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    // conditionValue 为字符串 "abc"，无法转 double，走字符串比较
    // 左值 "0" != "abc" → true
    REQUIRE(tool.configure(makeBranchParams("ne", QString("abc"))));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: 'contains' 子串匹配", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    // 左值 "0"，contains "0" → true
    REQUIRE(tool.configure(makeBranchParams("contains", QString("0"))));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: 'contains' 不匹配返回 false", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    // 左值 "0"，contains "x" → false
    REQUIRE(tool.configure(makeBranchParams("contains", QString("x"))));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

TEST_CASE("BranchControlTool.execute: 'startswith' 前缀匹配", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams("startswith", QString("0"))));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: 'endswith' 后缀匹配", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams("endswith", QString("0"))));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: 'in' 列表查找命中", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    // 左值 "0"，in "0,1,2" → true
    REQUIRE(tool.configure(makeBranchParams("in", QString("0,1,2"))));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: 'in' 列表查找未命中", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    // 左值 "0"，in "1,2,3" → false
    REQUIRE(tool.configure(makeBranchParams("in", QString("1,2,3"))));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

TEST_CASE("BranchControlTool.execute: 'in' 分号分隔列表", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    // 分号分隔也应支持
    REQUIRE(tool.configure(makeBranchParams("in", QString("1;0;2"))));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

// =====================================================
// v2.7.0 逻辑运算：AND/OR/NOT/XOR
// 通过 logicInputs + result.data["logicResults"] 间接测试
// =====================================================

TEST_CASE("BranchControlTool.execute: AND 全 true → true", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("and", QJsonValue::Null);
    params["logicInputs"] = "n1:true,n2:true";
    REQUIRE(tool.configure(params));

    ToolResult result;
    // 预填充 logicResults（模拟上游传入）
    QJsonObject logicResults;
    logicResults["n1"] = true;
    logicResults["n2"] = true;
    result.data["logicResults"] = logicResults;

    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
    // logicInputs 配置后，ports 应写入 logicInputs 和 logicOp
    REQUIRE(result.ports.contains("logicInputs"));
    REQUIRE(result.ports.value("logicOp").toString() == "and");
}

TEST_CASE("BranchControlTool.execute: AND 有 false → false", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("and", QJsonValue::Null);
    params["logicInputs"] = "n1:true,n2:false";
    REQUIRE(tool.configure(params));

    ToolResult result;
    QJsonObject logicResults;
    logicResults["n1"] = true;
    logicResults["n2"] = false;
    result.data["logicResults"] = logicResults;

    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

TEST_CASE("BranchControlTool.execute: AND 缺少 logicResults → false", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("and", QJsonValue::Null);
    params["logicInputs"] = "n1:true";
    REQUIRE(tool.configure(params));

    ToolResult result;
    // 不预填充 logicResults → evaluateLogicInput 找不到 n1 → 返回 false
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

TEST_CASE("BranchControlTool.execute: AND 空 logicInputs → true（空真）", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("and", QJsonValue::Null);
    // logicInputs 为空（不设置）
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    // 空列表 AND → true（无输入需满足）
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: OR 任一 true → true", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("or", QJsonValue::Null);
    params["logicInputs"] = "n1:false,n2:true";
    REQUIRE(tool.configure(params));

    ToolResult result;
    QJsonObject logicResults;
    logicResults["n1"] = false;
    logicResults["n2"] = true;
    result.data["logicResults"] = logicResults;

    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: OR 全 false → false", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("or", QJsonValue::Null);
    params["logicInputs"] = "n1:false,n2:false";
    REQUIRE(tool.configure(params));

    ToolResult result;
    QJsonObject logicResults;
    logicResults["n1"] = false;
    logicResults["n2"] = false;
    result.data["logicResults"] = logicResults;

    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

TEST_CASE("BranchControlTool.execute: NOT 取反 true → false", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("not", QJsonValue::Null);
    params["logicInputs"] = "n1:true";
    REQUIRE(tool.configure(params));

    ToolResult result;
    QJsonObject logicResults;
    logicResults["n1"] = true;
    result.data["logicResults"] = logicResults;

    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

TEST_CASE("BranchControlTool.execute: NOT 取反 false → true", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("not", QJsonValue::Null);
    params["logicInputs"] = "n1:true";
    REQUIRE(tool.configure(params));

    ToolResult result;
    QJsonObject logicResults;
    logicResults["n1"] = false;   // 实际为 false，期望 true → evaluateLogicInput=false → NOT=true
    result.data["logicResults"] = logicResults;

    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: NOT 空 logicInputs → true", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("not", QJsonValue::Null);
    // logicInputs 为空
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    // 空输入 NOT → true
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: XOR 不一致 → true", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("xor", QJsonValue::Null);
    params["logicInputs"] = "n1:true,n2:false";
    REQUIRE(tool.configure(params));

    ToolResult result;
    QJsonObject logicResults;
    logicResults["n1"] = true;
    logicResults["n2"] = false;
    result.data["logicResults"] = logicResults;

    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
}

TEST_CASE("BranchControlTool.execute: XOR 一致 → false", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("xor", QJsonValue::Null);
    params["logicInputs"] = "n1:true,n2:true";
    REQUIRE(tool.configure(params));

    ToolResult result;
    QJsonObject logicResults;
    logicResults["n1"] = true;
    logicResults["n2"] = true;
    result.data["logicResults"] = logicResults;

    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

TEST_CASE("BranchControlTool.execute: XOR 输入不足 2 个 → false", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("xor", QJsonValue::Null);
    params["logicInputs"] = "n1:true";   // 仅 1 个输入
    REQUIRE(tool.configure(params));

    ToolResult result;
    QJsonObject logicResults;
    logicResults["n1"] = true;
    result.data["logicResults"] = logicResults;

    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

TEST_CASE("BranchControlTool.execute: 逻辑运算 expectedBool 支持 '1'/'0'", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("and", QJsonValue::Null);
    params["logicInputs"] = "n1:1,n2:0";   // 用 1/0 代替 true/false
    REQUIRE(tool.configure(params));

    ToolResult result;
    QJsonObject logicResults;
    logicResults["n1"] = true;   // 实际 true，期望 1=true → 匹配
    logicResults["n2"] = true;   // 实际 true，期望 0=false → 不匹配
    result.data["logicResults"] = logicResults;

    REQUIRE(tool.execute(makeTestImage(), result));
    // n2 期望 false 但实际 true → AND=false
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

// =====================================================
// v2.7.0 switch 多分支选择
// =====================================================

TEST_CASE("BranchControlTool.execute: switch 有 switchValue+branchCases → true", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("switch", QJsonValue::Null);
    params["switchValue"] = "1";
    params["branchCases"] = "case0:tool0;case1:tool1";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == true);
    // switchValue 配置后写入 ports
    REQUIRE(result.ports.contains("switchValue"));
    REQUIRE(result.ports.value("switchValue").toString() == "1");
    REQUIRE(result.ports.contains("branchCases"));
}

TEST_CASE("BranchControlTool.execute: switch 缺少 switchValue → false", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("switch", QJsonValue::Null);
    // 不设置 switchValue
    params["branchCases"] = "case0:tool0";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    // switchValue 为空 → evaluateCondition 返回 false
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

TEST_CASE("BranchControlTool.execute: switch 缺少 branchCases → false", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("switch", QJsonValue::Null);
    params["switchValue"] = "1";
    // 不设置 branchCases
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("conditionMet").toBool() == false);
}

// =====================================================
// execute: ports/data 输出完整性
// =====================================================

TEST_CASE("BranchControlTool.execute: ports 写入 conditionMet 和 operation", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams(">", 5)));
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.contains("conditionMet"));
    REQUIRE(result.ports.contains("operation"));
    REQUIRE(result.ports.value("operation").toString() == ">");
    // data 通道也有
    REQUIRE(result.data.contains("conditionMet"));
    REQUIRE(result.data.contains("operation"));
    REQUIRE(result.data.contains("conditionValue"));
}

TEST_CASE("BranchControlTool.execute: overlay 透传输入图像", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    REQUIRE(tool.configure(makeBranchParams(">", 5)));
    const cv::Mat img = makeTestImage();
    ToolResult result;
    REQUIRE(tool.execute(img, result));
    REQUIRE_FALSE(result.overlayImage.empty());
    REQUIRE(result.overlayImage.size() == img.size());
}

TEST_CASE("BranchControlTool.execute: 无 branchNode 时仍透传图像", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    // 不调用 configure → 无 branchNode
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ok);
    REQUIRE_FALSE(result.overlayImage.empty());
    // 不应写入 conditionMet
    REQUIRE_FALSE(result.ports.contains("conditionMet"));
}

// =====================================================
// setBranchNode: 外部注入 BranchNode
// =====================================================

TEST_CASE("BranchControlTool.setBranchNode: 外部注入", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    BranchNode* node = new BranchNode();
    node->id = "ext1";
    node->sourceToolId = "src1";
    node->conditionOp = ">";
    node->conditionValue = 5;
    node->trueBranchToolIds.append("t1");
    node->falseBranchToolIds.append("f1");
    tool.setBranchNode(node);
    REQUIRE(tool.branchNode() == node);
    REQUIRE(tool.branchNode()->isValid());
}

TEST_CASE("BranchControlTool.setBranchNode: 重复注入释放旧节点", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    BranchNode* n1 = new BranchNode();
    n1->id = "n1"; n1->sourceToolId = "s"; n1->conditionOp = ">";
    n1->trueBranchToolIds.append("t");
    tool.setBranchNode(n1);
    BranchNode* n2 = new BranchNode();
    n2->id = "n2"; n2->sourceToolId = "s"; n2->conditionOp = "<";
    n2->trueBranchToolIds.append("t");
    tool.setBranchNode(n2);
    REQUIRE(tool.branchNode() == n2);
    REQUIRE(tool.branchNode()->conditionOp == "<");
}

// =====================================================
// 端口元数据
// =====================================================

TEST_CASE("BranchControlTool.outputPorts: 声明 conditionMet 和 operation", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    const QList<PortDescriptor> ports = tool.outputPorts();
    REQUIRE(ports.size() == 2);
    REQUIRE(ports.at(0).name == "conditionMet");
    REQUIRE(ports.at(0).type == PortType::Bool);
    REQUIRE(ports.at(0).dir == PortDirection::Out);
    REQUIRE(ports.at(1).name == "operation");
    REQUIRE(ports.at(1).type == PortType::String);
}

TEST_CASE("BranchControlTool.inputPorts: 声明 image 端口", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    const QList<PortDescriptor> ports = tool.inputPorts();
    REQUIRE(ports.size() == 1);
    REQUIRE(ports.at(0).name == "image");
    REQUIRE(ports.at(0).type == PortType::Image);
    REQUIRE(ports.at(0).dir == PortDirection::In);
}

// =====================================================
// serialize / deserialize 往返
// =====================================================

TEST_CASE("BranchControlTool.serialize/deserialize: 基本比较往返", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    const QJsonObject params = makeBranchParams(">", 5, "b1", "s1");
    REQUIRE(tool.configure(params));

    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("type").toString() == "BranchControl");
    REQUIRE(saved.value("id").toString() == "b1");
    REQUIRE(saved.value("op").toString() == ">");
    REQUIRE(saved.value("source").toString() == "s1");

    BranchControlTool restored;
    REQUIRE(restored.deserialize(saved));
    REQUIRE(restored.branchNode() != nullptr);
    REQUIRE(restored.branchNode()->isValid());
    REQUIRE(restored.branchNode()->conditionOp == ">");
    REQUIRE(restored.branchNode()->conditionValue.toInt() == 5);
}

TEST_CASE("BranchControlTool.serialize/deserialize: v2.7.0 switch + 逻辑运算参数往返", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject params = makeBranchParams("and", QJsonValue::Null);
    params["switchValue"] = "2";
    params["branchCases"] = "case0:t0;case1:t1;case2:t2";
    params["logicInputs"] = "n1:true,n2:false";
    REQUIRE(tool.configure(params));

    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("switchValue").toString() == "2");
    REQUIRE(saved.value("branchCases").toString() == "case0:t0;case1:t1;case2:t2");
    REQUIRE(saved.value("logicInputs").toString() == "n1:true,n2:false");

    BranchControlTool restored;
    REQUIRE(restored.deserialize(saved));
    REQUIRE(restored.serialize().value("switchValue").toString() == "2");
    REQUIRE(restored.serialize().value("logicInputs").toString() == "n1:true,n2:false");
}

TEST_CASE("BranchControlTool.deserialize: conditionOp/conditionValue 映射", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject data;
    data["id"] = "b1";
    data["source"] = "s1";
    data["conditionOp"] = "<";   // 元数据字段名
    data["conditionValue"] = 10;
    data["trueBranch"] = QJsonArray{"t1"};
    data["falseBranch"] = QJsonArray{"f1"};
    REQUIRE(tool.deserialize(data));
    // 映射后 BranchNode 字段正确
    REQUIRE(tool.branchNode()->conditionOp == "<");
    REQUIRE(tool.branchNode()->conditionValue.toInt() == 10);
}

TEST_CASE("BranchControlTool.deserialize: trueBranch 字符串形式兼容", "[Vision][BranchControlTool]") {
    // 历史方案数据可能以字符串形式存储 trueBranch
    BranchControlTool tool;
    QJsonObject data;
    data["id"] = "b1";
    data["source"] = "s1";
    data["op"] = ">";
    data["value"] = 5;
    data["trueBranch"] = QString("t1,t2,t3");   // 字符串形式
    data["falseBranch"] = QString("f1");
    REQUIRE(tool.deserialize(data));
    REQUIRE(tool.branchNode()->isValid());
    REQUIRE(tool.branchNode()->trueBranchToolIds.size() == 3);
    REQUIRE(tool.branchNode()->falseBranchToolIds.size() == 1);
}

TEST_CASE("BranchControlTool.deserialize: 缺少 trueBranch/falseBranch 导致 invalid", "[Vision][BranchControlTool]") {
    BranchControlTool tool;
    QJsonObject data;
    data["id"] = "b1";
    data["source"] = "s1";
    data["op"] = ">";
    data["value"] = 5;
    // 不提供 trueBranch/falseBranch
    REQUIRE_FALSE(tool.deserialize(data));
}
