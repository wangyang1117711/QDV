#include "../catch2/catch2_minimal.hpp"
#include "OperatorSDK/OperatorManifest.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

using namespace QDV;

// RT-002：动态库缺失 → loadPlugin 返回 false 不崩溃
TEST_CASE("OperatorPluginLoader: 不存在的 dll 返回 false (RT-002)", "[OperatorSDK][RT-002]") {
    OperatorManifest m;
    QString err;
    bool ok = loadPlugin("definitely_not_existing.dll", m, &err);
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(err.isEmpty());
    // 不崩溃即通过
}

// RT-003：错误路径写入 outError（间接验证 RT-003 的错误返回路径）
TEST_CASE("OperatorPluginLoader: 错误路径写入 outError (RT-003 配套)", "[OperatorSDK][RT-003]") {
    OperatorManifest m;
    QString err;
    bool ok = loadPlugin("another_missing_for_rt003.dll", m, &err);
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(err.isEmpty());
    // err 应包含 "library not found" 字样
    REQUIRE(err.contains("library not found"));
}

// RT-003 完整端到端验证：真实 Histogram.dll + manifest.json 版本匹配 → loadPlugin 成功
TEST_CASE("OperatorPluginLoader: 真实 Histogram.dll 加载成功 (RT-003 端到端)", "[OperatorSDK][RT-003]") {
    // 测试运行时 CWD 通常是 build/ 目录；Histogram.dll 在 build/bin/operators/Histogram.dll
    // 多候选路径以适配不同 CWD
    QStringList candidates = {
        QDir::currentPath() + "/bin/operators/Histogram.dll",
        QDir::currentPath() + "/operators/Histogram.dll",
        QDir::currentPath() + "/../bin/operators/Histogram.dll",
    };
    QString dllPath;
    for (const QString& p : candidates) {
        if (QFile::exists(p)) { dllPath = p; break; }
    }
    if (dllPath.isEmpty()) {
        // 测试环境未部署 dll，跳过（不失败）
        SUCCEED("Histogram.dll not deployed, skip end-to-end test");
        return;
    }
    OperatorManifest m;
    QString err;
    bool ok = loadPlugin(dllPath, m, &err);
    REQUIRE(ok);
    REQUIRE(m.type == "Histogram");
    REQUIRE(m.version == "1.0.0");
    REQUIRE(m.library == "Histogram.dll");
}

TEST_CASE("OperatorPluginLoader: loadAndRegister 不存在文件返回 false", "[OperatorSDK]") {
    QString err;
    bool ok = loadAndRegister("no_such_dll_for_register.dll", &err);
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(err.isEmpty());
}
