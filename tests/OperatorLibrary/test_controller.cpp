// =====================================================================
// OperatorLibraryController 集成测试（tasks.md Task 4.2）
//
// 覆盖：
//   - 6 个 Controller 单元测试（spec §6.2）
//   - 5 个集成测试（spec §6.3）
//
// 测试隔离策略：
//   - 每个用例使用独立 QTemporaryDir
//   - initController() 前调 OperatorDescriptors::reset() 清空外部注册表
//   - OperatorRegistryStore 单例通过 init() 重新初始化到新临时目录
// =====================================================================

#include "../catch2/catch2_minimal.hpp"
#include "OperatorLibrary/OperatorLibraryController.h"
#include "OperatorLibrary/OperatorRegistryStore.h"
#include "OperatorLibrary/OperatorDefinition.h"
#include "OperatorLibrary/OperatorImporter.h"
#include "UI/OperatorDescriptors.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

// 避免与 catch2 的 TestResult 同名冲突，使用具体 using 声明
using QDV::OperatorLibrary::OperatorLibraryController;
using QDV::OperatorLibrary::OperatorRegistryStore;
using QDV::OperatorLibrary::OperatorDef;
using QDV::OperatorLibrary::InputPort;
using QDV::OperatorLibrary::OutputPort;
using QDV::OperatorLibrary::Implementation;
using QDV::OperatorLibrary::ImportPreview;
using QDV::OperatorLibrary::ValidationIssue;
using QDV::UI::OperatorDescriptors;

// =====================================================================
// 辅助函数
// =====================================================================

/// 创建合法 OperatorDef
static OperatorDef makeValidDef(const QString& type) {
    OperatorDef def;
    def.type     = type;
    def.cnName   = QStringLiteral("测试算子_") + type;
    def.category = QStringLiteral("预处理");
    def.version  = QStringLiteral("1.0.0");
    def.author   = QStringLiteral("test");
    InputPort in;  in.name = "image";  in.cnName = QStringLiteral("输入图像"); in.type = "Image";
    def.inputs.append(in);
    OutputPort out; out.name = "result"; out.cnName = QStringLiteral("输出结果"); out.type = "Image";
    def.outputs.append(out);
    def.implementation.recipe.append("Threshold");
    return def;
}

/// 写入 .qdvop 文件到目录
static void writeOpFile(const QString& dir, const OperatorDef& def) {
    const QString path = dir + QStringLiteral("/") + def.type + QStringLiteral(".qdvop");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(def.toJson()).toJson(QJsonDocument::Indented));
    f.close();
}

/// 初始化 Controller 到临时目录（含 reset）
static void initController(const QString& dir) {
    // 重置 OperatorDescriptors 外部注册表（清空已导入算子，保留内置）
    OperatorDescriptors::reset();
    // Controller::init → RegistryStore::init + scanAll + registerExternalOperator
    OperatorLibraryController::instance()->init(dir, QString());
}

/// 获取一个内置算子 type（用于冲突测试）
static QString getBuiltinType() {
    const QStringList types = OperatorDescriptors::allTypes();
    if (types.isEmpty()) return QStringLiteral("Threshold");
    return types.first();
}

// =====================================================================
// 单元测试（6 个，spec §6.2）
// =====================================================================

TEST_CASE("Controller_init_加载算子到OperatorDescriptors", "[controller]") {
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    const QString dir = tmpDir.path();

    const QString type = QStringLiteral("UnitTestOp1");
    writeOpFile(dir, makeValidDef(type));

    initController(dir);

    // 验证：store 加载
    REQUIRE(OperatorRegistryStore::instance()->contains(type));
    // 验证：OperatorDescriptors 注册
    REQUIRE(OperatorDescriptors::has(type));
}

TEST_CASE("Controller_commitImport_成功路径", "[controller]") {
    QTemporaryDir sourceDir;
    QTemporaryDir storeDir;
    REQUIRE(sourceDir.isValid());
    REQUIRE(storeDir.isValid());

    initController(storeDir.path());  // 空目录

    const QString type = QStringLiteral("UnitTestOp2");
    writeOpFile(sourceDir.path(), makeValidDef(type));
    const QString filePath = sourceDir.path() + QStringLiteral("/") + type + QStringLiteral(".qdvop");

    // importFile
    QString err;
    ImportPreview preview = OperatorLibraryController::instance()->importFile(filePath, &err);
    REQUIRE(preview.conflicts.isEmpty());

    // commitImport
    REQUIRE(OperatorLibraryController::instance()->commitImport(preview, &err));

    // 验证：store 包含
    REQUIRE(OperatorRegistryStore::instance()->contains(type));
    // 验证：OperatorDescriptors 注册
    REQUIRE(OperatorDescriptors::has(type));
    // 验证：文件写入 storeDir
    REQUIRE(QFile::exists(storeDir.path() + QStringLiteral("/") + type + QStringLiteral(".qdvop")));
}

TEST_CASE("Controller_commitImport_TOCTOU防护", "[controller]") {
    QTemporaryDir sourceDir;
    QTemporaryDir storeDir;
    REQUIRE(sourceDir.isValid());
    REQUIRE(storeDir.isValid());

    initController(storeDir.path());

    const QString type = QStringLiteral("UnitTestOp3");
    writeOpFile(sourceDir.path(), makeValidDef(type));
    const QString filePath = sourceDir.path() + QStringLiteral("/") + type + QStringLiteral(".qdvop");

    // importFile（无冲突）
    QString err;
    ImportPreview preview = OperatorLibraryController::instance()->importFile(filePath, &err);
    REQUIRE(preview.conflicts.isEmpty());

    // 模拟 TOCTOU：在 commitImport 前手动 save 同 type 到 store
    REQUIRE(OperatorRegistryStore::instance()->save(makeValidDef(type), &err));
    REQUIRE(OperatorRegistryStore::instance()->contains(type));

    // commitImport 应失败（TOCTOU 检测）
    REQUIRE_FALSE(OperatorLibraryController::instance()->commitImport(preview, &err));
    // 错误信息应包含 TOCTOU
    REQUIRE(err.contains(QStringLiteral("TOCTOU"), Qt::CaseInsensitive));
}

TEST_CASE("Controller_commitImport_内置冲突", "[controller]") {
    QTemporaryDir sourceDir;
    QTemporaryDir storeDir;
    REQUIRE(sourceDir.isValid());
    REQUIRE(storeDir.isValid());

    initController(storeDir.path());

    // 构造与内置算子同 type 的 def
    const QString builtinType = getBuiltinType();
    OperatorDef def = makeValidDef(builtinType);
    writeOpFile(sourceDir.path(), def);
    const QString filePath = sourceDir.path() + QStringLiteral("/") + builtinType + QStringLiteral(".qdvop");

    // importFile 应检测到内置冲突
    QString err;
    ImportPreview preview = OperatorLibraryController::instance()->importFile(filePath, &err);
    REQUIRE_FALSE(preview.conflicts.isEmpty());

    // commitImport 应失败
    REQUIRE_FALSE(OperatorLibraryController::instance()->commitImport(preview, &err));
    // store 不应有该算子（type 与内置相同，但不应写入文件）
    REQUIRE_FALSE(QFile::exists(storeDir.path() + QStringLiteral("/") + builtinType + QStringLiteral(".qdvop")));
}

TEST_CASE("Controller_removeImported_成功", "[controller]") {
    QTemporaryDir sourceDir;
    QTemporaryDir storeDir;
    REQUIRE(sourceDir.isValid());
    REQUIRE(storeDir.isValid());

    initController(storeDir.path());

    // 先导入一个算子
    const QString type = QStringLiteral("UnitTestOp5");
    writeOpFile(sourceDir.path(), makeValidDef(type));
    const QString filePath = sourceDir.path() + QStringLiteral("/") + type + QStringLiteral(".qdvop");
    QString err;
    ImportPreview preview = OperatorLibraryController::instance()->importFile(filePath, &err);
    REQUIRE(OperatorLibraryController::instance()->commitImport(preview, &err));
    REQUIRE(OperatorRegistryStore::instance()->contains(type));

    // removeImported
    QVariantMap result = OperatorLibraryController::instance()->removeImported(type);
    REQUIRE(result.value("ok").toBool());

    // 验证：store 不再包含
    REQUIRE_FALSE(OperatorRegistryStore::instance()->contains(type));
    // 验证：文件已删除
    REQUIRE_FALSE(QFile::exists(storeDir.path() + QStringLiteral("/") + type + QStringLiteral(".qdvop")));
}

TEST_CASE("Controller_removeImported_不存在", "[controller]") {
    QTemporaryDir storeDir;
    REQUIRE(storeDir.isValid());
    initController(storeDir.path());

    QVariantMap result = OperatorLibraryController::instance()->removeImported(QStringLiteral("NonExistentOp"));
    REQUIRE_FALSE(result.value("ok").toBool());
    REQUIRE_FALSE(result.value("error").toString().isEmpty());
}

// =====================================================================
// 集成测试（5 个，spec §6.3）
// =====================================================================

TEST_CASE("集成_启动加载3个算子", "[controller][integration]") {
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    const QString dir = tmpDir.path();

    // 准备 3 个 .qdvop 文件
    const QString t1 = QStringLiteral("IntegrationOp1");
    const QString t2 = QStringLiteral("IntegrationOp2");
    const QString t3 = QStringLiteral("IntegrationOp3");
    writeOpFile(dir, makeValidDef(t1));
    writeOpFile(dir, makeValidDef(t2));
    writeOpFile(dir, makeValidDef(t3));

    initController(dir);

    // 验证：3 个 type 都在 OperatorDescriptors 中
    REQUIRE(OperatorDescriptors::has(t1));
    REQUIRE(OperatorDescriptors::has(t2));
    REQUIRE(OperatorDescriptors::has(t3));

    // 验证：store 加载 3 个
    REQUIRE_EQUAL(OperatorRegistryStore::instance()->types().size(), 3);
}

TEST_CASE("集成_导入后OperatorDescriptors可查询", "[controller][integration]") {
    QTemporaryDir sourceDir;
    QTemporaryDir storeDir;
    REQUIRE(sourceDir.isValid());
    REQUIRE(storeDir.isValid());
    initController(storeDir.path());

    const QString type = QStringLiteral("IntegrationQueryOp");
    const QString cnName = QStringLiteral("测试查询算子");
    OperatorDef def = makeValidDef(type);
    def.cnName = cnName;
    writeOpFile(sourceDir.path(), def);
    const QString filePath = sourceDir.path() + QStringLiteral("/") + type + QStringLiteral(".qdvop");

    QString err;
    ImportPreview preview = OperatorLibraryController::instance()->importFile(filePath, &err);
    REQUIRE(OperatorLibraryController::instance()->commitImport(preview, &err));

    // 验证：OperatorDescriptors::get 返回正确元数据
    auto meta = OperatorDescriptors::get(type);
    REQUIRE(meta.type == type);
    REQUIRE(meta.cnName == cnName);
    REQUIRE_FALSE(meta.params.isEmpty() && meta.outputs.isEmpty());
    // 验证：outputs 有至少 1 个（与 def.outputs 对齐）
    REQUIRE_FALSE(meta.outputs.isEmpty());
}

TEST_CASE("集成_删除后文件消失", "[controller][integration]") {
    QTemporaryDir sourceDir;
    QTemporaryDir storeDir;
    REQUIRE(sourceDir.isValid());
    REQUIRE(storeDir.isValid());
    initController(storeDir.path());

    const QString type = QStringLiteral("IntegrationDelOp");
    writeOpFile(sourceDir.path(), makeValidDef(type));
    const QString filePath = sourceDir.path() + QStringLiteral("/") + type + QStringLiteral(".qdvop");
    QString err;
    ImportPreview preview = OperatorLibraryController::instance()->importFile(filePath, &err);
    REQUIRE(OperatorLibraryController::instance()->commitImport(preview, &err));

    const QString storePath = storeDir.path() + QStringLiteral("/") + type + QStringLiteral(".qdvop");
    REQUIRE(QFile::exists(storePath));

    // 删除
    QVariantMap result = OperatorLibraryController::instance()->removeImported(type);
    REQUIRE(result.value("ok").toBool());

    // 验证：文件消失
    REQUIRE_FALSE(QFile::exists(storePath));
    // 验证：store 不再包含
    REQUIRE_FALSE(OperatorRegistryStore::instance()->contains(type));

    // 验证：重启后（重新 init）不再加载
    initController(storeDir.path());
    REQUIRE_FALSE(OperatorRegistryStore::instance()->contains(type));
}

TEST_CASE("集成_导入内置type被拒绝", "[controller][integration]") {
    QTemporaryDir sourceDir;
    QTemporaryDir storeDir;
    REQUIRE(sourceDir.isValid());
    REQUIRE(storeDir.isValid());
    initController(storeDir.path());

    const QString builtinType = getBuiltinType();
    OperatorDef def = makeValidDef(builtinType);
    writeOpFile(sourceDir.path(), def);
    const QString filePath = sourceDir.path() + QStringLiteral("/") + builtinType + QStringLiteral(".qdvop");

    QString err;
    ImportPreview preview = OperatorLibraryController::instance()->importFile(filePath, &err);

    // 验证：冲突非空
    REQUIRE_FALSE(preview.conflicts.isEmpty());

    // 验证：至少有一个冲突提到"内置"
    bool foundBuiltinConflict = false;
    for (const ValidationIssue& i : preview.conflicts) {
        if (i.message.contains(QStringLiteral("内置"))) {
            foundBuiltinConflict = true;
            break;
        }
    }
    REQUIRE(foundBuiltinConflict);

    // 验证：commitImport 失败
    REQUIRE_FALSE(OperatorLibraryController::instance()->commitImport(preview, &err));
}

TEST_CASE("集成_commitImport冲突时不写入文件", "[controller][integration]") {
    QTemporaryDir storeDir;
    REQUIRE(storeDir.isValid());
    initController(storeDir.path());

    // 构造有冲突的 ImportPreview（conflicts 非空）
    ImportPreview preview;
    preview.def = makeValidDef(QStringLiteral("ConflictTestOp"));
    ValidationIssue issue;
    issue.level   = ValidationIssue::Error;
    issue.field   = QStringLiteral("test");
    issue.message = QStringLiteral("人为构造的冲突");
    preview.conflicts.append(issue);

    // commitImport 应失败
    QString err;
    REQUIRE_FALSE(OperatorLibraryController::instance()->commitImport(preview, &err));

    // 验证：文件不存在（未写入）
    REQUIRE_FALSE(QFile::exists(storeDir.path() + QStringLiteral("/") + preview.def.type + QStringLiteral(".qdvop")));
    // 验证：store 不包含
    REQUIRE_FALSE(OperatorRegistryStore::instance()->contains(preview.def.type));
}
