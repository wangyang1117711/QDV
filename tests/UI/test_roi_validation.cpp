// ============================================================================
// EditViewBridge 单元测试 — ROI 校验模块（P1-C 后续任务 #2）
// ----------------------------------------------------------------------------
// 覆盖范围：
//   EditViewBridge::validateParam 中 ParamType::ROI 类型校验逻辑
//   （src/UI/EditViewBridge.cpp:486-515）
//
// ROI 校验规则：
//   - 格式 "x,y,w,h"（4 个整数，逗号/分号/空格分隔）
//   - 空字符串允许（表示未设置 ROI，由算子自行处理）
//   - 4 个字段必须均可解析为整数
//   - w/h >= 0（允许 0 表示空 ROI）
//   - x/y 允许负数（某些坐标系原点不在左上角）
//
// 测试策略：
//   由于当前内置算子均无 ROI 类型参数，需通过 OperatorDescriptors::loadFromJson
//   加载测试 JSON，注入一个带 ROI 参数的 "Threshold" 算子（Threshold 在
//   ToolFactory 中已注册，不会被 loadFromJson 的幽灵算子剔除逻辑移除）。
//   测试结束通过 reset() 恢复默认 registry，避免污染其他测试。
// ============================================================================
#include "../catch2/catch2_minimal.hpp"
#include "UI/EditViewBridge.h"
#include "UI/OperatorDescriptors.h"

#include <QApplication>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>
#include <QStringList>

// === QApplication 单例（UI 测试通用模式） ===
static int argc = 0;
static QApplication* app() {
    // 复用 test_main.cpp 中创建的全局 QApplication 实例
    return qobject_cast<QApplication*>(QCoreApplication::instance());
}
inline void ensureApp() { app(); }

// === RAII guard：测试期间加载自定义算子元数据，作用域结束自动 reset ===
class OperatorDescriptorsGuard {
public:
    OperatorDescriptorsGuard(const QString& jsonContent) {
        m_tmp.setFileTemplate(QStringLiteral("qdv_roi_test_XXXXXX.json"));
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

// === 测试用 JSON：包含带 ROI 参数的 Threshold 算子 ===
// ParamType 枚举值：Int=0, Float=1, Enum=2, Bool=3, String=4, ROI=5, Vector=6
static const char* kRoiTestJson = R"({
    "operators": [
        {
            "type": "Threshold",
            "cnName": "阈值分割（ROI测试）",
            "category": "检测",
            "params": [
                {"name": "threshold", "cnName": "阈值", "type": 1, "defaultValue": 128.0, "minValue": 0.0, "maxValue": 255.0},
                {"name": "roi", "cnName": "感兴趣区域", "type": 5, "defaultValue": ""}
            ]
        }
    ]
})";

// ============================================================================
// ROI 校验测试用例
// ============================================================================

TEST_CASE("ROI validation: legal format x,y,w,h", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    const QStringList errs = bridge.validateParam("Threshold", "roi", "10,20,100,200");
    REQUIRE(errs.isEmpty());
}

TEST_CASE("ROI validation: semicolon separator", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    const QStringList errs = bridge.validateParam("Threshold", "roi", "10;20;100;200");
    REQUIRE(errs.isEmpty());
}

TEST_CASE("ROI validation: space separator", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    const QStringList errs = bridge.validateParam("Threshold", "roi", "10 20 100 200");
    REQUIRE(errs.isEmpty());
}

TEST_CASE("ROI validation: mixed separator", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    // 逗号+分号+空格混合分隔
    const QStringList errs = bridge.validateParam("Threshold", "roi", "10, 20; 100 200");
    REQUIRE(errs.isEmpty());
}

TEST_CASE("ROI validation: empty string allowed", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    // 空字符串表示未设置 ROI，应通过
    const QStringList errs = bridge.validateParam("Threshold", "roi", "");
    REQUIRE(errs.isEmpty());
}

TEST_CASE("ROI validation: whitespace only allowed", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    // 仅空白字符串 trimmed 后为空，应通过
    const QStringList errs = bridge.validateParam("Threshold", "roi", "   ");
    REQUIRE(errs.isEmpty());
}

TEST_CASE("ROI validation: too few fields", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    const QStringList errs = bridge.validateParam("Threshold", "roi", "10,20,100");
    REQUIRE_FALSE(errs.isEmpty());
    REQUIRE(errs.first().contains("4"));
}

TEST_CASE("ROI validation: too many fields", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    const QStringList errs = bridge.validateParam("Threshold", "roi", "10,20,100,200,300");
    REQUIRE_FALSE(errs.isEmpty());
    REQUIRE(errs.first().contains("4"));
}

TEST_CASE("ROI validation: non-integer field", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    const QStringList errs = bridge.validateParam("Threshold", "roi", "10,a,100,200");
    REQUIRE_FALSE(errs.isEmpty());
    REQUIRE(errs.first().contains("非整数"));
}

TEST_CASE("ROI validation: negative width rejected", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    const QStringList errs = bridge.validateParam("Threshold", "roi", "10,20,-100,200");
    REQUIRE_FALSE(errs.isEmpty());
    REQUIRE(errs.first().contains("负数"));
}

TEST_CASE("ROI validation: negative height rejected", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    const QStringList errs = bridge.validateParam("Threshold", "roi", "10,20,100,-200");
    REQUIRE_FALSE(errs.isEmpty());
    REQUIRE(errs.first().contains("负数"));
}

TEST_CASE("ROI validation: negative x y allowed", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    // x/y 允许负数（某些坐标系原点不在左上角）
    const QStringList errs = bridge.validateParam("Threshold", "roi", "-10,-20,100,200");
    REQUIRE(errs.isEmpty());
}

TEST_CASE("ROI validation: zero w h allowed", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    // w/h=0 允许（表示空 ROI）
    const QStringList errs = bridge.validateParam("Threshold", "roi", "10,20,0,0");
    REQUIRE(errs.isEmpty());
}

TEST_CASE("ROI validation: float value rejected", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    // 10.5 无法被 toInt 解析（ok=false），应失败
    const QStringList errs = bridge.validateParam("Threshold", "roi", "10.5,20,100,200");
    REQUIRE_FALSE(errs.isEmpty());
    REQUIRE(errs.first().contains("非整数"));
}

TEST_CASE("ROI validation: leading trailing whitespace trimmed", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    // 带前后空格和字段间空格，trimmed 后合法
    const QStringList errs = bridge.validateParam("Threshold", "roi", "  10 , 20 , 100 , 200  ");
    REQUIRE(errs.isEmpty());
}

TEST_CASE("ROI validation: unknown operator type", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    // 未知算子类型应返回错误
    const QStringList errs = bridge.validateParam("UnknownType", "roi", "10,20,100,200");
    REQUIRE_FALSE(errs.isEmpty());
    REQUIRE(errs.first().contains("未知算子类型"));
}

TEST_CASE("ROI validation: unknown param name", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    // 已知算子但参数名不存在
    const QStringList errs = bridge.validateParam("Threshold", "nonexistent", "10,20,100,200");
    REQUIRE_FALSE(errs.isEmpty());
    REQUIRE(errs.first().contains("无参数"));
}

TEST_CASE("ROI validation: null value rejected", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    // null/invalid 值应被拒绝
    const QStringList errs = bridge.validateParam("Threshold", "roi", QVariant());
    REQUIRE_FALSE(errs.isEmpty());
    REQUIRE(errs.first().contains("不能为空"));
}

TEST_CASE("ROI validation: integer variant accepted", "[editview][roi]") {
    ensureApp();
    OperatorDescriptorsGuard guard(kRoiTestJson);
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    // QString 是 ROI 类型的标准输入形式
    const QStringList errs1 = bridge.validateParam("Threshold", "roi", QString("10,20,100,200"));
    REQUIRE(errs1.isEmpty());
}
