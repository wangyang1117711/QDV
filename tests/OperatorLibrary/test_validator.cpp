// =====================================================================
// OperatorValidator 单元测试（spec §6.2，12 个用例）
// =====================================================================
#include "../catch2/catch2_minimal.hpp"
#include "OperatorLibrary/OperatorValidator.h"
#include "OperatorLibrary/OperatorDefinition.h"

#include <QJsonObject>

using QDV::OperatorLibrary::OperatorValidator;
using QDV::OperatorLibrary::OperatorDef;
using QDV::OperatorLibrary::InputPort;
using QDV::OperatorLibrary::OutputPort;
using QDV::OperatorLibrary::ParamDef;
using QDV::OperatorLibrary::ParamType;
using QDV::OperatorLibrary::ValidationResult;
using QDV::OperatorLibrary::ValidationIssue;

// ---------------------------------------------------------------------
// 辅助：构造一个全字段合法的 OperatorDef（其他用例在此基础上变异）
// ---------------------------------------------------------------------
static OperatorDef makeValid() {
    OperatorDef d;
    d.type = "MyCustomThreshold";
    d.cnName = "我的自定义阈值";
    d.category = "图像分割";
    d.version = "1.0.0";
    d.status = QDV::OperatorLibrary::OperatorStatus::Published;

    InputPort in; in.name = "image"; in.cnName = "输入图像"; in.type = "Image"; in.required = true;
    d.inputs.append(in);

    OutputPort out; out.name = "region"; out.cnName = "分割区域"; out.type = "Region";
    d.outputs.append(out);

    ParamDef p; p.name = "mode"; p.cnName = "阈值模式"; p.type = ParamType::Enum;
    p.defaultValue = "auto"; p.options = {"自动", "手动"}; p.optionKeys = {"auto", "manual"};
    d.params.append(p);

    d.implementation.recipe = QStringList{"Threshold"};
    return d;
}

// ---------------------------------------------------------------------
// 辅助：检查 result 是否包含指定 field+level 的 issue
// ---------------------------------------------------------------------
static bool hasIssue(const ValidationResult& r, const QString& field,
                     ValidationIssue::Level level = ValidationIssue::Error) {
    for (const ValidationIssue& i : r.issues) {
        if (i.field == field && i.level == level) return true;
    }
    return false;
}

// =====================================================================
// 用例 1：全字段合法
// =====================================================================
TEST_CASE("Validator: valid def returns ok", "[op_library][validator]") {
    OperatorDef d = makeValid();
    const ValidationResult r = OperatorValidator::validate(d);
    REQUIRE(r.ok);
    REQUIRE(r.issues.isEmpty());
}

// =====================================================================
// 用例 2：type 缺失
// =====================================================================
TEST_CASE("Validator: missing type is Error", "[op_library][validator]") {
    OperatorDef d = makeValid();
    d.type.clear();
    const ValidationResult r = OperatorValidator::validate(d);
    REQUIRE_FALSE(r.ok);
    REQUIRE(hasIssue(r, "type"));
}

// =====================================================================
// 用例 3：type 含非法字符（下划线开头）
// =====================================================================
TEST_CASE("Validator: type starting with underscore is Error", "[op_library][validator]") {
    OperatorDef d = makeValid();
    d.type = "_Invalid";
    const ValidationResult r = OperatorValidator::validate(d);
    REQUIRE_FALSE(r.ok);
    REQUIRE(hasIssue(r, "type"));
}

// 用例 3b：type 含非法字符（数字开头）
TEST_CASE("Validator: type starting with digit is Error", "[op_library][validator]") {
    OperatorDef d = makeValid();
    d.type = "9Bad";
    const ValidationResult r = OperatorValidator::validate(d);
    REQUIRE_FALSE(r.ok);
    REQUIRE(hasIssue(r, "type"));
}

// 用例 3c：type 过短（< 3 字符）
TEST_CASE("Validator: type too short is Error", "[op_library][validator]") {
    OperatorDef d = makeValid();
    d.type = "Ab";  // 仅 2 字符，正则要求 {2,63} 即 3-64 字符
    const ValidationResult r = OperatorValidator::validate(d);
    REQUIRE_FALSE(r.ok);
    REQUIRE(hasIssue(r, "type"));
}

// =====================================================================
// 用例 4：Enum 参数无 options
// =====================================================================
TEST_CASE("Validator: Enum param without options is Error", "[op_library][validator]") {
    OperatorDef d = makeValid();
    d.params[0].options.clear();
    d.params[0].optionKeys.clear();
    const ValidationResult r = OperatorValidator::validate(d);
    REQUIRE_FALSE(r.ok);
    REQUIRE(hasIssue(r, "params"));
}

// =====================================================================
// 用例 5：implementation 全空（recipe/script/library 均空）
// =====================================================================
TEST_CASE("Validator: empty implementation is Error", "[op_library][validator]") {
    OperatorDef d = makeValid();
    d.implementation.recipe.clear();
    d.implementation.script.clear();
    d.implementation.library.clear();
    const ValidationResult r = OperatorValidator::validate(d);
    REQUIRE_FALSE(r.ok);
    REQUIRE(hasIssue(r, "implementation"));
}

// =====================================================================
// 用例 6：version 非语义化
// =====================================================================
TEST_CASE("Validator: non-SemVer version is Error", "[op_library][validator]") {
    OperatorDef d = makeValid();
    d.version = "v1.0";
    const ValidationResult r = OperatorValidator::validate(d);
    REQUIRE_FALSE(r.ok);
    REQUIRE(hasIssue(r, "version"));
}

// =====================================================================
// 用例 7：cnName 超长
// =====================================================================
TEST_CASE("Validator: cnName over 32 chars is Error", "[op_library][validator]") {
    OperatorDef d = makeValid();
    d.cnName = QStringLiteral("这是一个超过三十二个字符的中文名称测试用例用来验证校验器是否会正确拒绝超长算子中文名称");
    const ValidationResult r = OperatorValidator::validate(d);
    REQUIRE_FALSE(r.ok);
    REQUIRE(hasIssue(r, "cnName"));
}

// =====================================================================
// 用例 8：inputs name 重复
// =====================================================================
TEST_CASE("Validator: duplicate input names is Error", "[op_library][validator]") {
    OperatorDef d = makeValid();
    InputPort dup; dup.name = "image"; dup.cnName = "重复"; dup.type = "Image"; dup.required = true;
    d.inputs.append(dup);
    const ValidationResult r = OperatorValidator::validate(d);
    REQUIRE_FALSE(r.ok);
    REQUIRE(hasIssue(r, "inputs"));
}

// =====================================================================
// 用例 9：outputs name 重复
// =====================================================================
TEST_CASE("Validator: duplicate output names is Error", "[op_library][validator]") {
    OperatorDef d = makeValid();
    OutputPort dup; dup.name = "region"; dup.cnName = "重复"; dup.type = "Region";
    d.outputs.append(dup);
    const ValidationResult r = OperatorValidator::validate(d);
    REQUIRE_FALSE(r.ok);
    REQUIRE(hasIssue(r, "outputs"));
}

// =====================================================================
// 用例 10：params name 重复
// =====================================================================
TEST_CASE("Validator: duplicate param names is Error", "[op_library][validator]") {
    OperatorDef d = makeValid();
    ParamDef dup; dup.name = "mode"; dup.cnName = "重复"; dup.type = ParamType::Enum;
    dup.options = {"a"}; dup.optionKeys = {"a"};
    d.params.append(dup);
    const ValidationResult r = OperatorValidator::validate(d);
    REQUIRE_FALSE(r.ok);
    REQUIRE(hasIssue(r, "params"));
}

// =====================================================================
// 用例 11：category 不在建议词表（Warning，不阻断）
// =====================================================================
TEST_CASE("Validator: unknown category is Warning", "[op_library][validator]") {
    OperatorDef d = makeValid();
    d.category = "自定义分类";
    const ValidationResult r = OperatorValidator::validate(d);
    REQUIRE(r.ok);  // 仅有 Warning，仍 ok
    REQUIRE(hasIssue(r, "category", ValidationIssue::Warning));
}

// =====================================================================
// 用例 12：param name 不符合小写驼峰（Warning，不阻断）
// =====================================================================
TEST_CASE("Validator: non-camelCase param name is Warning", "[op_library][validator]") {
    OperatorDef d = makeValid();
    d.params[0].name = "Mode";  // 大写开头，违反小写驼峰
    const ValidationResult r = OperatorValidator::validate(d);
    REQUIRE(r.ok);  // 仅有 Warning
    REQUIRE(hasIssue(r, "params", ValidationIssue::Warning));
}

// =====================================================================
// 附加用例：isTypeName / isSemVer / isParamName 直接测试
// =====================================================================
TEST_CASE("Validator: isTypeName matches spec regex", "[op_library][validator]") {
    REQUIRE(OperatorValidator::isTypeName("Threshold"));       // 合法
    REQUIRE(OperatorValidator::isTypeName("MyCustom_2"));      // 合法（含下划线）
    REQUIRE(OperatorValidator::isTypeName("abc"));             // 合法（小写开头）
    REQUIRE_FALSE(OperatorValidator::isTypeName("_Bad"));      // 下划线开头
    REQUIRE_FALSE(OperatorValidator::isTypeName("9Bad"));      // 数字开头
    REQUIRE_FALSE(OperatorValidator::isTypeName("Ab"));        // 太短
    REQUIRE_FALSE(OperatorValidator::isTypeName(""));          // 空
    REQUIRE_FALSE(OperatorValidator::isTypeName("a b"));       // 含空格
}

TEST_CASE("Validator: isSemVer matches spec", "[op_library][validator]") {
    REQUIRE(OperatorValidator::isSemVer("1.0.0"));
    REQUIRE(OperatorValidator::isSemVer("0.0.1"));
    REQUIRE(OperatorValidator::isSemVer("10.20.30"));
    REQUIRE_FALSE(OperatorValidator::isSemVer("1.0"));
    REQUIRE_FALSE(OperatorValidator::isSemVer("v1.0.0"));
    REQUIRE_FALSE(OperatorValidator::isSemVer("1.0.0-beta"));
    REQUIRE_FALSE(OperatorValidator::isSemVer(""));
}

TEST_CASE("Validator: categoryVocabulary contains expected entries", "[op_library][validator]") {
    const QStringList cats = OperatorValidator::categoryVocabulary();
    REQUIRE(cats.contains("图像分割"));
    REQUIRE(cats.contains("预处理"));
    REQUIRE(cats.contains("深度学习"));
    REQUIRE(cats.contains("分支"));
    REQUIRE_FALSE(cats.contains("不存在的分类"));
}

// =====================================================================
// 附加用例：validateJson 反序列化失败
// =====================================================================
TEST_CASE("Validator: validateJson on missing type returns Error", "[op_library][validator]") {
    QJsonObject obj;
    obj["cnName"] = "无 type";
    obj["category"] = "预处理";
    QString parseErr;
    const ValidationResult r = OperatorValidator::validateJson(obj, &parseErr);
    REQUIRE_FALSE(r.ok);
    REQUIRE_FALSE(parseErr.isEmpty());
    REQUIRE(hasIssue(r, "type"));
}
