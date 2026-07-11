// test_mock_base.cpp - MockOperatorBase 单元测试
// 覆盖：占位图生成 / 参数校验 / ToolResult 格式 / 序列化 / 空输入处理

#include "../catch2/catch2_minimal.hpp"
#include "OperatorSDK/MockOperatorBase.h"
#include <QTemporaryFile>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/core/mat.hpp>

// 测试用的具体 mock 算子实现
class TestMockOperator : public QDV::MockOperatorBase {
public:
    TestMockOperator() { m_name = "测试Mock算子"; }
    QString type() const override { return "TestMock"; }
    IOperator* clone() const override { return new TestMockOperator(*this); }
protected:
    QString mockTag() const override { return "TestMock"; }
};

// 用例 1: 空输入图像时生成 64x64 灰度占位图
TEST_CASE("MockOperatorBase_EmptyInput_GeneratesPlaceholder", "[mock]") {
    TestMockOperator op;
    ToolResult result;
    cv::Mat emptyInput;

    bool ok = op.execute(emptyInput, result);

    REQUIRE(ok == true);
    REQUIRE(result.ok == true);
    REQUIRE(result.data["isMock"].toBool() == true);
    REQUIRE(result.data["mockTag"].toString() == "TestMock");
    REQUIRE(result.data["inputWidth"].toInt() == 0);
    REQUIRE(result.data["inputHeight"].toInt() == 0);
}

// 用例 2: 非空输入图像时返回输入副本
TEST_CASE("MockOperatorBase_NonEmptyInput_ReturnsCopy", "[mock]") {
    TestMockOperator op;
    ToolResult result;
    cv::Mat input = cv::Mat(100, 100, CV_8UC3, cv::Scalar(255, 0, 0));

    bool ok = op.execute(input, result);

    REQUIRE(ok == true);
    REQUIRE(result.data["inputWidth"].toInt() == 100);
    REQUIRE(result.data["inputHeight"].toInt() == 100);
    REQUIRE(result.data["inputChannels"].toInt() == 3);
    REQUIRE(result.overlayImage.cols == 100);
    REQUIRE(result.overlayImage.rows == 100);
}

// 用例 3: ToolResult 包含 [MOCK] 前缀的 reason
TEST_CASE("MockOperatorBase_ResultContainsMockPrefix", "[mock]") {
    TestMockOperator op;
    ToolResult result;
    cv::Mat input(64, 64, CV_8UC1, cv::Scalar(100));

    op.execute(input, result);

    QString reason = result.data["reason"].toString();
    REQUIRE(reason.startsWith("[MOCK]"));
    REQUIRE(reason.contains("TestMock"));
}

// 用例 4: 输出图像路径存在且可读
TEST_CASE("MockOperatorBase_OutputImagePath_Valid", "[mock]") {
    TestMockOperator op;
    ToolResult result;
    cv::Mat input(32, 32, CV_8UC1, cv::Scalar(50));

    op.execute(input, result);

    QString outPath = result.data["outputImagePath"].toString();
    REQUIRE_FALSE(outPath.isEmpty());
    REQUIRE(QFile::exists(outPath));

    // 验证输出图可读
    cv::Mat loaded = cv::imread(outPath.toStdString(), cv::IMREAD_UNCHANGED);
    REQUIRE_FALSE(loaded.empty());
}

// 用例 5: 序列化包含 isMock 和 mockTag 字段
TEST_CASE("MockOperatorBase_Serialize_IncludesMockFields", "[mock]") {
    TestMockOperator op;

    QJsonObject serialized = op.serialize();

    REQUIRE(serialized["isMock"].toBool() == true);
    REQUIRE(serialized["mockTag"].toString() == "TestMock");
    REQUIRE(serialized["type"].toString() == "TestMock");
}

// 用例 6: configure 保存参数供 execute 使用
TEST_CASE("MockOperatorBase_Configure_StoresParams", "[mock]") {
    TestMockOperator op;
    QJsonObject params;
    params["kernelSize"] = 5;
    params["sigma"] = 1.5;

    bool configOk = op.configure(params);

    REQUIRE(configOk == true);
}

// 用例 7: clone 返回独立副本
TEST_CASE("MockOperatorBase_Clone_ReturnsIndependentCopy", "[mock]") {
    TestMockOperator op1;
    QJsonObject params;
    params["testParam"] = "testValue";
    op1.configure(params);

    QDV::IOperator* cloned = op1.clone();

    REQUIRE(cloned != nullptr);
    REQUIRE(cloned->type() == "TestMock");
    REQUIRE(cloned->version() == "1.0.0");

    delete cloned;
}

// 用例 8: 反序列化恢复参数
TEST_CASE("MockOperatorBase_Deserialize_RestoresParams", "[mock]") {
    TestMockOperator op;
    QJsonObject data;
    data["type"] = "TestMock";
    data["name"] = "测试";
    data["id"] = "test-id-123";
    QJsonObject params;
    params["param1"] = "value1";
    data["params"] = params;

    bool ok = op.deserialize(data);

    REQUIRE(ok == true);
    REQUIRE(op.name() == "测试");
    REQUIRE(op.id() == "test-id-123");
}
