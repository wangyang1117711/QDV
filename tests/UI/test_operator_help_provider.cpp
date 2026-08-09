// ============================================================================
// OperatorHelpProvider 单元测试（v2.8.0）
// ----------------------------------------------------------------------------
// 覆盖范围：
//   1. getShortDesc 优先返回 coreMeaning
//   2. getShortDesc 在 coreMeaning 为空时回退到 description
//   3. getFullDoc 返回完整文档（含所有字段）
//   4. getFullDoc 返回参数列表（params 数组）
//   5. getParamHelp 返回参数的 help 字段
//   6. getParamHelp 在 help 为空时回退到 "cnName (类型名)"
//   7. 不存在的算子返回空值
//
// 测试策略：
//   - 使用 QTemporaryDir 创建隔离的测试环境
//   - 通过 OperatorHelpProvider::reload(operatorsJsonPath, markdownDir)
//     注入测试专用路径，避免依赖生产环境的 config/operators.json
//   - 每个测试用例独立构造测试 JSON，确保确定性
//   - 测试结束调用 clearCache() 避免状态污染其他测试
//
// 测试框架：catch2_minimal（TEST_CASE 静态注册，REQUIRE/CHECK 宏）
// ============================================================================
#include "../catch2/catch2_minimal.hpp"
#include "UI/OperatorHelpProvider.h"

#include <QApplication>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>

// === QApplication 单例（UI 测试通用模式） ===
static int argc = 0;
static QApplication* app() {
    // 复用 test_main.cpp 中创建的全局 QApplication 实例
    return qobject_cast<QApplication*>(QCoreApplication::instance());
}
inline void ensureApp() { app(); }

// ============================================================================
// 测试辅助：在临时目录写入 operators.json
// 返回写入的文件绝对路径；失败返回空字符串
// ============================================================================
static QString writeOperatorsJson(const QDir& dir, const QString& jsonContent) {
    const QString path = dir.absoluteFilePath(QStringLiteral("operators.json"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    f.write(jsonContent.toUtf8());
    f.close();
    return path;
}

// ============================================================================
// 测试用 JSON：包含 3 个测试算子
//   - OpWithCore：有 coreMeaning/scenario/caveats，参数带 help
//   - OpNoCore：无 coreMeaning，仅有 description（测试回退）
//   - OpNoHelp：参数无 help 字段（测试 getParamHelp 回退）
// ParamType 枚举值：Int=0, Float=1, Enum=2, Bool=3, String=4, ROI=5, Vector=6
// ============================================================================
static const char* kTestOperatorsJson = R"({
    "operators": [
        {
            "type": "OpWithCore",
            "cnName": "带核心含义算子",
            "category": "测试",
            "description": "功能描述A",
            "coreMeaning": "核心含义A",
            "scenario": "场景A",
            "caveats": ["注意事项1", "注意事项2"],
            "params": [
                {"name": "param1", "cnName": "参数1", "type": 0, "defaultValue": 10, "help": "参数1的帮助文本", "minValue": 0, "maxValue": 100, "step": 1, "options": [], "optionKeys": [], "unit": "px"},
                {"name": "param2", "cnName": "参数2", "type": 4, "defaultValue": "", "help": "参数2的帮助文本", "minValue": null, "maxValue": null, "step": null, "options": [], "optionKeys": [], "unit": ""}
            ],
            "outputs": []
        },
        {
            "type": "OpNoCore",
            "cnName": "无核心含义算子",
            "category": "测试",
            "description": "描述Y",
            "coreMeaning": "",
            "scenario": "",
            "caveats": [],
            "params": [
                {"name": "threshold", "cnName": "阈值", "type": 0, "defaultValue": 50, "help": "", "minValue": 0, "maxValue": 255, "step": 1, "options": [], "optionKeys": [], "unit": ""}
            ],
            "outputs": []
        },
        {
            "type": "OpNoHelp",
            "cnName": "无帮助算子",
            "category": "测试",
            "description": "无帮助描述",
            "coreMeaning": "无帮助核心",
            "scenario": "无帮助场景",
            "caveats": [],
            "params": [
                {"name": "mode", "cnName": "模式", "type": 2, "defaultValue": "fast", "help": "", "minValue": null, "maxValue": null, "step": null, "options": ["快速","慢速"], "optionKeys": ["fast","slow"], "unit": ""}
            ],
            "outputs": []
        }
    ]
})";

// ============================================================================
// 测试辅助：RAII guard，构造时 reload 到指定路径，析构时 clearCache
// 保证测试间状态隔离
// ============================================================================
class HelpProviderGuard {
public:
    HelpProviderGuard(const QString& operatorsJsonPath, const QString& markdownDir) {
        OperatorHelpProvider::instance().reload(operatorsJsonPath, markdownDir);
    }
    ~HelpProviderGuard() {
        OperatorHelpProvider::instance().clearCache();
    }
};

// ============================================================================
// 测试用例 1：getShortDesc 优先返回 coreMeaning
// ============================================================================
TEST_CASE("OperatorHelpProvider getShortDesc 返回 coreMeaning", "[operator_help_provider]") {
    ensureApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    const QString jsonPath = writeOperatorsJson(tmpDir.path(), kTestOperatorsJson);
    REQUIRE(!jsonPath.isEmpty());

    HelpProviderGuard guard(jsonPath, tmpDir.path());  // markdownDir 不存在 .md 文件

    const QString desc = OperatorHelpProvider::instance().getShortDesc("OpWithCore");
    REQUIRE(desc == QStringLiteral("核心含义A"));
}

// ============================================================================
// 测试用例 2：getShortDesc 在 coreMeaning 为空时回退到 description
// ============================================================================
TEST_CASE("OperatorHelpProvider getShortDesc 回退到 description", "[operator_help_provider]") {
    ensureApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    const QString jsonPath = writeOperatorsJson(tmpDir.path(), kTestOperatorsJson);
    REQUIRE(!jsonPath.isEmpty());

    HelpProviderGuard guard(jsonPath, tmpDir.path());

    // OpNoCore 的 coreMeaning 为空，应回退到 description="描述Y"
    const QString desc = OperatorHelpProvider::instance().getShortDesc("OpNoCore");
    REQUIRE(desc == QStringLiteral("描述Y"));
}

// ============================================================================
// 测试用例 3：getFullDoc 返回完整文档
// 验证返回的 QVariantMap 包含所有预期字段
// ============================================================================
TEST_CASE("OperatorHelpProvider getFullDoc 返回完整文档", "[operator_help_provider]") {
    ensureApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    const QString jsonPath = writeOperatorsJson(tmpDir.path(), kTestOperatorsJson);
    REQUIRE(!jsonPath.isEmpty());

    HelpProviderGuard guard(jsonPath, tmpDir.path());

    const QVariantMap doc = OperatorHelpProvider::instance().getFullDoc("OpWithCore");
    REQUIRE(!doc.isEmpty());

    // 验证基础字段
    REQUIRE(doc.value("type").toString() == QStringLiteral("OpWithCore"));
    REQUIRE(doc.value("cnName").toString() == QStringLiteral("带核心含义算子"));
    REQUIRE(doc.value("category").toString() == QStringLiteral("测试"));
    REQUIRE(doc.value("description").toString() == QStringLiteral("功能描述A"));

    // 验证核心含义字段
    REQUIRE(doc.value("coreMeaning").toString() == QStringLiteral("核心含义A"));
    REQUIRE(doc.value("scenario").toString() == QStringLiteral("场景A"));

    // 验证 caveats 字段（QStringList）
    const QStringList caveats = doc.value("caveats").toStringList();
    REQUIRE(caveats.size() == 2);
    REQUIRE(caveats[0] == QStringLiteral("注意事项1"));
    REQUIRE(caveats[1] == QStringLiteral("注意事项2"));

    // 验证 relatedOperators 字段存在（无 Markdown 时为空列表）
    const QVariant relatedVar = doc.value("relatedOperators");
    REQUIRE(relatedVar.isValid());
    REQUIRE(relatedVar.toStringList().isEmpty());
}

// ============================================================================
// 测试用例 4：getFullDoc 返回参数列表
// 验证 params 数组数量与字段
// ============================================================================
TEST_CASE("OperatorHelpProvider getFullDoc 返回参数列表", "[operator_help_provider]") {
    ensureApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    const QString jsonPath = writeOperatorsJson(tmpDir.path(), kTestOperatorsJson);
    REQUIRE(!jsonPath.isEmpty());

    HelpProviderGuard guard(jsonPath, tmpDir.path());

    const QVariantMap doc = OperatorHelpProvider::instance().getFullDoc("OpWithCore");
    REQUIRE(!doc.isEmpty());

    // 验证 params 数组
    const QVariantList params = doc.value("params").toList();
    REQUIRE(params.size() == 2);

    // 验证第一个参数
    const QVariantMap p1 = params[0].toMap();
    REQUIRE(p1.value("name").toString() == QStringLiteral("param1"));
    REQUIRE(p1.value("cnName").toString() == QStringLiteral("参数1"));
    REQUIRE(p1.value("type").toInt() == 0);  // Int
    REQUIRE(p1.value("help").toString() == QStringLiteral("参数1的帮助文本"));

    // 验证第二个参数
    const QVariantMap p2 = params[1].toMap();
    REQUIRE(p2.value("name").toString() == QStringLiteral("param2"));
    REQUIRE(p2.value("cnName").toString() == QStringLiteral("参数2"));
    REQUIRE(p2.value("type").toInt() == 4);  // String
}

// ============================================================================
// 测试用例 5：getParamHelp 返回参数的 help 字段
// ============================================================================
TEST_CASE("OperatorHelpProvider getParamHelp 返回 help 字段", "[operator_help_provider]") {
    ensureApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    const QString jsonPath = writeOperatorsJson(tmpDir.path(), kTestOperatorsJson);
    REQUIRE(!jsonPath.isEmpty());

    HelpProviderGuard guard(jsonPath, tmpDir.path());

    // OpWithCore.param1 的 help 为 "参数1的帮助文本"
    const QString help = OperatorHelpProvider::instance().getParamHelp("OpWithCore", "param1");
    REQUIRE(help == QStringLiteral("参数1的帮助文本"));

    // 第二个参数也应返回其 help
    const QString help2 = OperatorHelpProvider::instance().getParamHelp("OpWithCore", "param2");
    REQUIRE(help2 == QStringLiteral("参数2的帮助文本"));
}

// ============================================================================
// 测试用例 6：getParamHelp 在 help 为空时回退到 "cnName (类型名)"
// ============================================================================
TEST_CASE("OperatorHelpProvider getParamHelp 回退到 cnName", "[operator_help_provider]") {
    ensureApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    const QString jsonPath = writeOperatorsJson(tmpDir.path(), kTestOperatorsJson);
    REQUIRE(!jsonPath.isEmpty());

    HelpProviderGuard guard(jsonPath, tmpDir.path());

    // OpNoCore.threshold 的 help 为空，cnName="阈值"，type=0(Int)
    // 预期返回 "阈值 (Int)"
    const QString help = OperatorHelpProvider::instance().getParamHelp("OpNoCore", "threshold");
    REQUIRE(help == QStringLiteral("阈值 (Int)"));

    // OpNoHelp.mode 的 help 为空，cnName="模式"，type=2(Enum)
    // 预期返回 "模式 (Enum)"
    const QString help2 = OperatorHelpProvider::instance().getParamHelp("OpNoHelp", "mode");
    REQUIRE(help2 == QStringLiteral("模式 (Enum)"));
}

// ============================================================================
// 测试用例 7：不存在的算子返回空值
// ============================================================================
TEST_CASE("OperatorHelpProvider 不存在的算子返回空值", "[operator_help_provider]") {
    ensureApp();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    const QString jsonPath = writeOperatorsJson(tmpDir.path(), kTestOperatorsJson);
    REQUIRE(!jsonPath.isEmpty());

    HelpProviderGuard guard(jsonPath, tmpDir.path());

    // getShortDesc 返回空字符串
    const QString desc = OperatorHelpProvider::instance().getShortDesc("NonExistentOperator");
    REQUIRE(desc.isEmpty());

    // getFullDoc 返回空 QVariantMap
    const QVariantMap doc = OperatorHelpProvider::instance().getFullDoc("NonExistentOperator");
    REQUIRE(doc.isEmpty());

    // getParamHelp 返回空字符串
    const QString help = OperatorHelpProvider::instance().getParamHelp("NonExistentOperator", "anyParam");
    REQUIRE(help.isEmpty());

    // 已存在算子但参数名不存在也应返回空
    const QString help2 = OperatorHelpProvider::instance().getParamHelp("OpWithCore", "nonExistentParam");
    REQUIRE(help2.isEmpty());
}
