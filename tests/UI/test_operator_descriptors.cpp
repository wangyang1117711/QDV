// ============================================================================
// OperatorDescriptors 单元测试 — AI 分类算子输出元数据扩展（v5.4 升级 Task 1）
// ----------------------------------------------------------------------------
// 覆盖范围：
//   1. AiClassify 算子 outputs 数量验证（应为 5 项）
//   2. AiClassify 算子 outputs 中 defaultEnabled 字段值验证
//   3. AiClassify 算子 outputs 中 multiTargetOnly 字段值验证
//   4. OperatorMeta::toMap()/fromMap() 往返一致性验证（含新字段）
//
// 测试策略：
//   - 测试 1~3 使用 OperatorDescriptorsGuard 加载测试 JSON（与 buildRegistry
//     中 AiClassify 的 outputs 定义一致），确保测试环境确定性，不依赖外部
//     config/operators.json 文件。
//   - 测试 4 直接构造 OperatorMeta 对象，验证 toMap/fromMap 序列化路径对
//     defaultEnabled/multiTargetOnly 新字段的支持。
//
// 测试框架：catch2_minimal（TEST_CASE 静态注册，REQUIRE/CHECK 宏）
// ============================================================================
#include "../catch2/catch2_minimal.hpp"
#include "UI/OperatorDescriptors.h"

#include <QApplication>
#include <QTemporaryFile>
#include <QVariant>
#include <QVariantMap>
#include <QStringList>

// === QApplication 单例（UI 测试通用模式） ===
static int argc = 0;
static QApplication* app() {
    // 复用 test_main.cpp 中创建的全局 QApplication 实例
    return qobject_cast<QApplication*>(QCoreApplication::instance());
}
inline void ensureApp() { app(); }

// === RAII guard：测试期间加载自定义算子元数据，作用域结束自动 reset ===
// 参考 test_roi_validation.cpp 中的同名实现
class OperatorDescriptorsGuard {
public:
    OperatorDescriptorsGuard(const QString& jsonContent) {
        m_tmp.setFileTemplate(QStringLiteral("qdv_opdesc_test_XXXXXX.json"));
        m_tmp.setAutoRemove(true);
        REQUIRE(m_tmp.open());
        m_tmp.write(jsonContent.toUtf8());
        m_tmp.close();

        QString err;
        m_loaded = QDV::UI::OperatorDescriptors::loadFromJson(m_tmp.fileName(), &err);
        m_error = err;
    }
    ~OperatorDescriptorsGuard() {
        QDV::UI::OperatorDescriptors::reset();
    }
    bool loaded() const { return m_loaded; }
    QString error() const { return m_error; }
private:
    QTemporaryFile m_tmp;
    bool m_loaded = false;
    QString m_error;
};

// === 测试用 JSON：包含 AiClassify 算子（5 项 outputs，与 buildRegistry 一致） ===
// ParamType 枚举值：Int=0, Float=1, Enum=2, Bool=3, String=4, ROI=5, Vector=6
// AiClassify 已在 ToolFactory 中注册，不会被 loadFromJson 的幽灵算子剔除逻辑移除
static const char* kAiClassifyTestJson = R"({
    "operators": [
        {
            "type": "AiClassify",
            "cnName": "AI 分类",
            "category": "AI",
            "params": [
                {"name": "modelPath", "cnName": "模型路径", "type": 4, "defaultValue": ""}
            ],
            "outputs": [
                {"name": "classId", "cnName": "分类ID", "typeName": "int", "desc": "分类标签的数值ID", "color": "#81C784", "defaultEnabled": true, "multiTargetOnly": false},
                {"name": "className", "cnName": "分类类别", "typeName": "string", "desc": "分类类别名称（单目标为字符串，多目标为数组）", "color": "#64B5F6", "defaultEnabled": true, "multiTargetOnly": false},
                {"name": "confidence", "cnName": "分类置信度", "typeName": "double", "desc": "分类置信度（单目标为数值，多目标为数组）", "color": "#FFD54F", "defaultEnabled": true, "multiTargetOnly": false},
                {"name": "classArray", "cnName": "类别数组", "typeName": "string[]", "desc": "多目标场景下按检测顺序输出的类别名数组", "color": "#BA68C8", "defaultEnabled": false, "multiTargetOnly": true},
                {"name": "confidenceArray", "cnName": "置信度数组", "typeName": "double[]", "desc": "多目标场景下按检测顺序输出的置信度数组", "color": "#4DB6AC", "defaultEnabled": false, "multiTargetOnly": true}
            ]
        }
    ]
})";

// ============================================================================
// 测试用例 1：AiClassify 的 outputs 数量应为 5
// ============================================================================
TEST_CASE("AiClassify outputs count is 5", "[operator_descriptors][aiclassify]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kAiClassifyTestJson);
    REQUIRE(guard.loaded());

    const QDV::UI::OperatorMeta om = QDV::UI::OperatorDescriptors::get("AiClassify");
    REQUIRE(om.type == "AiClassify");
    REQUIRE(om.outputs.size() == 5);
}

// ============================================================================
// 测试用例 2：AiClassify outputs 的 defaultEnabled 字段值验证
//   - classId/className/confidence: defaultEnabled = true（单目标必填项）
//   - classArray/confidenceArray: defaultEnabled = false（多目标专属项，默认关闭）
// ============================================================================
TEST_CASE("AiClassify outputs defaultEnabled field", "[operator_descriptors][aiclassify]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kAiClassifyTestJson);
    REQUIRE(guard.loaded());

    const QDV::UI::OperatorMeta om = QDV::UI::OperatorDescriptors::get("AiClassify");
    REQUIRE(om.outputs.size() == 5);

    // 按 name 查找对应输出项
    auto findOutput = [&om](const QString& name) -> QVariantMap {
        for (const QVariantMap& o : om.outputs) {
            if (o.value("name").toString() == name) return o;
        }
        return QVariantMap();
    };

    // 单目标必填项：defaultEnabled = true
    QVariantMap classId = findOutput("classId");
    REQUIRE(!classId.isEmpty());
    REQUIRE(classId.value("defaultEnabled").toBool() == true);

    QVariantMap className = findOutput("className");
    REQUIRE(!className.isEmpty());
    REQUIRE(className.value("defaultEnabled").toBool() == true);

    QVariantMap confidence = findOutput("confidence");
    REQUIRE(!confidence.isEmpty());
    REQUIRE(confidence.value("defaultEnabled").toBool() == true);

    // 多目标专属项：defaultEnabled = false
    QVariantMap classArray = findOutput("classArray");
    REQUIRE(!classArray.isEmpty());
    REQUIRE(classArray.value("defaultEnabled").toBool() == false);

    QVariantMap confidenceArray = findOutput("confidenceArray");
    REQUIRE(!confidenceArray.isEmpty());
    REQUIRE(confidenceArray.value("defaultEnabled").toBool() == false);
}

// ============================================================================
// 测试用例 3：AiClassify outputs 的 multiTargetOnly 字段值验证
//   - classId/className/confidence: multiTargetOnly = false（单目标场景可用）
//   - classArray/confidenceArray: multiTargetOnly = true（仅多目标场景可用）
// ============================================================================
TEST_CASE("AiClassify outputs multiTargetOnly field", "[operator_descriptors][aiclassify]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kAiClassifyTestJson);
    REQUIRE(guard.loaded());

    const QDV::UI::OperatorMeta om = QDV::UI::OperatorDescriptors::get("AiClassify");
    REQUIRE(om.outputs.size() == 5);

    auto findOutput = [&om](const QString& name) -> QVariantMap {
        for (const QVariantMap& o : om.outputs) {
            if (o.value("name").toString() == name) return o;
        }
        return QVariantMap();
    };

    // 单目标场景可用项：multiTargetOnly = false
    REQUIRE(findOutput("classId").value("multiTargetOnly").toBool() == false);
    REQUIRE(findOutput("className").value("multiTargetOnly").toBool() == false);
    REQUIRE(findOutput("confidence").value("multiTargetOnly").toBool() == false);

    // 多目标专属项：multiTargetOnly = true
    REQUIRE(findOutput("classArray").value("multiTargetOnly").toBool() == true);
    REQUIRE(findOutput("confidenceArray").value("multiTargetOnly").toBool() == true);
}

// ============================================================================
// 测试用例 4：OperatorMeta::toMap()/fromMap() 往返一致性验证
//   构造 OperatorMeta，设置 outputs（含 defaultEnabled/multiTargetOnly），
//   toMap 后 fromMap，验证 outputs 字段往返一致。
//   此测试不依赖 OperatorDescriptors 单例，直接验证序列化路径。
// ============================================================================
TEST_CASE("OperatorMeta toMap/fromMap round trip with new output fields", "[operator_descriptors][roundtrip]") {
    ensureApp();

    using namespace QDV::UI;

    // 构造原始 OperatorMeta
    OperatorMeta original;
    original.type        = "AiClassify";
    original.cnName      = "AI 分类";
    original.category    = "AI";
    original.description = "测试用算子";

    // 添加 2 个 outputs：一个单目标项，一个多目标专属项
    {
        QVariantMap o;
        o["name"] = "classId";
        o["cnName"] = "分类ID";
        o["typeName"] = "int";
        o["desc"] = "分类标签的数值ID";
        o["color"] = "#81C784";
        o["defaultEnabled"] = true;
        o["multiTargetOnly"] = false;
        original.outputs.append(o);
    }
    {
        QVariantMap o;
        o["name"] = "classArray";
        o["cnName"] = "类别数组";
        o["typeName"] = "string[]";
        o["desc"] = "多目标场景下按检测顺序输出的类别名数组";
        o["color"] = "#BA68C8";
        o["defaultEnabled"] = false;
        o["multiTargetOnly"] = true;
        original.outputs.append(o);
    }

    // 序列化 -> 反序列化
    const QVariantMap serialized = original.toMap();
    const OperatorMeta restored = OperatorMeta::fromMap(serialized);

    // 验证基本字段往返一致
    REQUIRE(restored.type == original.type);
    REQUIRE(restored.cnName == original.cnName);
    REQUIRE(restored.category == original.category);

    // 验证 outputs 数量一致
    REQUIRE(restored.outputs.size() == 2);

    // 验证第一项（classId）的所有字段往返一致
    const QVariantMap& r0 = restored.outputs.at(0);
    REQUIRE(r0.value("name").toString() == "classId");
    REQUIRE(r0.value("cnName").toString() == "分类ID");
    REQUIRE(r0.value("typeName").toString() == "int");
    REQUIRE(r0.value("desc").toString() == "分类标签的数值ID");
    REQUIRE(r0.value("color").toString() == "#81C784");
    REQUIRE(r0.value("defaultEnabled").toBool() == true);
    REQUIRE(r0.value("multiTargetOnly").toBool() == false);

    // 验证第二项（classArray）的所有字段往返一致
    const QVariantMap& r1 = restored.outputs.at(1);
    REQUIRE(r1.value("name").toString() == "classArray");
    REQUIRE(r1.value("cnName").toString() == "类别数组");
    REQUIRE(r1.value("typeName").toString() == "string[]");
    REQUIRE(r1.value("desc").toString() == "多目标场景下按检测顺序输出的类别名数组");
    REQUIRE(r1.value("color").toString() == "#BA68C8");
    REQUIRE(r1.value("defaultEnabled").toBool() == false);
    REQUIRE(r1.value("multiTargetOnly").toBool() == true);
}
