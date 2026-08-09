// VariableTool 单元测试（v2.7.0 升级覆盖）
//
// 覆盖范围：
//  - 基本算术运算：add/sub/mul/div/mod
//  - 字符串运算：concat / format
//  - v2.7.0 新增：get/set/define/delete 操作（接入 VariableManager）
//  - define 各类型变量：int/double/string/bool/roi/region/points
//  - setVariableManager 注入
//  - 序列化/反序列化往返
//  - 输入/输出端口元数据
//
// 测试框架：项目自制 catch2_minimal.hpp

#include "../catch2/catch2_minimal.hpp"
#include "Vision/VariableTool.h"
#include "Core/VariableManager.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <QVariantList>
#include <QString>
#include <opencv2/core.hpp>

using namespace QDV;

// 允许的浮点误差
static constexpr double EPS = 1e-9;

// 构造一个非空测试图像（100x100 BGR 灰色）
static cv::Mat makeTestImage() {
    return cv::Mat(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
}

// =====================================================
// 基本算术运算：add/sub/mul/div/mod
// =====================================================

TEST_CASE("VariableTool: type() 返回 'Variable'", "[Vision][VariableTool]") {
    VariableTool tool;
    REQUIRE(tool.type() == "Variable");
}

TEST_CASE("VariableTool.add: 数值加法", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "add";
    params["operand1"]  = "3.5";
    params["operand2"]  = "4.5";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ok);
    REQUIRE_NEAR(result.ports.value("result").toDouble(), 8.0, EPS);
    // data 通道也有 result
    REQUIRE_NEAR(result.data.value("result").toVariant().toDouble(), 8.0, EPS);
    REQUIRE(result.data.value("operation").toString() == "add");
}

TEST_CASE("VariableTool.sub: 数值减法", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "sub";
    params["operand1"]  = "10";
    params["operand2"]  = "3";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE_NEAR(result.ports.value("result").toDouble(), 7.0, EPS);
}

TEST_CASE("VariableTool.mul: 数值乘法", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "mul";
    params["operand1"]  = "6";
    params["operand2"]  = "7";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE_NEAR(result.ports.value("result").toDouble(), 42.0, EPS);
}

TEST_CASE("VariableTool.div: 数值除法", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "div";
    params["operand1"]  = "20";
    params["operand2"]  = "4";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE_NEAR(result.ports.value("result").toDouble(), 5.0, EPS);
}

TEST_CASE("VariableTool.div: 除零返回失败", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "div";
    params["operand1"]  = "10";
    params["operand2"]  = "0";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE_FALSE(tool.execute(makeTestImage(), result));
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.data.contains("error"));
}

TEST_CASE("VariableTool.mod: 浮点取模", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "mod";
    params["operand1"]  = "10.5";
    params["operand2"]  = "3";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    // fmod(10.5, 3) = 1.5
    REQUIRE_NEAR(result.ports.value("result").toDouble(), 1.5, EPS);
}

TEST_CASE("VariableTool.mod: 取模除零返回失败", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "mod";
    params["operand1"]  = "10";
    params["operand2"]  = "0";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE_FALSE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.contains("error"));
}

TEST_CASE("VariableTool: operand1 非数值返回失败", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "add";
    params["operand1"]  = "abc";   // 非数值
    params["operand2"]  = "1";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE_FALSE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.contains("error"));
}

TEST_CASE("VariableTool: operand2 非数值返回失败", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "add";
    params["operand1"]  = "1";
    params["operand2"]  = "xyz";   // 非数值
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE_FALSE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.contains("error"));
}

TEST_CASE("VariableTool: 科学计数法解析", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "mul";
    params["operand1"]  = "1e2";   // 100
    params["operand2"]  = "2e-1";  // 0.2
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE_NEAR(result.ports.value("result").toDouble(), 20.0, EPS);
}

// =====================================================
// 字符串运算：concat / format
// =====================================================

TEST_CASE("VariableTool.concat: 字符串拼接", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "concat";
    params["operand1"]  = "Hello, ";
    params["operand2"]  = "World!";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("result").toString() == "Hello, World!");
}

TEST_CASE("VariableTool.format: %.2f 格式化数值", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "format";
    params["operand1"]  = "3.14159";
    params["operand2"]  = "0";
    params["format"]    = "%.2f";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("result").toString() == "3.14");
}

TEST_CASE("VariableTool.format: %d 格式化整数", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "format";
    params["operand1"]  = "42";
    params["operand2"]  = "0";
    params["format"]    = "Count=%d";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("result").toString() == "Count=42");
}

TEST_CASE("VariableTool.format: %s 格式化字符串", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "format";
    params["operand1"]  = "abc";
    params["operand2"]  = "def";
    params["format"]    = "%s-%s";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("result").toString() == "abc-def");
}

// =====================================================
// outputVar：自定义输出端口名
// =====================================================

TEST_CASE("VariableTool.outputVar: 自定义输出变量名", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "add";
    params["operand1"]  = "1";
    params["operand2"]  = "2";
    params["outputVar"] = "mySum";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    // 同时写入 result 端口和 mySum 端口
    REQUIRE(result.ports.contains("result"));
    REQUIRE(result.ports.contains("mySum"));
    REQUIRE_NEAR(result.ports.value("mySum").toDouble(), 3.0, EPS);
    REQUIRE(result.data.value("outputVar").toString() == "mySum");
}

TEST_CASE("VariableTool.outputVar: 空时默认 'result'", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "add";
    params["operand1"]  = "1";
    params["operand2"]  = "2";
    params["outputVar"] = "";  // 空 → 默认 result
    REQUIRE(tool.configure(params));
    // 仅 result 端口（不会重复写）
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.contains("result"));
    REQUIRE(result.ports.size() == 1);
}

// =====================================================
// 空输入图像：overlay 透传行为
// =====================================================

TEST_CASE("VariableTool: 空输入图像仍能执行（overlay 为空）", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "add";
    params["operand1"]  = "1";
    params["operand2"]  = "2";
    REQUIRE(tool.configure(params));

    cv::Mat empty;
    ToolResult result;
    REQUIRE(tool.execute(empty, result));
    REQUIRE(result.ok);
    // 空输入 → overlay 不被赋值
    REQUIRE(result.overlayImage.empty());
}

TEST_CASE("VariableTool: 非空输入图像透传到 overlay", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "add";
    params["operand1"]  = "1";
    params["operand2"]  = "2";
    REQUIRE(tool.configure(params));

    const cv::Mat img = makeTestImage();
    ToolResult result;
    REQUIRE(tool.execute(img, result));
    REQUIRE_FALSE(result.overlayImage.empty());
    REQUIRE(result.overlayImage.size() == img.size());
}

// =====================================================
// v2.7.0：get/set/define/delete 操作
// =====================================================

TEST_CASE("VariableTool.get: 读取全局变量", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    vm.createVariable("v1", "int", 42);
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "get";
    params["operand1"]  = "v1";   // 变量名
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("result").toInt() == 42);
}

TEST_CASE("VariableTool.get: 未注入 VariableManager 返回失败", "[Vision][VariableTool]") {
    VariableTool tool;   // 未调用 setVariableManager
    QJsonObject params;
    params["operation"] = "get";
    params["operand1"]  = "v1";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE_FALSE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.value("error").toString().contains("VariableManager"));
}

TEST_CASE("VariableTool.get: 变量未定义返回失败", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "get";
    params["operand1"]  = "nope";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE_FALSE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.contains("error"));
}

TEST_CASE("VariableTool.set: 写入已存在的全局变量（数值自动转换）", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    vm.createVariable("v1", "int", 0);
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "set";
    params["operand1"]  = "v1";
    params["operand2"]  = "99";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    // 验证变量已被更新
    REQUIRE(vm.value("v1").toInt() == 99);
    // result.ports 也写入 set 的值
    REQUIRE_NEAR(result.ports.value("result").toDouble(), 99.0, EPS);
}

TEST_CASE("VariableTool.set: 字符串值写入", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    vm.createVariable("s1", "string", QString(""));
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "set";
    params["operand1"]  = "s1";
    params["operand2"]  = "hello";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(vm.value("s1").toString() == "hello");
}

TEST_CASE("VariableTool.set: 未注入 VariableManager 返回失败", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "set";
    params["operand1"]  = "v1";
    params["operand2"]  = "1";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE_FALSE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.value("error").toString().contains("VariableManager"));
}

TEST_CASE("VariableTool.set: 变量不存在返回失败", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "set";
    params["operand1"]  = "nope";
    params["operand2"]  = "1";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE_FALSE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.contains("error"));
}

TEST_CASE("VariableTool.define: int 类型变量", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "define";
    params["operand1"]  = "newInt";   // 变量名
    params["operand2"]  = "int";      // 类型
    params["format"]    = "42";       // 初始值（复用 format 字段）
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(vm.exists("newInt"));
    REQUIRE(vm.value("newInt").toInt() == 42);
    // result.ports 写入默认值
    REQUIRE(result.ports.value("result").toInt() == 42);
}

TEST_CASE("VariableTool.define: double 类型变量", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "define";
    params["operand1"]  = "newDouble";
    params["operand2"]  = "double";
    params["format"]    = "3.14";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE_NEAR(vm.value("newDouble").toDouble(), 3.14, EPS);
}

TEST_CASE("VariableTool.define: string 类型变量", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "define";
    params["operand1"]  = "newStr";
    params["operand2"]  = "string";
    params["format"]    = "hello";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(vm.value("newStr").toString() == "hello");
}

TEST_CASE("VariableTool.define: bool 类型变量", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "define";
    params["operand1"]  = "newBool";
    params["operand2"]  = "bool";
    params["format"]    = "true";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(vm.value("newBool").toBool() == true);
}

TEST_CASE("VariableTool.define: roi 类型变量", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "define";
    params["operand1"]  = "newRoi";
    params["operand2"]  = "roi";
    params["format"]    = "";  // roi 用默认值
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    const QVariantMap roi = vm.value("newRoi").toMap();
    REQUIRE_FALSE(roi.isEmpty());
    // roi 默认 {x=0,y=0,w=100,h=100}
    REQUIRE_NEAR(roi.value("w").toDouble(), 100.0, EPS);
    REQUIRE_NEAR(roi.value("h").toDouble(), 100.0, EPS);
}

TEST_CASE("VariableTool.define: region 类型变量", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "define";
    params["operand1"]  = "newRegion";
    params["operand2"]  = "region";
    params["format"]    = "";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    // region 默认空列表
    REQUIRE(vm.value("newRegion").toList().isEmpty());
}

TEST_CASE("VariableTool.define: points 类型变量", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "define";
    params["operand1"]  = "newPoints";
    params["operand2"]  = "points";
    params["format"]    = "";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(vm.value("newPoints").toList().isEmpty());
}

TEST_CASE("VariableTool.define: 未注入 VariableManager 返回失败", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "define";
    params["operand1"]  = "x";
    params["operand2"]  = "int";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE_FALSE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.value("error").toString().contains("VariableManager"));
}

TEST_CASE("VariableTool.define: 重复定义返回失败", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    vm.createVariable("dup", "int", 0);
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "define";
    params["operand1"]  = "dup";  // 已存在
    params["operand2"]  = "int";
    params["format"]    = "1";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE_FALSE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.contains("error"));
}

TEST_CASE("VariableTool.delete: 删除全局变量", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    vm.createVariable("toDel", "int", 0);
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "delete";
    params["operand1"]  = "toDel";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE_FALSE(vm.exists("toDel"));
    // delete 操作 result 端口写入 true
    REQUIRE(result.ports.value("result").toBool() == true);
}

TEST_CASE("VariableTool.delete: 未注入 VariableManager 返回失败", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "delete";
    params["operand1"]  = "x";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE_FALSE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.value("error").toString().contains("VariableManager"));
}

TEST_CASE("VariableTool.delete: 变量不存在返回失败", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "delete";
    params["operand1"]  = "nope";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE_FALSE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.contains("error"));
}

// =====================================================
// configure: 非法 operation 回退 add
// =====================================================

TEST_CASE("VariableTool.configure: 非法 operation 回退 add", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "weirdOp";   // 非法
    params["operand1"]  = "1";
    params["operand2"]  = "2";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    // 回退 add → 1+2=3
    REQUIRE_NEAR(result.ports.value("result").toDouble(), 3.0, EPS);
    REQUIRE(result.data.value("operation").toString() == "add");
}

TEST_CASE("VariableTool.configure: 默认 operation 为 add", "[Vision][VariableTool]") {
    VariableTool tool;
    // 不设置 operation → 默认 add
    QJsonObject params;
    params["operand1"]  = "5";
    params["operand2"]  = "5";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE_NEAR(result.ports.value("result").toDouble(), 10.0, EPS);
}

// =====================================================
// 端口元数据
// =====================================================

TEST_CASE("VariableTool.outputPorts: 声明 result 端口", "[Vision][VariableTool]") {
    VariableTool tool;
    const QList<PortDescriptor> ports = tool.outputPorts();
    REQUIRE(ports.size() == 1);
    REQUIRE(ports.at(0).name == "result");
    REQUIRE(ports.at(0).type == PortType::Any);
    REQUIRE(ports.at(0).dir == PortDirection::Out);
}

TEST_CASE("VariableTool.inputPorts: 声明 image 端口", "[Vision][VariableTool]") {
    VariableTool tool;
    const QList<PortDescriptor> ports = tool.inputPorts();
    REQUIRE(ports.size() == 1);
    REQUIRE(ports.at(0).name == "image");
    REQUIRE(ports.at(0).type == PortType::Image);
    REQUIRE(ports.at(0).dir == PortDirection::In);
}

// =====================================================
// 序列化 / 反序列化 往返
// =====================================================

TEST_CASE("VariableTool.serialize/deserialize: 算术运算往返", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "mul";
    params["operand1"]  = "3";
    params["operand2"]  = "4";
    params["format"]    = "%.2f";
    params["outputVar"] = "product";
    REQUIRE(tool.configure(params));

    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("type").toString() == "Variable");
    REQUIRE(saved.value("operation").toString() == "mul");
    REQUIRE(saved.value("operand1").toString() == "3");
    REQUIRE(saved.value("operand2").toString() == "4");
    REQUIRE(saved.value("format").toString() == "%.2f");
    REQUIRE(saved.value("outputVar").toString() == "product");

    VariableTool restored;
    REQUIRE(restored.deserialize(saved));
    REQUIRE(restored.serialize().value("operation").toString() == "mul");
    REQUIRE(restored.serialize().value("operand1").toString() == "3");
    REQUIRE(restored.serialize().value("operand2").toString() == "4");
    REQUIRE(restored.serialize().value("outputVar").toString() == "product");

    // 反序列化后执行行为一致
    ToolResult result;
    REQUIRE(restored.execute(makeTestImage(), result));
    REQUIRE_NEAR(result.ports.value("product").toDouble(), 12.0, EPS);
}

TEST_CASE("VariableTool.serialize/deserialize: v2.7.0 get/set/define/delete 往返", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject params;
    params["operation"] = "define";
    params["operand1"]  = "myVar";
    params["operand2"]  = "int";
    params["format"]    = "100";
    REQUIRE(tool.configure(params));

    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("operation").toString() == "define");

    VariableTool restored;
    REQUIRE(restored.deserialize(saved));
    REQUIRE(restored.serialize().value("operation").toString() == "define");
    REQUIRE(restored.serialize().value("operand1").toString() == "myVar");
    REQUIRE(restored.serialize().value("operand2").toString() == "int");
    REQUIRE(restored.serialize().value("format").toString() == "100");
}

TEST_CASE("VariableTool.deserialize: outputVar 空时默认 'result'", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject data;
    data["type"] = "Variable";
    data["outputVar"] = "";  // 空
    REQUIRE(tool.deserialize(data));
    REQUIRE(tool.serialize().value("outputVar").toString() == "result");
}

TEST_CASE("VariableTool.deserialize: 非法 operation 被忽略（保持默认）", "[Vision][VariableTool]") {
    VariableTool tool;
    QJsonObject data;
    data["type"] = "Variable";
    data["operation"] = "weirdOp";   // 非法，被忽略
    REQUIRE(tool.deserialize(data));
    // 默认 operation 为 add
    REQUIRE(tool.serialize().value("operation").toString() == "add");
}

// =====================================================
// setVariableManager 注入
// =====================================================

TEST_CASE("VariableTool.setVariableManager: 注入后 get 可访问", "[Vision][VariableTool]") {
    VariableTool tool;
    VariableManager vm;
    vm.createVariable("g", "int", 7);
    tool.setVariableManager(&vm);

    QJsonObject params;
    params["operation"] = "get";
    params["operand1"]  = "g";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("result").toInt() == 7);
}
