#include "../catch2/catch2_minimal.hpp"
#include "OperatorSDK/OperatorManifest.h"

using namespace QDV;

// RT-004：manifest 缺少必填字段（type/library 等）→ 返回 false，outError 记录缺失字段名
TEST_CASE("ManifestLoader: 缺少 type 字段 (RT-004)", "[OperatorSDK][RT-004]") {
    QJsonObject obj;
    obj["version"]  = "1.0.0";
    obj["cnName"]   = "测试";
    obj["category"] = "测试类";
    obj["library"]  = "Test.dll";
    QString err;
    OperatorManifest m = OperatorManifest::fromJson(obj, &err);
    REQUIRE_FALSE(err.isEmpty());
    REQUIRE(err.contains("type"));
    REQUIRE(err.contains("missing"));
}

TEST_CASE("ManifestLoader: 缺少 library 字段 (RT-004)", "[OperatorSDK][RT-004]") {
    QJsonObject obj;
    obj["type"]     = "Test";
    obj["version"]  = "1.0.0";
    obj["cnName"]   = "测试";
    obj["category"] = "测试类";
    // library 缺失
    QString err;
    OperatorManifest m = OperatorManifest::fromJson(obj, &err);
    REQUIRE_FALSE(err.isEmpty());
    REQUIRE(err.contains("library"));
}

TEST_CASE("ManifestLoader: 完整 JSON 解析成功", "[OperatorSDK]") {
    QJsonObject obj;
    obj["type"]        = "Histogram";
    obj["version"]     = "1.0.0";
    obj["cnName"]      = "直方图";
    obj["category"]    = "预处理";
    obj["iconPath"]    = "qrc:/icons/hist.svg";
    obj["description"] = "test";
    obj["library"]     = "Histogram.dll";
    QString err;
    OperatorManifest m = OperatorManifest::fromJson(obj, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(m.type == "Histogram");
    REQUIRE(m.version == "1.0.0");
    REQUIRE(m.library == "Histogram.dll");
}

TEST_CASE("ManifestLoader: validateManifest 校验必填", "[OperatorSDK][RT-004]") {
    OperatorManifest m;
    m.type = "X";
    m.version = "1.0.0";
    m.cnName = "X";
    m.category = "X";
    // library 空缺
    QString err;
    REQUIRE_FALSE(validateManifest(m, &err));
    REQUIRE(err.contains("library"));

    m.library = "X.dll";
    REQUIRE(validateManifest(m, &err));
}
