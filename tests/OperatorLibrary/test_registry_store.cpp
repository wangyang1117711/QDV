// =====================================================================
// OperatorRegistryStore 单元测试（spec §6.2）
// 覆盖 8 个核心用例：
//   1. 扫描空目录
//   2. 扫描含损坏文件
//   3. 保存覆盖（含 .bak）
//   4. 删除不存在（幂等）
//   5. init 创建嵌套目录
//   6. 原子写入无 .tmp 残留
//   7. tmp 文件跳过
//   8. 备份内容验证
// =====================================================================
#include "../catch2/catch2_minimal.hpp"
#include "OperatorLibrary/OperatorRegistryStore.h"
#include "OperatorLibrary/OperatorDefinition.h"

#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>

// 注意：不使用 `using namespace QDV::OperatorLibrary`，因为该命名空间内的 TestResult
// 会与 catch2_minimal.hpp 中的全局 TestResult 产生歧义
using QDV::OperatorLibrary::OperatorRegistryStore;
using QDV::OperatorLibrary::OperatorDef;
using QDV::OperatorLibrary::InputPort;
using QDV::OperatorLibrary::OutputPort;

// ---------------------------------------------------------------------
// 辅助：构造最小合法 OperatorDef
// ---------------------------------------------------------------------
static OperatorDef makeDef(const QString& type, const QString& cnName = "测试算子",
                           const QString& category = "预处理") {
    OperatorDef d;
    d.type = type;
    d.cnName = cnName;
    d.category = category;
    d.version = "1.0.0";
    InputPort in; in.name = "image"; in.cnName = "输入"; in.type = "Image"; in.required = true;
    d.inputs.append(in);
    OutputPort out; out.name = "result"; out.cnName = "输出"; out.type = "Image";
    d.outputs.append(out);
    return d;
}

// ---------------------------------------------------------------------
// 辅助：直接向磁盘写入 .qdvop 文件（绕过 save()，用于扫描测试）
// ---------------------------------------------------------------------
static void writeOpFile(const QString& dir, const QString& type, const OperatorDef& def) {
    QFile f(dir + "/" + type + ".qdvop");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(def.toJson()).toJson(QJsonDocument::Indented));
    f.close();
}

// =====================================================================
// 用例 1：扫描空目录
// =====================================================================
TEST_CASE("RegistryStore: scanAll on empty dir returns 0", "[op_library][registry]") {
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    auto* store = OperatorRegistryStore::instance();
    QString err;
    const bool ok = store->init(tmpDir.path(), &err);
    REQUIRE(ok);
    REQUIRE_EQUAL(store->all().size(), 0);
    REQUIRE(store->types().isEmpty());
    REQUIRE(store->categories().isEmpty());
}

// =====================================================================
// 用例 2：扫描含损坏文件，跳过损坏文件加载其他
// =====================================================================
TEST_CASE("RegistryStore: scanAll skips corrupted files", "[op_library][registry]") {
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    // 写一个合法 .qdvop
    writeOpFile(tmpDir.path(), "GoodOp", makeDef("GoodOp", "好算子"));

    // 写一个损坏 .qdvop（非法 JSON）
    QFile fBad(tmpDir.path() + "/BadOp.qdvop");
    REQUIRE(fBad.open(QIODevice::WriteOnly));
    fBad.write("not a valid json {{{");
    fBad.close();

    auto* store = OperatorRegistryStore::instance();
    REQUIRE(store->init(tmpDir.path()));

    REQUIRE_EQUAL(store->all().size(), 1);
    REQUIRE(store->contains("GoodOp"));
    REQUIRE_FALSE(store->contains("BadOp"));
}

// =====================================================================
// 用例 3：保存覆盖 + .bak 生成
// =====================================================================
TEST_CASE("RegistryStore: save overwrites existing and creates .bak", "[op_library][registry]") {
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    auto* store = OperatorRegistryStore::instance();
    REQUIRE(store->init(tmpDir.path()));

    // 第一次保存 v1
    OperatorDef v1 = makeDef("OverwriteOp", "v1");
    REQUIRE(store->save(v1));
    REQUIRE(QFile::exists(store->filePathFor("OverwriteOp")));

    // 第二次保存 v2（覆盖）
    OperatorDef v2 = makeDef("OverwriteOp", "v2");
    REQUIRE(store->save(v2));

    // .bak 应存在
    const QString bakPath = store->filePathFor("OverwriteOp") + ".bak";
    REQUIRE(QFile::exists(bakPath));

    // 内存缓存应是 v2
    bool ok = false;
    const OperatorDef cached = store->get("OverwriteOp", &ok);
    REQUIRE(ok);
    REQUIRE_EQUAL(cached.cnName.toStdString(), std::string("v2"));
}

// =====================================================================
// 用例 4：删除不存在的算子（幂等返回 true）
// =====================================================================
TEST_CASE("RegistryStore: remove nonexistent is idempotent", "[op_library][registry]") {
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    auto* store = OperatorRegistryStore::instance();
    REQUIRE(store->init(tmpDir.path()));

    // 不存在的算子也应返回 true（幂等）
    QString err;
    const bool ok = store->remove("NonexistentOp", &err);
    REQUIRE(ok);
    REQUIRE(err.isEmpty());
}

// =====================================================================
// 用例 5：init 创建嵌套目录（验证 mkpath 行为）
// =====================================================================
TEST_CASE("RegistryStore: init creates nested directory", "[op_library][registry]") {
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    const QString nested = tmpDir.path() + "/sub1/sub2/operators_imported";
    auto* store = OperatorRegistryStore::instance();
    REQUIRE(store->init(nested));

    // 目录应被自动创建
    REQUIRE(QDir(nested).exists());
    // store->directory() 应返回绝对路径
    REQUIRE_FALSE(store->directory().isEmpty());
}

// =====================================================================
// 用例 6：原子写入无 .tmp 残留
// =====================================================================
TEST_CASE("RegistryStore: atomic write leaves no .tmp", "[op_library][registry]") {
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    auto* store = OperatorRegistryStore::instance();
    REQUIRE(store->init(tmpDir.path()));

    OperatorDef def = makeDef("AtomicOp", "原子算子");
    REQUIRE(store->save(def));

    // .tmp 不应残留
    const QString tmpPath = store->filePathFor("AtomicOp") + ".tmp";
    REQUIRE_FALSE(QFile::exists(tmpPath));

    // 目标文件应存在
    REQUIRE(QFile::exists(store->filePathFor("AtomicOp")));
}

// =====================================================================
// 用例 7：扫描时跳过 .tmp / .bak 文件
// =====================================================================
TEST_CASE("RegistryStore: scanAll skips .tmp and .bak files", "[op_library][registry]") {
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    // 写一个真正的 .qdvop
    writeOpFile(tmpDir.path(), "RealOp", makeDef("RealOp", "真算子"));

    // 写一个 .qdvop.tmp（不会出现在 *.qdvop 通配符中，但防御性测试）
    QFile fTmp(tmpDir.path() + "/TempOp.qdvop.tmp");
    REQUIRE(fTmp.open(QIODevice::WriteOnly));
    fTmp.write("garbage");
    fTmp.close();

    // 写一个 .qdvop.bak（同上）
    QFile fBak(tmpDir.path() + "/BakOp.qdvop.bak");
    REQUIRE(fBak.open(QIODevice::WriteOnly));
    fBak.write("garbage");
    fBak.close();

    auto* store = OperatorRegistryStore::instance();
    REQUIRE(store->init(tmpDir.path()));

    // 应只加载 RealOp，TempOp/BakOp 不应进入缓存
    REQUIRE(store->contains("RealOp"));
    REQUIRE_FALSE(store->contains("TempOp"));
    REQUIRE_FALSE(store->contains("BakOp"));
    REQUIRE_EQUAL(store->all().size(), 1);
}

// =====================================================================
// 用例 8：备份文件包含上一版本内容
// =====================================================================
TEST_CASE("RegistryStore: backup contains previous content", "[op_library][registry]") {
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    auto* store = OperatorRegistryStore::instance();
    REQUIRE(store->init(tmpDir.path()));

    // 保存 v1
    OperatorDef v1 = makeDef("BackedOp", "旧版本");
    REQUIRE(store->save(v1));

    // 保存 v2（覆盖）
    OperatorDef v2 = makeDef("BackedOp", "新版本");
    REQUIRE(store->save(v2));

    // 读 .bak 应得 v1 内容
    const QString bakPath = store->filePathFor("BackedOp") + ".bak";
    QFile bak(bakPath);
    REQUIRE(bak.open(QIODevice::ReadOnly));
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(bak.readAll(), &parseErr);
    bak.close();

    REQUIRE(parseErr.error == QJsonParseError::NoError);
    REQUIRE(doc.isObject());
    REQUIRE_EQUAL(doc.object().value("cnName").toString().toStdString(),
                  std::string("旧版本"));
}

// =====================================================================
// 附加用例：删除成功后内存与磁盘一致
// =====================================================================
TEST_CASE("RegistryStore: remove clears memory and disk", "[op_library][registry]") {
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    auto* store = OperatorRegistryStore::instance();
    REQUIRE(store->init(tmpDir.path()));

    // 保存
    OperatorDef def = makeDef("RemoveMe", "待删除");
    REQUIRE(store->save(def));
    REQUIRE(store->contains("RemoveMe"));
    REQUIRE(QFile::exists(store->filePathFor("RemoveMe")));

    // 删除
    REQUIRE(store->remove("RemoveMe"));
    REQUIRE_FALSE(store->contains("RemoveMe"));
    REQUIRE_FALSE(QFile::exists(store->filePathFor("RemoveMe")));

    // 二次删除仍应幂等成功
    REQUIRE(store->remove("RemoveMe"));
}

// =====================================================================
// 附加用例：query 按分类+关键字过滤
// =====================================================================
TEST_CASE("RegistryStore: query filters by category and keyword", "[op_library][registry]") {
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    auto* store = OperatorRegistryStore::instance();
    REQUIRE(store->init(tmpDir.path()));

    REQUIRE(store->save(makeDef("MatchOp", "匹配算子", "图像分割")));
    REQUIRE(store->save(makeDef("OtherOp", "其他算子", "预处理")));

    // 分类过滤
    REQUIRE_EQUAL(store->byCategory("图像分割").size(), 1);
    REQUIRE_EQUAL(store->byCategory("预处理").size(), 1);
    REQUIRE_EQUAL(store->byCategory("不存在").size(), 0);

    // 关键字过滤（type 命中）
    REQUIRE_EQUAL(store->query("", "Match").size(), 1);

    // 关键字过滤（cnName 命中）
    REQUIRE_EQUAL(store->query("", "匹配").size(), 1);

    // 分类+关键字组合
    REQUIRE_EQUAL(store->query("图像分割", "Match").size(), 1);
    REQUIRE_EQUAL(store->query("预处理", "Match").size(), 0);
}
