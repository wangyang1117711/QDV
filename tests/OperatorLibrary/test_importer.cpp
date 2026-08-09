// =====================================================================
// OperatorImporter 单元测试（spec §6.2，10 个用例）
// =====================================================================
#include "../catch2/catch2_minimal.hpp"
#include "OperatorLibrary/OperatorImporter.h"
#include "OperatorLibrary/OperatorValidator.h"
#include "OperatorLibrary/OperatorRegistryStore.h"
#include "OperatorLibrary/OperatorDefinition.h"

#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

using QDV::OperatorLibrary::OperatorImporter;
using QDV::OperatorLibrary::OperatorValidator;
using QDV::OperatorLibrary::OperatorRegistryStore;
using QDV::OperatorLibrary::OperatorDef;
using QDV::OperatorLibrary::InputPort;
using QDV::OperatorLibrary::OutputPort;
using QDV::OperatorLibrary::ParamDef;
using QDV::OperatorLibrary::ParamType;
using QDV::OperatorLibrary::ImportPreview;
using QDV::OperatorLibrary::ValidationIssue;

// ---------------------------------------------------------------------
// 辅助：构造合法 OperatorDef + JSON 文档
// ---------------------------------------------------------------------
static OperatorDef makeValidDef() {
    OperatorDef d;
    d.type = "ImportableOp";
    d.cnName = "可导入算子";
    d.category = "图像分割";
    d.version = "1.0.0";
    d.status = QDV::OperatorLibrary::OperatorStatus::Published;
    InputPort in; in.name = "image"; in.cnName = "输入"; in.type = "Image"; in.required = true;
    d.inputs.append(in);
    OutputPort out; out.name = "result"; out.cnName = "输出"; out.type = "Region";
    d.outputs.append(out);
    d.implementation.recipe = QStringList{"Threshold"};
    return d;
}

static QByteArray validJsonBytes() {
    return QJsonDocument(makeValidDef().toJson()).toJson(QJsonDocument::Indented);
}

// ---------------------------------------------------------------------
// 辅助：写入临时文件
// ---------------------------------------------------------------------
static QString writeTempFile(const QByteArray& content, const QString& suffix = ".qdvop") {
    QTemporaryFile f(QDir::tempPath() + "/qdv_test_XXXXXX" + suffix);
    f.setAutoRemove(false);
    if (!f.open()) return QString();
    f.write(content);
    f.close();
    return f.fileName();
}

// ---------------------------------------------------------------------
// 辅助：检查 preview 是否包含指定 field+level 的 issue
// ---------------------------------------------------------------------
static bool hasIssue(const QList<ValidationIssue>& issues, const QString& field) {
    for (const ValidationIssue& i : issues) {
        if (i.field == field) return true;
    }
    return false;
}

// =====================================================================
// 用例 1：合法文件 → 无冲突无警告
// =====================================================================
TEST_CASE("Importer: valid file yields clean preview", "[op_library][importer]") {
    const QString path = writeTempFile(validJsonBytes());
    REQUIRE_FALSE(path.isEmpty());

    QString err;
    const ImportPreview preview = OperatorImporter::importFile(path, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(preview.conflicts.isEmpty());
    REQUIRE(preview.warnings.isEmpty());
    REQUIRE_EQUAL(preview.def.type.toStdString(), std::string("ImportableOp"));

    QFile::remove(path);
}

// =====================================================================
// 用例 2：空文件 → JSON 解析失败
// =====================================================================
TEST_CASE("Importer: empty file yields JSON error", "[op_library][importer]") {
    const QString path = writeTempFile(QByteArray());
    REQUIRE_FALSE(path.isEmpty());

    QString err;
    const ImportPreview preview = OperatorImporter::importFile(path, &err);
    REQUIRE_FALSE(err.isEmpty());
    REQUIRE_FALSE(preview.conflicts.isEmpty());
    REQUIRE(hasIssue(preview.conflicts, "json"));

    QFile::remove(path);
}

// =====================================================================
// 用例 3：JSON 损坏 → 解析失败含错误信息
// =====================================================================
TEST_CASE("Importer: corrupted JSON yields parse error", "[op_library][importer]") {
    const QString path = writeTempFile(QByteArray("{ not valid json {{{"));
    REQUIRE_FALSE(path.isEmpty());

    QString err;
    const ImportPreview preview = OperatorImporter::importFile(path, &err);
    REQUIRE_FALSE(err.isEmpty());
    REQUIRE_FALSE(preview.conflicts.isEmpty());
    REQUIRE(hasIssue(preview.conflicts, "json"));

    QFile::remove(path);
}

// =====================================================================
// 用例 4：type 重复（mock store）→ detectConflicts 返回冲突
// =====================================================================
TEST_CASE("Importer: detectConflicts finds duplicate type", "[op_library][importer]") {
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    auto* store = OperatorRegistryStore::instance();
    REQUIRE(store->init(tmpDir.path()));

    // 预先导入一个同 type 算子
    OperatorDef existing = makeValidDef();
    REQUIRE(store->save(existing));

    // 检测冲突
    const QList<ValidationIssue> conflicts = OperatorImporter::detectConflicts(makeValidDef(), store);
    REQUIRE_FALSE(conflicts.isEmpty());
    REQUIRE(hasIssue(conflicts, "type"));
}

// =====================================================================
// 用例 5：字段类型错误（type 含非法字符）→ 校验失败
// =====================================================================
TEST_CASE("Importer: invalid type yields validation conflict", "[op_library][importer]") {
    OperatorDef d = makeValidDef();
    d.type = "_Invalid";  // 下划线开头
    const QByteArray json = QJsonDocument(d.toJson()).toJson();

    const QString path = writeTempFile(json);
    REQUIRE_FALSE(path.isEmpty());

    QString err;
    const ImportPreview preview = OperatorImporter::importFile(path, &err);
    REQUIRE_FALSE(preview.conflicts.isEmpty());
    REQUIRE(hasIssue(preview.conflicts, "type"));

    QFile::remove(path);
}

// =====================================================================
// 用例 6：Implementation 全空 → 校验失败
// =====================================================================
TEST_CASE("Importer: empty implementation yields conflict", "[op_library][importer]") {
    OperatorDef d = makeValidDef();
    d.implementation.recipe.clear();
    d.implementation.script.clear();
    d.implementation.library.clear();
    const QByteArray json = QJsonDocument(d.toJson()).toJson();

    const QString path = writeTempFile(json);
    REQUIRE_FALSE(path.isEmpty());

    QString err;
    const ImportPreview preview = OperatorImporter::importFile(path, &err);
    REQUIRE_FALSE(preview.conflicts.isEmpty());
    REQUIRE(hasIssue(preview.conflicts, "implementation"));

    QFile::remove(path);
}

// =====================================================================
// 用例 7：文件不存在
// =====================================================================
TEST_CASE("Importer: nonexistent file yields file error", "[op_library][importer]") {
    const QString path = QDir::tempPath() + "/qdv_nonexistent_"
                         + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".qdvop";
    REQUIRE_FALSE(QFile::exists(path));

    QString err;
    const ImportPreview preview = OperatorImporter::importFile(path, &err);
    REQUIRE_FALSE(err.isEmpty());
    REQUIRE_FALSE(preview.conflicts.isEmpty());
    REQUIRE(hasIssue(preview.conflicts, "file"));
}

// =====================================================================
// 用例 8：importFromJson 直接调用（跳过文件读取）
// =====================================================================
TEST_CASE("Importer: importFromJson works without file", "[op_library][importer]") {
    const QJsonObject obj = makeValidDef().toJson();
    QString err;
    const ImportPreview preview = OperatorImporter::importFromJson(obj, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(preview.conflicts.isEmpty());
    REQUIRE_EQUAL(preview.def.type.toStdString(), std::string("ImportableOp"));
}

// =====================================================================
// 用例 9：importFromJson 处理 type 缺失
// =====================================================================
TEST_CASE("Importer: importFromJson with missing type returns conflict", "[op_library][importer]") {
    QJsonObject obj = makeValidDef().toJson();
    obj.remove("type");
    QString err;
    const ImportPreview preview = OperatorImporter::importFromJson(obj, &err);
    REQUIRE_FALSE(err.isEmpty());
    REQUIRE_FALSE(preview.conflicts.isEmpty());
    REQUIRE(hasIssue(preview.conflicts, "type"));
    REQUIRE(preview.def.type.isEmpty());
}

// =====================================================================
// 用例 10：Warning 不应进入 conflicts（以 category 不在词表为例）
// =====================================================================
TEST_CASE("Importer: Warning goes to warnings not conflicts", "[op_library][importer]") {
    OperatorDef d = makeValidDef();
    d.category = "自定义分类";  // 不在词表 → Warning
    const QByteArray json = QJsonDocument(d.toJson()).toJson();

    const QString path = writeTempFile(json);
    REQUIRE_FALSE(path.isEmpty());

    QString err;
    const ImportPreview preview = OperatorImporter::importFile(path, &err);
    REQUIRE(preview.conflicts.isEmpty());     // 无冲突
    REQUIRE_FALSE(preview.warnings.isEmpty()); // 有警告
    REQUIRE(hasIssue(preview.warnings, "category"));

    QFile::remove(path);
}

// =====================================================================
// 附加用例：detectConflicts 在 store 为空时返回空
// =====================================================================
TEST_CASE("Importer: detectConflicts returns empty for unique type", "[op_library][importer]") {
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    auto* store = OperatorRegistryStore::instance();
    REQUIRE(store->init(tmpDir.path()));

    // store 为空，任何 type 都应无冲突
    const QList<ValidationIssue> conflicts = OperatorImporter::detectConflicts(makeValidDef(), store);
    REQUIRE(conflicts.isEmpty());
}

// =====================================================================
// 附加用例：detectConflicts 处理 nullptr store
// =====================================================================
TEST_CASE("Importer: detectConflicts handles null store", "[op_library][importer]") {
    const QList<ValidationIssue> conflicts = OperatorImporter::detectConflicts(makeValidDef(), nullptr);
    REQUIRE(conflicts.isEmpty());
}
