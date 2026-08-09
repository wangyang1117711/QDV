// VariableManager 单元测试（v2.7.0 升级覆盖）
//
// 覆盖范围：
//  - CRUD：createVariable / setValue / value / variable / variables / removeVariable / clear
//  - 类型扩展：Int/Double/String/Bool/Roi/Region/Points
//  - 变量绑定解析：resolveBinding / resolveVariant / extractReferences
//  - 序列化：toJson / fromJson（全类型）
//  - 算子输出映射：registerOperatorOutput / unregisterOperatorOutput / updateOperatorOutputValues
//  - 线程安全：多线程并发 setValue/getValue（验证 QRecursiveMutex 保护）
//
// 测试框架：项目自制 catch2_minimal.hpp（提供 TEST_CASE/REQUIRE/CHECK 等宏）
// 注意：test_main.cpp 已创建 QApplication，本文件无需自行创建 QCoreApplication

#include "../catch2/catch2_minimal.hpp"
#include "Core/VariableManager.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>
#include <QVariantList>
#include <QRectF>
#include <QPointF>
#include <QList>
#include <QSet>

#include <thread>
#include <vector>
#include <atomic>

using namespace QDV;

// =====================================================
// 辅助：构造一个 ROI 变量值 {x,y,w,h}
// =====================================================
static QVariantMap makeRoiMap(double x, double y, double w, double h) {
    QVariantMap m;
    m["x"] = x;
    m["y"] = y;
    m["w"] = w;
    m["h"] = h;
    return m;
}

// =====================================================
// 类型工具
// =====================================================

TEST_CASE("VariableManager.typeToString: 全类型枚举映射", "[Core][VariableManager]") {
    REQUIRE(VariableManager::typeToString(VariableManager::Type::Int)    == "int");
    REQUIRE(VariableManager::typeToString(VariableManager::Type::Double) == "double");
    REQUIRE(VariableManager::typeToString(VariableManager::Type::String) == "string");
    REQUIRE(VariableManager::typeToString(VariableManager::Type::Bool)   == "bool");
    REQUIRE(VariableManager::typeToString(VariableManager::Type::Roi)    == "roi");
    REQUIRE(VariableManager::typeToString(VariableManager::Type::Region) == "region");
    REQUIRE(VariableManager::typeToString(VariableManager::Type::Points) == "points");
}

TEST_CASE("VariableManager.stringToType: 标准名与别名解析", "[Core][VariableManager]") {
    bool ok = false;
    // 标准名
    REQUIRE(VariableManager::stringToType("int", &ok)    == VariableManager::Type::Int);    REQUIRE(ok);
    REQUIRE(VariableManager::stringToType("double", &ok) == VariableManager::Type::Double); REQUIRE(ok);
    REQUIRE(VariableManager::stringToType("string", &ok) == VariableManager::Type::String); REQUIRE(ok);
    REQUIRE(VariableManager::stringToType("bool", &ok)   == VariableManager::Type::Bool);   REQUIRE(ok);
    REQUIRE(VariableManager::stringToType("roi", &ok)    == VariableManager::Type::Roi);    REQUIRE(ok);
    REQUIRE(VariableManager::stringToType("region", &ok) == VariableManager::Type::Region); REQUIRE(ok);
    REQUIRE(VariableManager::stringToType("points", &ok) == VariableManager::Type::Points); REQUIRE(ok);
    // 别名
    REQUIRE(VariableManager::stringToType("integer", &ok) == VariableManager::Type::Int);    REQUIRE(ok);
    REQUIRE(VariableManager::stringToType("float", &ok)   == VariableManager::Type::Double); REQUIRE(ok);
    REQUIRE(VariableManager::stringToType("real", &ok)    == VariableManager::Type::Double); REQUIRE(ok);
    REQUIRE(VariableManager::stringToType("str", &ok)     == VariableManager::Type::String); REQUIRE(ok);
    REQUIRE(VariableManager::stringToType("text", &ok)    == VariableManager::Type::String); REQUIRE(ok);
    REQUIRE(VariableManager::stringToType("boolean", &ok) == VariableManager::Type::Bool);   REQUIRE(ok);
    REQUIRE(VariableManager::stringToType("rect", &ok)    == VariableManager::Type::Roi);    REQUIRE(ok);
    REQUIRE(VariableManager::stringToType("point[]", &ok) == VariableManager::Type::Points); REQUIRE(ok);
    // 未知类型：返回 String 且 ok=false
    REQUIRE(VariableManager::stringToType("unknown", &ok) == VariableManager::Type::String); REQUIRE_FALSE(ok);
}

TEST_CASE("VariableManager.isValidName: 命名规范校验", "[Core][VariableManager]") {
    // 合法
    REQUIRE(VariableManager::isValidName("x"));
    REQUIRE(VariableManager::isValidName("_x"));
    REQUIRE(VariableManager::isValidName("abc123"));
    REQUIRE(VariableManager::isValidName("a_b_c"));
    REQUIRE(VariableManager::isValidName("A_B_C_1"));
    // 非法
    REQUIRE_FALSE(VariableManager::isValidName(""));
    REQUIRE_FALSE(VariableManager::isValidName("1abc"));     // 首字符数字
    REQUIRE_FALSE(VariableManager::isValidName("ab-c"));     // 含连字符
    REQUIRE_FALSE(VariableManager::isValidName("ab.c"));     // 含点
    REQUIRE_FALSE(VariableManager::isValidName("ab c"));     // 含空格
    REQUIRE_FALSE(VariableManager::isValidName("$abc"));     // 含特殊符号
}

// =====================================================
// createVariable：各类型创建 + 非法名/重复名/未知类型
// =====================================================

TEST_CASE("VariableManager.createVariable: int/double/string/bool 各类型创建", "[Core][VariableManager]") {
    VariableManager vm;
    REQUIRE(vm.createVariable("i", "int", 42));
    REQUIRE(vm.createVariable("d", "double", 3.14));
    REQUIRE(vm.createVariable("s", "string", QString("hello")));
    REQUIRE(vm.createVariable("b", "bool", true));
    REQUIRE(vm.count() == 4);
    // 默认值在未提供时按类型回退
    VariableManager vm2;
    REQUIRE(vm2.createVariable("i2", "int", QVariant()));
    REQUIRE(vm2.value("i2").toInt() == 0);
    REQUIRE(vm2.createVariable("d2", "double", QVariant()));
    REQUIRE_NEAR(vm2.value("d2").toDouble(), 0.0, 1e-9);
    REQUIRE(vm2.createVariable("b2", "bool", QVariant()));
    REQUIRE(vm2.value("b2").toBool() == false);
}

TEST_CASE("VariableManager.createVariable: roi 类型创建", "[Core][VariableManager]") {
    VariableManager vm;
    const QVariantMap roi = makeRoiMap(10.0, 20.0, 100.0, 50.0);
    REQUIRE(vm.createVariable("r", "roi", roi));
    const QVariant v = vm.value("r");
    REQUIRE(v.canConvert<QVariantMap>());
    const QVariantMap m = v.toMap();
    REQUIRE_NEAR(m.value("x").toDouble(), 10.0, 1e-9);
    REQUIRE_NEAR(m.value("y").toDouble(), 20.0, 1e-9);
    REQUIRE_NEAR(m.value("w").toDouble(), 100.0, 1e-9);
    REQUIRE_NEAR(m.value("h").toDouble(), 50.0, 1e-9);
}

TEST_CASE("VariableManager.createVariable: roi 空值默认 {x=0,y=0,w=100,h=100}", "[Core][VariableManager]") {
    VariableManager vm;
    REQUIRE(vm.createVariable("r", "roi", QVariant()));
    const QVariantMap m = vm.value("r").toMap();
    REQUIRE_FALSE(m.isEmpty());
    REQUIRE_NEAR(m.value("x").toDouble(), 0.0, 1e-9);
    REQUIRE_NEAR(m.value("y").toDouble(), 0.0, 1e-9);
    REQUIRE_NEAR(m.value("w").toDouble(), 100.0, 1e-9);
    REQUIRE_NEAR(m.value("h").toDouble(), 100.0, 1e-9);
}

TEST_CASE("VariableManager.createVariable: region 类型创建", "[Core][VariableManager]") {
    VariableManager vm;
    QVariantList polygon;
    polygon.append(0); polygon.append(0);
    polygon.append(10); polygon.append(0);
    polygon.append(10); polygon.append(10);
    polygon.append(0); polygon.append(10);
    REQUIRE(vm.createVariable("reg", "region", polygon));
    const QVariantList got = vm.value("reg").toList();
    REQUIRE(got.size() == 8);
    REQUIRE(got.at(0).toInt() == 0);
    REQUIRE(got.at(2).toInt() == 10);
}

TEST_CASE("VariableManager.createVariable: points 类型创建", "[Core][VariableManager]") {
    VariableManager vm;
    // 点集：QVariantList of {x,y} Map
    QVariantList ptList;
    QVariantMap p1; p1["x"] = 1.0; p1["y"] = 2.0;
    QVariantMap p2; p2["x"] = 3.0; p2["y"] = 4.0;
    ptList.append(p1); ptList.append(p2);
    REQUIRE(vm.createVariable("pts", "points", ptList));
    const QVariantList got = vm.value("pts").toList();
    REQUIRE(got.size() == 2);
    REQUIRE_NEAR(got.at(0).toMap().value("x").toDouble(), 1.0, 1e-9);
    REQUIRE_NEAR(got.at(1).toMap().value("y").toDouble(), 4.0, 1e-9);
}

TEST_CASE("VariableManager.createVariable: 非法名被拒绝", "[Core][VariableManager]") {
    VariableManager vm;
    REQUIRE_FALSE(vm.createVariable("", "int", 0));       // 空名
    REQUIRE_FALSE(vm.createVariable("1abc", "int", 0));   // 首字符数字
    REQUIRE_FALSE(vm.createVariable("ab-c", "int", 0));   // 含连字符
    REQUIRE(vm.count() == 0);
}

TEST_CASE("VariableManager.createVariable: 重复名被拒绝", "[Core][VariableManager]") {
    VariableManager vm;
    REQUIRE(vm.createVariable("x", "int", 1));
    REQUIRE_FALSE(vm.createVariable("x", "int", 2));
    REQUIRE(vm.count() == 1);
    // 原值未被覆盖
    REQUIRE(vm.value("x").toInt() == 1);
}

TEST_CASE("VariableManager.createVariable: 未知类型被拒绝", "[Core][VariableManager]") {
    VariableManager vm;
    REQUIRE_FALSE(vm.createVariable("x", "unknownType", 0));
    REQUIRE(vm.count() == 0);
}

// =====================================================
// setValue：各类型赋值 + 类型不匹配 + 不存在变量
// =====================================================

TEST_CASE("VariableManager.setValue: int/double/string/bool 赋值", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("i", "int", 1);
    vm.createVariable("d", "double", 1.0);
    vm.createVariable("s", "string", QString("a"));
    vm.createVariable("b", "bool", false);

    REQUIRE(vm.setValue("i", 99));
    REQUIRE(vm.value("i").toInt() == 99);
    REQUIRE(vm.setValue("d", 2.5));
    REQUIRE_NEAR(vm.value("d").toDouble(), 2.5, 1e-9);
    REQUIRE(vm.setValue("s", QString("world")));
    REQUIRE(vm.value("s").toString() == "world");
    REQUIRE(vm.setValue("b", true));
    REQUIRE(vm.value("b").toBool() == true);
}

TEST_CASE("VariableManager.setValue: roi 赋值", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("r", "roi", makeRoiMap(0, 0, 10, 10));
    REQUIRE(vm.setValue("r", makeRoiMap(5, 5, 50, 60)));
    const QVariantMap m = vm.value("r").toMap();
    REQUIRE_NEAR(m.value("x").toDouble(), 5.0, 1e-9);
    REQUIRE_NEAR(m.value("w").toDouble(), 50.0, 1e-9);
}

TEST_CASE("VariableManager.setValue: roi 空值被拒绝（类型不匹配）", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("r", "roi", makeRoiMap(0, 0, 10, 10));
    // toMap() 对非 Map 类型返回空，触发类型不匹配
    REQUIRE_FALSE(vm.setValue("r", QVariant(123)));
    // 原值未变
    REQUIRE_NEAR(vm.value("r").toMap().value("w").toDouble(), 10.0, 1e-9);
}

TEST_CASE("VariableManager.setValue: region/points 赋值", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("reg", "region", QVariantList());
    vm.createVariable("pts", "points", QVariantList());

    QVariantList polygon; polygon.append(1); polygon.append(2); polygon.append(3); polygon.append(4);
    REQUIRE(vm.setValue("reg", polygon));
    REQUIRE(vm.value("reg").toList().size() == 4);

    QVariantMap p; p["x"] = 1.0; p["y"] = 2.0;
    QVariantList pts; pts.append(p);
    REQUIRE(vm.setValue("pts", pts));
    REQUIRE(vm.value("pts").toList().size() == 1);
}

TEST_CASE("VariableManager.setValue: int 类型不匹配被拒绝", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("i", "int", 0);
    // QVariant(无法转 int) 不存在；这里用 List 模拟不可转换值
    REQUIRE_FALSE(vm.setValue("i", QVariantList()));
    REQUIRE(vm.value("i").toInt() == 0);
}

TEST_CASE("VariableManager.setValue: double 类型不匹配被拒绝", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("d", "double", 0.0);
    REQUIRE_FALSE(vm.setValue("d", QVariantList()));
    REQUIRE_NEAR(vm.value("d").toDouble(), 0.0, 1e-9);
}

TEST_CASE("VariableManager.setValue: bool 类型不匹配被拒绝", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("b", "bool", false);
    REQUIRE_FALSE(vm.setValue("b", QVariantList()));
    REQUIRE(vm.value("b").toBool() == false);
}

TEST_CASE("VariableManager.setValue: 不存在的变量返回 false", "[Core][VariableManager]") {
    VariableManager vm;
    REQUIRE_FALSE(vm.setValue("nope", 1));
}

TEST_CASE("VariableManager.setValue: 相同值不重复发信号（值未变化）", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("i", "int", 5);
    // 相同值：返回 true 但不发 valueChanged 信号（这里仅校验返回值与值一致性）
    REQUIRE(vm.setValue("i", 5));
    REQUIRE(vm.value("i").toInt() == 5);
}

TEST_CASE("VariableManager.setDescription: 修改描述", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("i", "int", 0, "old");
    REQUIRE(vm.setDescription("i", "new"));
    REQUIRE(vm.variable("i").value("description").toString() == "new");
}

TEST_CASE("VariableManager.setDescription: 不存在的变量返回 false", "[Core][VariableManager]") {
    VariableManager vm;
    REQUIRE_FALSE(vm.setDescription("nope", "x"));
}

// =====================================================
// getValue / variable / variables 读取 + 不存在变量
// =====================================================

TEST_CASE("VariableManager.value: 读取 + 不存在的变量返回无效 QVariant", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("i", "int", 42);
    REQUIRE(vm.value("i").toInt() == 42);
    REQUIRE_FALSE(vm.value("nope").isValid());
}

TEST_CASE("VariableManager.variable: 完整信息读取", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("i", "int", 42, "desc");
    const QVariantMap info = vm.variable("i");
    REQUIRE(info.value("name").toString() == "i");
    REQUIRE(info.value("type").toString() == "int");
    REQUIRE(info.value("value").toInt() == 42);
    REQUIRE(info.value("description").toString() == "desc");
}

TEST_CASE("VariableManager.variable: 不存在的变量返回空 Map", "[Core][VariableManager]") {
    VariableManager vm;
    const QVariantMap info = vm.variable("nope");
    REQUIRE(info.isEmpty());
}

TEST_CASE("VariableManager.variables: 列表读取", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("i", "int", 1);
    vm.createVariable("s", "string", QString("x"));
    const QVariantList list = vm.variables();
    REQUIRE(list.size() == 2);
    // 验证列表元素结构
    const QVariantMap first = list.at(0).toMap();
    REQUIRE(first.contains("name"));
    REQUIRE(first.contains("type"));
    REQUIRE(first.contains("value"));
    REQUIRE(first.contains("description"));
}

TEST_CASE("VariableManager.variables: 空列表", "[Core][VariableManager]") {
    VariableManager vm;
    REQUIRE(vm.variables().isEmpty());
}

TEST_CASE("VariableManager.exists / count", "[Core][VariableManager]") {
    VariableManager vm;
    REQUIRE(vm.count() == 0);
    REQUIRE_FALSE(vm.exists("x"));
    vm.createVariable("x", "int", 0);
    REQUIRE(vm.exists("x"));
    REQUIRE(vm.count() == 1);
}

// =====================================================
// removeVariable：删除 + 不存在的
// =====================================================

TEST_CASE("VariableManager.removeVariable: 删除成功", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("x", "int", 0);
    REQUIRE(vm.removeVariable("x"));
    REQUIRE_FALSE(vm.exists("x"));
    REQUIRE(vm.count() == 0);
}

TEST_CASE("VariableManager.removeVariable: 不存在的变量返回 false", "[Core][VariableManager]") {
    VariableManager vm;
    REQUIRE_FALSE(vm.removeVariable("nope"));
}

// =====================================================
// clear：清空
// =====================================================

TEST_CASE("VariableManager.clear: 清空所有变量", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("i", "int", 1);
    vm.createVariable("s", "string", QString("x"));
    vm.clear();
    REQUIRE(vm.count() == 0);
    REQUIRE(vm.variables().isEmpty());
}

TEST_CASE("VariableManager.clear: 空时调用不报错", "[Core][VariableManager]") {
    VariableManager vm;
    vm.clear();
    REQUIRE(vm.count() == 0);
}

// =====================================================
// resolveBinding：${var} 替换 + 未定义变量 + 嵌套
// =====================================================

TEST_CASE("VariableManager.resolveBinding: 单变量替换", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("thresh", "int", 128);
    bool ok = false;
    const QString r = vm.resolveBinding("${thresh}", &ok);
    REQUIRE(ok);
    REQUIRE(r == "128");
}

TEST_CASE("VariableManager.resolveBinding: 前缀 + 后缀", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("v", "string", QString("ABC"));
    bool ok = false;
    REQUIRE(vm.resolveBinding("pre_${v}_suf", &ok) == "pre_ABC_suf");
    REQUIRE(ok);
}

TEST_CASE("VariableManager.resolveBinding: 多变量替换", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("a", "int", 10);
    vm.createVariable("b", "int", 20);
    bool ok = false;
    REQUIRE(vm.resolveBinding("${a}+${b}", &ok) == "10+20");
    REQUIRE(ok);
}

TEST_CASE("VariableManager.resolveBinding: 未定义变量 ok=false", "[Core][VariableManager]") {
    VariableManager vm;
    bool ok = true;
    const QString r = vm.resolveBinding("${nope}", &ok);
    REQUIRE_FALSE(ok);
    // 未定义变量保留原样
    REQUIRE(r == "${nope}");
}

TEST_CASE("VariableManager.resolveBinding: 部分未定义 ok=false 且已定义变量被替换", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("a", "int", 5);
    bool ok = true;
    const QString r = vm.resolveBinding("${a}-${nope}", &ok);
    REQUIRE_FALSE(ok);
    // 已定义变量被替换，未定义的保留
    REQUIRE(r == "5-${nope}");
}

TEST_CASE("VariableManager.resolveBinding: 不含 ${} 的字符串原样返回", "[Core][VariableManager]") {
    VariableManager vm;
    bool ok = false;
    REQUIRE(vm.resolveBinding("hello world", &ok) == "hello world");
    REQUIRE(ok);
}

TEST_CASE("VariableManager.resolveBinding: 空字符串原样返回", "[Core][VariableManager]") {
    VariableManager vm;
    bool ok = false;
    REQUIRE(vm.resolveBinding("", &ok) == "");
    REQUIRE(ok);
}

TEST_CASE("VariableManager.resolveBinding: 算子输出变量名（含点和连字符）", "[Core][VariableManager]") {
    VariableManager vm;
    // UUID 形式的算子输出变量名（含点和连字符）
    vm.registerOperatorOutput("abc-123-def", "outName", "int");
    vm.updateOperatorOutputValues("abc-123-def", QJsonObject{{"outName", 999}});
    bool ok = false;
    const QString r = vm.resolveBinding("${abc-123-def.outName}", &ok);
    REQUIRE(ok);
    REQUIRE(r == "999");
}

// =====================================================
// resolveVariant：String/Map/List 递归解析
// =====================================================

TEST_CASE("VariableManager.resolveVariant: String 类型递归解析", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("v", "int", 42);
    const QVariant r = vm.resolveVariant(QVariant("${v}"));
    REQUIRE(r.toString() == "42");
}

TEST_CASE("VariableManager.resolveVariant: Map 类型递归解析", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("v", "int", 7);
    QVariantMap src;
    src["a"] = QVariant("${v}");
    src["b"] = QVariant("literal");
    const QVariant r = vm.resolveVariant(src);
    const QVariantMap got = r.toMap();
    REQUIRE(got.value("a").toString() == "7");
    REQUIRE(got.value("b").toString() == "literal");
}

TEST_CASE("VariableManager.resolveVariant: List 类型递归解析", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("v", "int", 3);
    QVariantList src;
    src.append(QVariant("${v}"));
    src.append(QVariant("literal"));
    const QVariant r = vm.resolveVariant(src);
    const QVariantList got = r.toList();
    REQUIRE(got.at(0).toString() == "3");
    REQUIRE(got.at(1).toString() == "literal");
}

TEST_CASE("VariableManager.resolveVariant: 嵌套 Map+List+String 解析", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("v", "int", 100);
    QVariantMap inner;
    inner["k"] = QVariant("${v}");
    QVariantList src;
    src.append(inner);
    src.append(QVariant("${v}"));
    const QVariant r = vm.resolveVariant(src);
    const QVariantList got = r.toList();
    REQUIRE(got.at(0).toMap().value("k").toString() == "100");
    REQUIRE(got.at(1).toString() == "100");
}

TEST_CASE("VariableManager.resolveVariant: 含未定义变量返回原值", "[Core][VariableManager]") {
    VariableManager vm;
    const QVariant r = vm.resolveVariant(QVariant("${nope}"));
    // 含未定义变量 → 返回原值
    REQUIRE(r.toString() == "${nope}");
}

TEST_CASE("VariableManager.resolveVariant: 数值类型直接返回", "[Core][VariableManager]") {
    VariableManager vm;
    REQUIRE(vm.resolveVariant(QVariant(42)).toInt() == 42);
    REQUIRE(vm.resolveVariant(QVariant(true)).toBool() == true);
}

TEST_CASE("VariableManager.resolveVariant: 无效 QVariant 直接返回", "[Core][VariableManager]") {
    VariableManager vm;
    const QVariant invalid;
    const QVariant r = vm.resolveVariant(invalid);
    REQUIRE_FALSE(r.isValid());
}

// =====================================================
// extractReferences：提取变量引用
// =====================================================

TEST_CASE("VariableManager.extractReferences: 提取单个变量", "[Core][VariableManager]") {
    VariableManager vm;
    const QStringList refs = vm.extractReferences("${a}");
    REQUIRE(refs.size() == 1);
    REQUIRE(refs.at(0) == "a");
}

TEST_CASE("VariableManager.extractReferences: 提取多个变量", "[Core][VariableManager]") {
    VariableManager vm;
    const QStringList refs = vm.extractReferences("pre_${a}_${b}_suf");
    REQUIRE(refs.size() == 2);
    REQUIRE(refs.contains("a"));
    REQUIRE(refs.contains("b"));
}

TEST_CASE("VariableManager.extractReferences: 不含变量引用返回空", "[Core][VariableManager]") {
    VariableManager vm;
    const QStringList refs = vm.extractReferences("plain text");
    REQUIRE(refs.isEmpty());
}

TEST_CASE("VariableManager.extractReferences: 算子输出变量名（含点和连字符）", "[Core][VariableManager]") {
    VariableManager vm;
    const QStringList refs = vm.extractReferences("${node-1.out}");
    REQUIRE(refs.size() == 1);
    REQUIRE(refs.at(0) == "node-1.out");
}

// =====================================================
// toJson / fromJson：序列化/反序列化 + 所有类型
// =====================================================

TEST_CASE("VariableManager.toJson/fromJson: 全类型往返", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("i", "int", 42);
    vm.createVariable("d", "double", 3.14);
    vm.createVariable("s", "string", QString("hello"));
    vm.createVariable("b", "bool", true);
    vm.createVariable("r", "roi", makeRoiMap(1, 2, 3, 4));
    QVariantList polygon; polygon.append(0); polygon.append(0); polygon.append(10); polygon.append(10);
    vm.createVariable("reg", "region", polygon);
    QVariantMap p; p["x"] = 1.0; p["y"] = 2.0;
    QVariantList pts; pts.append(p);
    vm.createVariable("pts", "points", pts);

    const QJsonArray arr = vm.toJson();
    REQUIRE(arr.size() == 7);

    VariableManager restored;
    REQUIRE(restored.fromJson(arr));
    REQUIRE(restored.count() == 7);
    // 逐类型校验
    REQUIRE(restored.value("i").toInt() == 42);
    REQUIRE_NEAR(restored.value("d").toDouble(), 3.14, 1e-9);
    REQUIRE(restored.value("s").toString() == "hello");
    REQUIRE(restored.value("b").toBool() == true);
    const QVariantMap r = restored.value("r").toMap();
    REQUIRE_NEAR(r.value("x").toDouble(), 1.0, 1e-9);
    REQUIRE_NEAR(r.value("w").toDouble(), 3.0, 1e-9);
    REQUIRE(restored.value("reg").toList().size() == 4);
    REQUIRE(restored.value("pts").toList().size() == 1);
}

TEST_CASE("VariableManager.fromJson: 空数组", "[Core][VariableManager]") {
    VariableManager vm;
    vm.createVariable("x", "int", 1);
    REQUIRE(vm.fromJson(QJsonArray()));
    REQUIRE(vm.count() == 0);
}

TEST_CASE("VariableManager.fromJson: 非法名被跳过", "[Core][VariableManager]") {
    VariableManager vm;
    QJsonArray arr;
    QJsonObject bad;
    bad["name"] = "1invalid";   // 非法名
    bad["type"] = "int";
    bad["value"] = 1;
    arr.append(bad);
    QJsonObject good;
    good["name"] = "ok";
    good["type"] = "int";
    good["value"] = 1;
    arr.append(good);
    REQUIRE(vm.fromJson(arr));
    REQUIRE(vm.count() == 1);
    REQUIRE(vm.exists("ok"));
}

TEST_CASE("VariableManager.fromJson: 未知类型被跳过", "[Core][VariableManager]") {
    VariableManager vm;
    QJsonArray arr;
    QJsonObject bad;
    bad["name"] = "x";
    bad["type"] = "unknownType";
    bad["value"] = 1;
    arr.append(bad);
    REQUIRE(vm.fromJson(arr));
    REQUIRE(vm.count() == 0);
}

TEST_CASE("VariableManager.toJson: 空时返回空数组", "[Core][VariableManager]") {
    VariableManager vm;
    REQUIRE(vm.toJson().isEmpty());
}

// =====================================================
// registerOperatorOutput：算子输出注册 + 各种 typeName
// =====================================================

TEST_CASE("VariableManager.registerOperatorOutput: 基本注册", "[Core][VariableManager]") {
    VariableManager vm;
    vm.registerOperatorOutput("node1", "out1", "int");
    REQUIRE(vm.isOperatorOutputRegistered("node1", "out1"));
    REQUIRE(vm.exists("node1.out1"));
    // 默认值
    REQUIRE(vm.value("node1.out1").toInt() == 0);
}

TEST_CASE("VariableManager.registerOperatorOutput: 各种 typeName 映射", "[Core][VariableManager]") {
    VariableManager vm;
    vm.registerOperatorOutput("n_int",     "o", "int");
    vm.registerOperatorOutput("n_double",  "o", "double");
    vm.registerOperatorOutput("n_string",  "o", "string");
    vm.registerOperatorOutput("n_strarr",  "o", "string[]");
    vm.registerOperatorOutput("n_dblarr",  "o", "double[]");
    vm.registerOperatorOutput("n_roi",     "o", "roi");
    vm.registerOperatorOutput("n_rect",    "o", "rect");
    vm.registerOperatorOutput("n_region",  "o", "region");
    vm.registerOperatorOutput("n_points",  "o", "points");
    vm.registerOperatorOutput("n_pointarr","o", "point[]");
    vm.registerOperatorOutput("n_unknown", "o", "weirdType");

    // int
    REQUIRE(vm.value("n_int.o").toInt() == 0);
    // double
    REQUIRE_NEAR(vm.value("n_double.o").toDouble(), 0.0, 1e-9);
    // string（默认空串）
    REQUIRE(vm.value("n_string.o").toString() == "");
    // string[] 归并为 String（值 QStringList）
    // double[] 归并为 Double（值 QVariantList）
    // roi 默认 {x=0,y=0,w=100,h=100}
    const QVariantMap roi = vm.value("n_roi.o").toMap();
    REQUIRE_NEAR(roi.value("w").toDouble(), 100.0, 1e-9);
    // rect 也映射为 roi
    REQUIRE_FALSE(vm.value("n_rect.o").toMap().isEmpty());
    // region 默认空列表
    REQUIRE(vm.value("n_region.o").toList().isEmpty());
    // points 默认空列表
    REQUIRE(vm.value("n_points.o").toList().isEmpty());
    // point[] 映射为 points
    REQUIRE(vm.value("n_pointarr.o").toList().isEmpty());
    // 未知类型默认 String
    REQUIRE(vm.value("n_unknown.o").toString() == "");
}

TEST_CASE("VariableManager.registerOperatorOutput: 空参数被拒绝", "[Core][VariableManager]") {
    VariableManager vm;
    vm.registerOperatorOutput("", "o", "int");
    vm.registerOperatorOutput("n", "", "int");
    REQUIRE(vm.count() == 0);
}

TEST_CASE("VariableManager.registerOperatorOutput: 重复注册不覆盖", "[Core][VariableManager]") {
    VariableManager vm;
    vm.registerOperatorOutput("n", "o", "int");
    vm.updateOperatorOutputValues("n", QJsonObject{{"o", 42}});
    REQUIRE(vm.value("n.o").toInt() == 42);
    // 重复注册：不应覆盖已有值
    vm.registerOperatorOutput("n", "o", "int");
    REQUIRE(vm.value("n.o").toInt() == 42);
}

TEST_CASE("VariableManager.unregisterOperatorOutput: 指定输出名反注册", "[Core][VariableManager]") {
    VariableManager vm;
    vm.registerOperatorOutput("n", "o1", "int");
    vm.registerOperatorOutput("n", "o2", "int");
    vm.unregisterOperatorOutput("n", "o1");
    REQUIRE_FALSE(vm.isOperatorOutputRegistered("n", "o1"));
    REQUIRE(vm.isOperatorOutputRegistered("n", "o2"));
}

TEST_CASE("VariableManager.unregisterOperatorOutput: 输出名空时反注册所有", "[Core][VariableManager]") {
    VariableManager vm;
    vm.registerOperatorOutput("n", "o1", "int");
    vm.registerOperatorOutput("n", "o2", "int");
    vm.registerOperatorOutput("other", "o3", "int");
    vm.unregisterOperatorOutput("n", QString());
    REQUIRE_FALSE(vm.isOperatorOutputRegistered("n", "o1"));
    REQUIRE_FALSE(vm.isOperatorOutputRegistered("n", "o2"));
    // other 节点不受影响
    REQUIRE(vm.isOperatorOutputRegistered("other", "o3"));
}

TEST_CASE("VariableManager.unregisterOperatorOutput: 空 nodeId 不操作", "[Core][VariableManager]") {
    VariableManager vm;
    vm.registerOperatorOutput("n", "o", "int");
    vm.unregisterOperatorOutput("", "o");
    REQUIRE(vm.isOperatorOutputRegistered("n", "o"));
}

TEST_CASE("VariableManager.updateOperatorOutputValues: 更新已注册变量", "[Core][VariableManager]") {
    VariableManager vm;
    vm.registerOperatorOutput("n", "o", "int");
    QJsonObject outputs;
    outputs["o"] = 123;
    vm.updateOperatorOutputValues("n", outputs);
    REQUIRE(vm.value("n.o").toInt() == 123);
}

TEST_CASE("VariableManager.updateOperatorOutputValues: 仅更新已注册变量（不创建新变量）", "[Core][VariableManager]") {
    VariableManager vm;
    vm.registerOperatorOutput("n", "o1", "int");
    QJsonObject outputs;
    outputs["o1"] = 1;
    outputs["o2"] = 2;  // 未注册，不应创建
    vm.updateOperatorOutputValues("n", outputs);
    REQUIRE(vm.value("n.o1").toInt() == 1);
    REQUIRE_FALSE(vm.exists("n.o2"));
}

TEST_CASE("VariableManager.updateOperatorOutputValues: 空参数不操作", "[Core][VariableManager]") {
    VariableManager vm;
    vm.registerOperatorOutput("n", "o", "int");
    vm.updateOperatorOutputValues("", QJsonObject{{"o", 1}});
    vm.updateOperatorOutputValues("n", QJsonObject());
    // 原值未变
    REQUIRE(vm.value("n.o").toInt() == 0);
}

TEST_CASE("VariableManager.updateOperatorOutputValues: 相同值不更新", "[Core][VariableManager]") {
    VariableManager vm;
    vm.registerOperatorOutput("n", "o", "int");
    vm.updateOperatorOutputValues("n", QJsonObject{{"o", 42}});
    // 再次写入相同值
    vm.updateOperatorOutputValues("n", QJsonObject{{"o", 42}});
    REQUIRE(vm.value("n.o").toInt() == 42);
}

TEST_CASE("VariableManager.isOperatorOutputRegistered: 空参数返回 false", "[Core][VariableManager]") {
    VariableManager vm;
    REQUIRE_FALSE(vm.isOperatorOutputRegistered("", "o"));
    REQUIRE_FALSE(vm.isOperatorOutputRegistered("n", ""));
}

// =====================================================
// v2.7.0 ROI/Region/Points 辅助方法
// =====================================================

TEST_CASE("VariableManager.toRoi/fromRoi: QRectF <-> QVariantMap", "[Core][VariableManager]") {
    const QRectF r(1.0, 2.0, 100.0, 50.0);
    const QVariant v = VariableManager::fromRoi(r);
    const QRectF got = VariableManager::toRoi(v);
    REQUIRE_NEAR(got.x(), 1.0, 1e-9);
    REQUIRE_NEAR(got.y(), 2.0, 1e-9);
    REQUIRE_NEAR(got.width(), 100.0, 1e-9);
    REQUIRE_NEAR(got.height(), 50.0, 1e-9);
}

TEST_CASE("VariableManager.toRoi: 空 QVariant 返回空 QRectF", "[Core][VariableManager]") {
    const QRectF got = VariableManager::toRoi(QVariant());
    REQUIRE(got.isNull());
}

TEST_CASE("VariableManager.toPoints/fromPoints: QList<QPointF> <-> QVariantList", "[Core][VariableManager]") {
    QList<QPointF> pts;
    pts.append(QPointF(1.0, 2.0));
    pts.append(QPointF(3.0, 4.0));
    const QVariant v = VariableManager::fromPoints(pts);
    const QList<QPointF> got = VariableManager::toPoints(v);
    REQUIRE(got.size() == 2);
    REQUIRE_NEAR(got.at(0).x(), 1.0, 1e-9);
    REQUIRE_NEAR(got.at(1).y(), 4.0, 1e-9);
}

TEST_CASE("VariableManager.toPoints: 缺少 x/y 字段的项被跳过", "[Core][VariableManager]") {
    QVariantList lst;
    QVariantMap good; good["x"] = 1.0; good["y"] = 2.0;
    QVariantMap bad;  bad["z"] = 3.0;
    lst.append(good);
    lst.append(bad);
    const QList<QPointF> got = VariableManager::toPoints(QVariant(lst));
    REQUIRE(got.size() == 1);
}

// =====================================================
// 线程安全：多线程并发 setValue/getValue
// 验证 QRecursiveMutex 保护下不会崩溃且数据一致
// =====================================================

TEST_CASE("VariableManager.线程安全: 多线程并发读写不崩溃", "[Core][VariableManager][Threads]") {
    VariableManager vm;
    vm.createVariable("counter", "int", 0);

    const int threadCount = 8;
    const int itersPerThread = 200;

    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    for (int t = 0; t < threadCount; ++t) {
        threads.emplace_back([&vm, &errors, itersPerThread]() {
            for (int i = 0; i < itersPerThread; ++i) {
                // 并发读
                vm.value("counter");
                vm.variables();
                vm.exists("counter");
                // 并发写
                if (!vm.setValue("counter", i)) {
                    errors++;
                }
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    // 所有写操作都应成功
    REQUIRE(errors.load() == 0);
    // 最终值应该是某个线程的最后一次写入
    REQUIRE(vm.value("counter").toInt() >= 0);
}

TEST_CASE("VariableManager.线程安全: 并发 createVariable + removeVariable", "[Core][VariableManager][Threads]") {
    VariableManager vm;
    const int threadCount = 4;
    const int itersPerThread = 100;

    std::vector<std::thread> threads;
    for (int t = 0; t < threadCount; ++t) {
        threads.emplace_back([&vm, t, itersPerThread]() {
            for (int i = 0; i < itersPerThread; ++i) {
                const QString name = QString("v_%1_%2").arg(t).arg(i);
                vm.createVariable(name, "int", i);
                vm.value(name);
                vm.removeVariable(name);
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    // 所有临时变量都被删除
    REQUIRE(vm.count() == 0);
}

TEST_CASE("VariableManager.线程安全: 并发 resolveBinding 不崩溃", "[Core][VariableManager][Threads]") {
    VariableManager vm;
    vm.createVariable("v", "int", 42);
    const int threadCount = 4;
    const int itersPerThread = 200;
    std::vector<std::thread> threads;
    std::atomic<int> okCount{0};
    for (int t = 0; t < threadCount; ++t) {
        threads.emplace_back([&vm, &okCount, itersPerThread]() {
            for (int i = 0; i < itersPerThread; ++i) {
                bool ok = false;
                const QString r = vm.resolveBinding("${v}", &ok);
                if (ok && r == "42") okCount++;
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    // 所有解析都应成功
    REQUIRE(okCount.load() == threadCount * itersPerThread);
}
