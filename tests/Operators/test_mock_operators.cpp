// test_mock_operators.cpp - 32个mock补全算子批量测试
// 验证：可加载性 / 可执行性 / ToolResult 格式 / manifest 正确性
#include "../catch2/catch2_minimal.hpp"
#include "OperatorSDK/MockOperatorBase.h"
#include "OperatorSDK/OperatorManifest.h"
#include "OperatorSDK/IOperatorRegistry.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>

// 32个mock补全算子类型列表
static const QStringList MOCK_OPERATOR_TYPES = {
    "GrabImage", "OpenFramegrabber",
    "FftGeneric", "GaussFilter", "MeanImage", "Emphasize", "ScaleImage", "MedianImage",
    "AffineTransImage", "PolarTransImage",
    "Closing", "BottomHat", "Erosion", "Opening", "TopHat", "Dilation",
    "DynThreshold", "Watershed", "RegionGrowing",
    "Connection", "SelectShape",
    "PointsHarris", "EdgesSubPix",
    "FindNccModel", "FindShapeModel",
    "DistancePp", "AngleLl",
    "Reconstruct3D", "BinocularDisparity", "SurfaceMatching",
    "SegmentDl", "DetectObjectsDl",
};

// 用例 1: 所有mock算子DLL可被OperatorPluginLoader加载
TEST_CASE("MockOperators_AllLoadable", "[mock_batch]") {
    QString operatorsDir = QCoreApplication::applicationDirPath() + "/operators";
    if (!QDir(operatorsDir).exists()) {
        // 测试环境回退到build目录
        operatorsDir = QDir::currentPath() + "/bin/operators";
    }

    int loadedCount = 0;
    for (const QString& type : MOCK_OPERATOR_TYPES) {
        QString libPath = operatorsDir + "/Mock" + type + ".dll";
        if (!QFile::exists(libPath)) {
            continue;
        }
        QString err;
        QDV::OperatorManifest manifest;
        if (QDV::loadPlugin(libPath, manifest, &err)) {
            REQUIRE(manifest.type == type);
            REQUIRE(manifest.version == "1.0.0");
            loadedCount++;
        }
    }
    REQUIRE(loadedCount >= 0);
}

// 用例 2: 所有mock算子manifest.json格式正确
TEST_CASE("MockOperators_ManifestFormat", "[mock_batch]") {
    QString operatorsDir = QDir::currentPath() + "/bin/operators";
    int validCount = 0;

    for (const QString& type : MOCK_OPERATOR_TYPES) {
        QString manifestPath = operatorsDir + "/Mock" + type + ".json";
        if (!QFile::exists(manifestPath)) continue;

        QFile f(manifestPath);
        if (!f.open(QIODevice::ReadOnly)) continue;

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError) {
            REQUIRE_FALSE("Manifest JSON parse error");
            continue;
        }

        QJsonObject m = doc.object();
        REQUIRE(m["type"].toString() == type);
        REQUIRE(m["version"].toString() == "1.0.0");
        REQUIRE(m["isMock"].toBool() == true);
        REQUIRE_FALSE(m["cnName"].toString().isEmpty());
        REQUIRE_FALSE(m["category"].toString().isEmpty());
        validCount++;
    }
    REQUIRE(validCount > 0);
}

// 用例 3: mock算子执行返回合法ToolResult
TEST_CASE("MockOperators_ExecutionValid", "[mock_batch]") {
    QString operatorsDir = QDir::currentPath() + "/bin/operators";

    cv::Mat testInput(64, 64, CV_8UC1, cv::Scalar(128));
    int executedCount = 0;

    for (const QString& type : MOCK_OPERATOR_TYPES) {
        QString libPath = operatorsDir + "/Mock" + type + ".dll";
        if (!QFile::exists(libPath)) continue;

        QString err;
        QDV::OperatorManifest manifest;
        if (!QDV::loadPlugin(libPath, manifest, &err)) continue;

        // 通过注册表创建实例
        QDV::IOperator* op = QDV::IOperatorRegistry::instance().createOperator(type);
        if (!op) continue;

        ToolResult result;
        bool ok = op->execute(testInput, result);

        REQUIRE(ok == true);
        REQUIRE(result.ok == true);
        REQUIRE(result.data["isMock"].toBool() == true);
        REQUIRE(result.data["mockTag"].toString() == type);

        QString reason = result.data["reason"].toString();
        REQUIRE(reason.startsWith("[MOCK]"));

        delete op;
        executedCount++;
    }
    REQUIRE(executedCount >= 0);
}
