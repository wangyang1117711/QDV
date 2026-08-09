#include "../catch2/catch2_minimal.hpp"
#include "Vision/NormalizationTool.h"
#include "Vision/ToolFactory.h"
#include <opencv2/core.hpp>
#include <QJsonArray>
#include <QJsonObject>
#include <cmath>

using namespace QDV;

// 允许浮点比较误差
static constexpr double EPS = 1e-4;

// 创建指定 CV 类型的测试图（单通道或 3 通道）
static cv::Mat makeTestMat(int rows, int cols, int channels, int cvType, double baseVal) {
    cv::Mat mat(rows, cols, cvType);
    if (channels == 1) {
        mat = cv::Scalar(baseVal);
    } else {
        mat = cv::Scalar(baseVal + 10, baseVal + 20, baseVal + 30);
    }
    return mat;
}

TEST_CASE("NormalizationTool: 工厂可创建", "[Vision][Normalization]") {
    VisionTool* tool = ToolFactory::instance()->createTool("Normalization");
    REQUIRE(tool != nullptr);
    REQUIRE(tool->type() == "Normalization");
    delete tool;
}

TEST_CASE("NormalizationTool: 空输入返回失败", "[Vision][Normalization]") {
    NormalizationTool tool;
    cv::Mat empty;
    ToolResult result;
    bool ok = tool.execute(empty, result);
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.data["error"].toString() == "Empty input");
}

TEST_CASE("NormalizationTool: MinMax [0,1] 单通道", "[Vision][Normalization]") {
    NormalizationTool tool;
    QJsonObject params;
    params["mode"] = "minMax";
    params["targetRange"] = "0_1";
    params["perChannel"] = true;
    tool.configure(params);

    cv::Mat input = (cv::Mat_<uchar>(2, 3) << 0, 50, 100, 150, 200, 255);
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE(ok);
    REQUIRE(result.ok);

    // 输出数据校验
    REQUIRE(result.data["mode"].toString() == "minMax");
    REQUIRE(result.data["targetRange"].toString() == "0_1");
    REQUIRE_NEAR(result.data["outputMin"].toDouble(), 0.0, EPS);
    REQUIRE_NEAR(result.data["outputMax"].toDouble(), 1.0, EPS);

    // 可视化图像应存在且为单通道 uint8
    REQUIRE(!result.overlayImage.empty());
    REQUIRE(result.overlayImage.channels() == 1);
    REQUIRE(result.overlayImage.depth() == CV_8U);
}

TEST_CASE("NormalizationTool: MinMax [-1,1] 全局缩放", "[Vision][Normalization]") {
    NormalizationTool tool;
    QJsonObject params;
    params["mode"] = "minMax";
    params["targetRange"] = "minus1_1";
    params["perChannel"] = false;
    tool.configure(params);

    cv::Mat input = (cv::Mat_<uchar>(2, 2) << 0, 128, 128, 255);
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE(ok);
    REQUIRE_NEAR(result.data["outputMin"].toDouble(), -1.0, EPS);
    REQUIRE_NEAR(result.data["outputMax"].toDouble(), 1.0, EPS);
}

TEST_CASE("NormalizationTool: 常量图避免除零", "[Vision][Normalization]") {
    NormalizationTool tool;
    QJsonObject params;
    params["mode"] = "zScore";
    params["mean"] = "";
    params["std"] = "";
    params["epsilon"] = 1e-5;
    tool.configure(params);

    cv::Mat input = makeTestMat(10, 10, 1, CV_8UC1, 128);
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE(ok);
    // 常量图标准化后接近 0（在 epsilon 保护下）
    REQUIRE_NEAR(result.data["outputMin"].toDouble(), 0.0, EPS);
    REQUIRE_NEAR(result.data["outputMax"].toDouble(), 0.0, EPS);
}

TEST_CASE("NormalizationTool: ZScore 自定义 mean/std", "[Vision][Normalization]") {
    NormalizationTool tool;
    QJsonObject params;
    params["mode"] = "zScore";
    params["mean"] = "128";
    params["std"] = "64";
    tool.configure(params);

    cv::Mat input = makeTestMat(4, 4, 1, CV_8UC1, 128);
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE(ok);
    REQUIRE_NEAR(result.data["outputMin"].toDouble(), 0.0, EPS);
    REQUIRE_NEAR(result.data["outputMax"].toDouble(), 0.0, EPS);
}

TEST_CASE("NormalizationTool: ImageNet 模式数值正确", "[Vision][Normalization]") {
    NormalizationTool tool;
    QJsonObject params;
    params["mode"] = "imageNet";
    tool.configure(params);

    // 构造一个 BGR 全 128 的图（注意 OpenCV 默认 BGR，但 ImageNet 对三通道分别处理）
    cv::Mat input(2, 2, CV_8UC3, cv::Scalar(128, 128, 128));
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE(ok);

    QJsonArray meanArr = result.data["mean"].toArray();
    QJsonArray stdArr = result.data["std"].toArray();
    REQUIRE(meanArr.size() == 3);
    REQUIRE(stdArr.size() == 3);
    REQUIRE_NEAR(meanArr[0].toDouble(), 0.485, EPS);
    REQUIRE_NEAR(stdArr[0].toDouble(), 0.229, EPS);
}

TEST_CASE("NormalizationTool: LayerNorm 全特征模式", "[Vision][Normalization]") {
    NormalizationTool tool;
    QJsonObject params;
    params["mode"] = "layerNorm";
    params["perChannel"] = false;
    tool.configure(params);

    cv::Mat input = makeTestMat(4, 4, 3, CV_8UC3, 50);
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE(ok);
    // 全特征模式下三个通道共享同一 mean/std
    QJsonArray meanArr = result.data["mean"].toArray();
    REQUIRE(meanArr.size() == 3);
    REQUIRE_NEAR(meanArr[0].toDouble(), meanArr[1].toDouble(), EPS);
    REQUIRE_NEAR(meanArr[1].toDouble(), meanArr[2].toDouble(), EPS);
}

TEST_CASE("NormalizationTool: BatchNorm 所有通道共享统计量", "[Vision][Normalization]") {
    NormalizationTool tool;
    QJsonObject params;
    params["mode"] = "batchNorm";
    tool.configure(params);

    // 三通道不同基值，确保存在可计算的统计量
    cv::Mat input(4, 4, CV_8UC3);
    cv::randu(input, cv::Scalar(0, 30, 60), cv::Scalar(50, 80, 110));

    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE(ok);

    QJsonArray meanArr = result.data["mean"].toArray();
    REQUIRE(meanArr.size() == 3);
    REQUIRE_NEAR(meanArr[0].toDouble(), meanArr[1].toDouble(), EPS);
    REQUIRE_NEAR(meanArr[1].toDouble(), meanArr[2].toDouble(), EPS);
}

TEST_CASE("NormalizationTool: 不同位深输入均可处理", "[Vision][Normalization]") {
    NormalizationTool tool;
    QJsonObject params;
    params["mode"] = "minMax";
    tool.configure(params);

    const int types[] = {CV_8UC1, CV_8UC3, CV_16UC1, CV_32FC1, CV_32FC3, CV_64FC1};
    for (int t : types) {
        cv::Mat input;
        if (t == CV_16UC1) {
            input = makeTestMat(4, 4, 1, t, 1000);
        } else if (t == CV_32FC1 || t == CV_64FC1) {
            input = makeTestMat(4, 4, 1, t, 0.5);
        } else {
            input = makeTestMat(4, 4, t == CV_8UC3 ? 3 : 1, t, 50);
        }

        ToolResult result;
        bool ok = tool.execute(input, result);
        REQUIRE(ok);
        REQUIRE(!result.overlayImage.empty());
    }
}

TEST_CASE("NormalizationTool: 序列化与反序列化保持参数", "[Vision][Normalization]") {
    NormalizationTool tool;
    QJsonObject params;
    params["mode"] = "instanceNorm";
    params["targetRange"] = "minus1_1";
    params["perChannel"] = false;
    params["mean"] = "0.5";
    params["std"] = "0.2";
    params["epsilon"] = 1e-6;
    tool.configure(params);

    QJsonObject saved = tool.serialize();
    REQUIRE(saved["type"].toString() == "Normalization");
    REQUIRE(saved["mode"].toString() == "instanceNorm");
    REQUIRE(saved["targetRange"].toString() == "minus1_1");
    REQUIRE(saved["perChannel"].toBool() == false);
    REQUIRE(saved["mean"].toString() == "0.5");
    REQUIRE(saved["std"].toString() == "0.2");
    REQUIRE_NEAR(saved["epsilon"].toDouble(), 1e-6, EPS);

    NormalizationTool restored;
    bool ok = restored.deserialize(saved);
    REQUIRE(ok);
    REQUIRE(restored.mode() == NormalizationTool::Mode::InstanceNorm);
}

TEST_CASE("NormalizationTool: epsilon <= 0 被重置", "[Vision][Normalization]") {
    NormalizationTool tool;
    QJsonObject params;
    params["mode"] = "zScore";
    params["epsilon"] = -1e-5;
    tool.configure(params);

    cv::Mat input = makeTestMat(4, 4, 1, CV_8UC1, 100);
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE(ok);
    // 仍能正常执行，因为 epsilon 被重置为 1e-5
    REQUIRE_NEAR(result.data["epsilon"].toDouble(), 1e-5, EPS);
}

TEST_CASE("NormalizationTool: 1x1 最小尺寸图像", "[Vision][Normalization]") {
    NormalizationTool tool;
    QJsonObject params;
    params["mode"] = "instanceNorm";
    tool.configure(params);

    cv::Mat input(1, 1, CV_8UC3, cv::Scalar(100, 150, 200));
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE(ok);
    REQUIRE(!result.overlayImage.empty());
    REQUIRE(result.overlayImage.rows == 1);
    REQUIRE(result.overlayImage.cols == 1);
}
