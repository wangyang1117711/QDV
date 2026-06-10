#include "../catch2/catch2_minimal.hpp"
#include "Core/Scheme.h"
#include "Core/SchemeManager.h"
#include "Core/CameraConfig.h"
#include "Core/VisionTool.h"
#include <QJsonObject>
#include <QJsonArray>

using namespace QDV;

class MockTool : public VisionTool {
public:
    QString type() const override { return "Mock"; }
    bool execute(const cv::Mat& input, ToolResult& result) override {
        Q_UNUSED(input)
        result.ok = true;
        return true;
    }
};

TEST_CASE("Scheme constructor", "[scheme]") {
    Scheme scheme;
    REQUIRE_FALSE(scheme.id().isEmpty());
    REQUIRE_EQUAL(scheme.name().toStdString(), std::string("New Scheme"));
}

TEST_CASE("Scheme set name", "[scheme]") {
    Scheme scheme;
    scheme.setName("TestScheme");
    REQUIRE_EQUAL(scheme.name().toStdString(), std::string("TestScheme"));
}

TEST_CASE("Scheme serialize to JSON", "[scheme]") {
    Scheme scheme("JSONTest");
    scheme.setName("JSON Test");
    QJsonObject json = scheme.serialize();
    REQUIRE(json.contains("id"));
    REQUIRE_EQUAL(json["name"].toString().toStdString(), std::string("JSON Test"));
}

TEST_CASE("Scheme deserialize valid JSON", "[scheme]") {
    QJsonObject input;
    input["name"] = "Deserialized";
    input["id"] = "test-id-123";
    Scheme scheme;
    bool ok = scheme.deserialize(input);
    REQUIRE(ok);
    REQUIRE_EQUAL(scheme.name().toStdString(), std::string("Deserialized"));
    REQUIRE_EQUAL(scheme.id().toStdString(), std::string("test-id-123"));
}

TEST_CASE("Scheme deserialize invalid JSON", "[scheme]") {
    QJsonObject empty;
    Scheme scheme;
    bool ok = scheme.deserialize(empty);
    REQUIRE_FALSE(ok);
}

TEST_CASE("Scheme tool chain", "[scheme]") {
    Scheme scheme;
    REQUIRE_EQUAL(scheme.toolChain().size(), 0);
    MockTool* tool = new MockTool();
    QString toolId = tool->id();
    scheme.addTool(std::unique_ptr<VisionTool>(tool));
    REQUIRE_EQUAL(scheme.toolChain().size(), 1);
    scheme.removeTool(toolId);
    REQUIRE_EQUAL(scheme.toolChain().size(), 0);
}

TEST_CASE("Scheme unique ID", "[scheme]") {
    Scheme s1, s2;
    REQUIRE_FALSE(s1.id().isEmpty());
    REQUIRE_FALSE(s2.id().isEmpty());
    REQUIRE(s1.id() != s2.id());
}

TEST_CASE("Scheme tryDeserialize正常", "[scheme]") {
    QJsonObject input;
    input["id"] = "try-id-1";
    input["name"] = "TryDeserialize";
    input["version"] = "1.0";
    input["created"] = "2024-01-01T00:00:00";
    input["modified"] = "2024-01-01T00:00:00";
    Scheme scheme;
    auto result = scheme.tryDeserialize(input);
    REQUIRE(result.isOk());
    REQUIRE_EQUAL(scheme.name().toStdString(), std::string("TryDeserialize"));
    REQUIRE_EQUAL(scheme.id().toStdString(), std::string("try-id-1"));
}

TEST_CASE("Scheme tryDeserialize空JSON", "[scheme]") {
    QJsonObject empty;
    Scheme scheme;
    auto result = scheme.tryDeserialize(empty);
    REQUIRE(result.isErr());
    REQUIRE_FALSE(result.error().isEmpty());
}

TEST_CASE("Scheme tryDeserialize缺失字段", "[scheme]") {
    QJsonObject input;
    input["id"] = "missing-fields-id";
    input["name"] = "MissingFields";
    Scheme scheme;
    auto result = scheme.tryDeserialize(input);
    REQUIRE(result.isErr());
}

TEST_CASE("Scheme tryDeserialize类型错误", "[scheme]") {
    QJsonObject input;
    input["id"] = 12345;
    input["name"] = "TypeError";
    input["version"] = "1.0";
    input["created"] = "2024-01-01";
    input["modified"] = "2024-01-01";
    Scheme scheme;
    auto result = scheme.tryDeserialize(input);
    REQUIRE(result.isErr());
}

TEST_CASE("Scheme deserialize缺失name字段", "[scheme]") {
    QJsonObject input;
    input["id"] = "missing-name-id";
    input["version"] = "1.0";
    input["created"] = "2024-01-01";
    input["modified"] = "2024-01-01";
    Scheme scheme;
    bool ok = scheme.deserialize(input);
    REQUIRE_FALSE(ok);
}

TEST_CASE("Scheme deserialize id类型错误", "[scheme]") {
    QJsonObject input;
    input["name"] = "type-error";
    input["id"] = 12345;
    input["version"] = "1.0";
    input["created"] = "2024-01-01";
    input["modified"] = "2024-01-01";
    Scheme scheme;
    bool ok = scheme.deserialize(input);
    REQUIRE_FALSE(ok);
}

TEST_CASE("SchemeManager当前为空时saveCurrentScheme返回false", "[schememanager]") {
    SchemeManager* mgr = SchemeManager::instance();
    Scheme* prev = mgr->currentScheme();
    mgr->setCurrentScheme(nullptr);
    bool saved = mgr->saveCurrentScheme();
    REQUIRE_FALSE(saved);
    mgr->setCurrentScheme(prev);
}

TEST_CASE("SchemeManager loadScheme文件不存在返回false", "[schememanager]") {
    SchemeManager* mgr = SchemeManager::instance();
    bool loaded = mgr->loadScheme("nonexistent_file_12345.scheme");
    REQUIRE_FALSE(loaded);
}

TEST_CASE("Scheme 10个实例ID唯一", "[scheme]") {
    QList<Scheme*> schemes;
    for (int i = 0; i < 10; ++i) {
        schemes.append(new Scheme());
    }
    for (int i = 0; i < schemes.size(); ++i) {
        for (int j = i + 1; j < schemes.size(); ++j) {
            REQUIRE(schemes[i]->id() != schemes[j]->id());
        }
    }
    qDeleteAll(schemes);
}

TEST_CASE("Scheme filePath设置与获取", "[scheme]") {
    Scheme scheme;
    REQUIRE(scheme.filePath().isEmpty());
    scheme.setFilePath("C:/schemes/test.scheme");
    REQUIRE_EQUAL(scheme.filePath().toStdString(), std::string("C:/schemes/test.scheme"));
}

TEST_CASE("Scheme serialize包含modified字段", "[scheme]") {
    Scheme scheme("ModifiedTest");
    QJsonObject json = scheme.serialize();
    REQUIRE(json.contains("modified"));
    REQUIRE_FALSE(json["modified"].toString().isEmpty());
}

TEST_CASE("Scheme serialize包含created字段", "[scheme]") {
    Scheme scheme;
    QJsonObject json = scheme.serialize();
    REQUIRE(json.contains("created"));
    REQUIRE_FALSE(json["created"].toString().isEmpty());
}

TEST_CASE("Scheme cameraConfig默认非空", "[scheme]") {
    Scheme scheme;
    REQUIRE(scheme.cameraConfig() != nullptr);
}

TEST_CASE("Scheme cameraConfig设置后访问", "[scheme]") {
    Scheme scheme;
    CameraConfig* cfg = new CameraConfig();
    cfg->exposure = 10000;
    cfg->gain = 2.0;
    cfg->width = 2560;
    cfg->height = 1440;
    scheme.setCameraConfig(cfg);
    REQUIRE(scheme.cameraConfig() != nullptr);
    REQUIRE_EQUAL(scheme.cameraConfig()->exposure, 10000);
    REQUIRE_NEAR(scheme.cameraConfig()->gain, 2.0, 0.001);
    REQUIRE_EQUAL(scheme.cameraConfig()->width, 2560);
    REQUIRE_EQUAL(scheme.cameraConfig()->height, 1440);
}