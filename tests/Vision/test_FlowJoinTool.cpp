// FlowJoinTool 单元测试（v2.7.0 新算子）
//
// 覆盖范围：
//  - configure: waitBranches/timeoutMs 参数
//  - execute: 透传输入图像 + 标注 "JOIN"
//  - serialize/deserialize 往返
//  - 输入/输出端口元数据
//  - 默认值验证（timeoutMs=5000）
//
// 测试框架：项目自制 catch2_minimal.hpp

#include "../catch2/catch2_minimal.hpp"
#include "Vision/FlowJoinTool.h"

#include <QJsonObject>
#include <QVariantMap>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

using namespace QDV;

// 允许的浮点误差
static constexpr double EPS = 1e-9;

// 构造一个非空测试图像（100x100 BGR 灰色）
static cv::Mat makeTestImage() {
    return cv::Mat(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
}

// 构造一个单通道灰度图像
static cv::Mat makeGrayImage() {
    return cv::Mat(100, 100, CV_8UC1, cv::Scalar(128));
}

// =====================================================
// 基本属性
// =====================================================

TEST_CASE("FlowJoinTool: type() 返回 'FlowJoin'", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    REQUIRE(tool.type() == "FlowJoin");
}

TEST_CASE("FlowJoinTool: 默认 timeoutMs=5000", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("timeoutMs").toInt() == 5000);
}

TEST_CASE("FlowJoinTool: 默认 waitBranches 为空", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("waitBranches").toString() == "");
}

// =====================================================
// configure: waitBranches / timeoutMs 参数
// =====================================================

TEST_CASE("FlowJoinTool.configure: 设置 waitBranches", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    QJsonObject params;
    params["waitBranches"] = "branch1,branch2,branch3";
    REQUIRE(tool.configure(params));
    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("waitBranches").toString() == "branch1,branch2,branch3");
}

TEST_CASE("FlowJoinTool.configure: 设置 timeoutMs", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    QJsonObject params;
    params["timeoutMs"] = 3000;
    REQUIRE(tool.configure(params));
    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("timeoutMs").toInt() == 3000);
}

TEST_CASE("FlowJoinTool.configure: timeoutMs=0 被拒绝（保持默认 5000）", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    QJsonObject params;
    params["timeoutMs"] = 0;   // 非法
    REQUIRE(tool.configure(params));
    const QJsonObject saved = tool.serialize();
    // 非法值不被采纳，保持默认 5000
    REQUIRE(saved.value("timeoutMs").toInt() == 5000);
}

TEST_CASE("FlowJoinTool.configure: timeoutMs 负值被拒绝", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    QJsonObject params;
    params["timeoutMs"] = -100;
    REQUIRE(tool.configure(params));
    REQUIRE(tool.serialize().value("timeoutMs").toInt() == 5000);
}

TEST_CASE("FlowJoinTool.configure: 空参数保持默认值", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    QJsonObject empty;
    REQUIRE(tool.configure(empty));
    REQUIRE(tool.serialize().value("timeoutMs").toInt() == 5000);
    REQUIRE(tool.serialize().value("waitBranches").toString() == "");
}

TEST_CASE("FlowJoinTool.configure: 完整参数", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    QJsonObject params;
    params["waitBranches"] = "b1,b2";
    params["timeoutMs"] = 10000;
    REQUIRE(tool.configure(params));
    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("waitBranches").toString() == "b1,b2");
    REQUIRE(saved.value("timeoutMs").toInt() == 10000);
}

// =====================================================
// execute: 透传输入图像 + 标注 "JOIN"
// =====================================================

TEST_CASE("FlowJoinTool.execute: 透传 3 通道图像", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    QJsonObject params;
    params["waitBranches"] = "b1,b2";
    params["timeoutMs"] = 1000;
    REQUIRE(tool.configure(params));

    const cv::Mat img = makeTestImage();
    ToolResult result;
    REQUIRE(tool.execute(img, result));
    REQUIRE(result.ok);
    REQUIRE_FALSE(result.overlayImage.empty());
    // overlay 与输入同尺寸
    REQUIRE(result.overlayImage.size() == img.size());
    // overlay 是 3 通道
    REQUIRE(result.overlayImage.channels() == 3);
}

TEST_CASE("FlowJoinTool.execute: 单通道图像自动转 BGR", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    REQUIRE(tool.configure(QJsonObject{}));

    const cv::Mat gray = makeGrayImage();
    ToolResult result;
    REQUIRE(tool.execute(gray, result));
    REQUIRE(result.ok);
    REQUIRE_FALSE(result.overlayImage.empty());
    // 单通道输入 → 输出转为 3 通道 BGR（用于绘制彩色 "JOIN" 文字）
    REQUIRE(result.overlayImage.channels() == 3);
}

TEST_CASE("FlowJoinTool.execute: 空输入图像返回失败", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    REQUIRE(tool.configure(QJsonObject{}));

    cv::Mat empty;
    ToolResult result;
    REQUIRE_FALSE(tool.execute(empty, result));
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.data.contains("error"));
}

TEST_CASE("FlowJoinTool.execute: data 写入 waitBranches 和 timeoutMs", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    QJsonObject params;
    params["waitBranches"] = "b1,b2,b3";
    params["timeoutMs"] = 8000;
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.value("waitBranches").toString() == "b1,b2,b3");
    REQUIRE(result.data.value("timeoutMs").toInt() == 8000);
}

TEST_CASE("FlowJoinTool.execute: ports 写入 joined 和 waitBranches", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    QJsonObject params;
    params["waitBranches"] = "b1";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ports.contains("joined"));
    REQUIRE(result.ports.value("joined").toBool() == true);
    REQUIRE(result.ports.contains("waitBranches"));
    REQUIRE(result.ports.value("waitBranches").toString() == "b1");
}

TEST_CASE("FlowJoinTool.execute: overlay 标注 JOIN 文字", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    REQUIRE(tool.configure(QJsonObject{}));

    // 用纯黑图像，便于检测 JOIN 文字像素（黄色 (0,255,255)）
    cv::Mat img = cv::Mat::zeros(100, 100, CV_8UC3);
    ToolResult result;
    REQUIRE(tool.execute(img, result));
    REQUIRE_FALSE(result.overlayImage.empty());

    // 在 (10,30) 附近应有黄色像素（JOIN 文字）
    // 黄色 BGR = (0, 255, 255)
    bool foundYellow = false;
    for (int y = 0; y < 50 && !foundYellow; ++y) {
        for (int x = 0; x < 100 && !foundYellow; ++x) {
            const cv::Vec3b& px = result.overlayImage.at<cv::Vec3b>(y, x);
            if (px[0] == 0 && px[1] == 255 && px[2] == 255) {
                foundYellow = true;
            }
        }
    }
    REQUIRE(foundYellow);
}

TEST_CASE("FlowJoinTool.execute: 不修改输入图像（clone 而非原地）", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    REQUIRE(tool.configure(QJsonObject{}));

    cv::Mat img = makeTestImage();
    const cv::Mat original = img.clone();   // 保存原始内容
    ToolResult result;
    REQUIRE(tool.execute(img, result));
    // 输入图像不应被修改
    bool same = true;
    for (int y = 0; y < img.rows && same; ++y) {
        for (int x = 0; x < img.cols && same; ++x) {
            if (img.at<cv::Vec3b>(y, x) != original.at<cv::Vec3b>(y, x)) {
                same = false;
            }
        }
    }
    REQUIRE(same);
}

// =====================================================
// 输入/输出端口元数据
// =====================================================

TEST_CASE("FlowJoinTool.outputPorts: 声明 image/joined/waitBranches", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    const QList<PortDescriptor> ports = tool.outputPorts();
    REQUIRE(ports.size() == 3);
    // image 端口
    REQUIRE(ports.at(0).name == "image");
    REQUIRE(ports.at(0).type == PortType::Image);
    REQUIRE(ports.at(0).dir == PortDirection::Out);
    // joined 端口
    REQUIRE(ports.at(1).name == "joined");
    REQUIRE(ports.at(1).type == PortType::Bool);
    // waitBranches 端口
    REQUIRE(ports.at(2).name == "waitBranches");
    REQUIRE(ports.at(2).type == PortType::String);
}

TEST_CASE("FlowJoinTool.inputPorts: 声明 image 端口", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    const QList<PortDescriptor> ports = tool.inputPorts();
    REQUIRE(ports.size() == 1);
    REQUIRE(ports.at(0).name == "image");
    REQUIRE(ports.at(0).type == PortType::Image);
    REQUIRE(ports.at(0).dir == PortDirection::In);
}

// =====================================================
// serialize / deserialize 往返
// =====================================================

TEST_CASE("FlowJoinTool.serialize/deserialize: 完整往返", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    QJsonObject params;
    params["waitBranches"] = "branchA,branchB";
    params["timeoutMs"] = 7000;
    REQUIRE(tool.configure(params));

    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("type").toString() == "FlowJoin");
    REQUIRE(saved.value("waitBranches").toString() == "branchA,branchB");
    REQUIRE(saved.value("timeoutMs").toInt() == 7000);

    FlowJoinTool restored;
    REQUIRE(restored.deserialize(saved));
    REQUIRE(restored.serialize().value("waitBranches").toString() == "branchA,branchB");
    REQUIRE(restored.serialize().value("timeoutMs").toInt() == 7000);

    // 反序列化后执行行为一致
    ToolResult result;
    REQUIRE(restored.execute(makeTestImage(), result));
    REQUIRE(result.ok);
    REQUIRE(result.data.value("waitBranches").toString() == "branchA,branchB");
    REQUIRE(result.data.value("timeoutMs").toInt() == 7000);
}

TEST_CASE("FlowJoinTool.deserialize: timeoutMs=0 被拒绝（保持默认）", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    QJsonObject data;
    data["type"] = "FlowJoin";
    data["waitBranches"] = "b1";
    data["timeoutMs"] = 0;   // 非法
    REQUIRE(tool.deserialize(data));
    REQUIRE(tool.serialize().value("timeoutMs").toInt() == 5000);
}

TEST_CASE("FlowJoinTool.deserialize: 缺失字段保持默认", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    QJsonObject data;
    data["type"] = "FlowJoin";
    // 不提供 waitBranches/timeoutMs
    REQUIRE(tool.deserialize(data));
    REQUIRE(tool.serialize().value("waitBranches").toString() == "");
    REQUIRE(tool.serialize().value("timeoutMs").toInt() == 5000);
}

TEST_CASE("FlowJoinTool.deserialize: 基类字段（id/name）正确反序列化", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    QJsonObject data;
    data["type"] = "FlowJoin";
    data["id"] = "my-id";
    data["name"] = "my-name";
    data["waitBranches"] = "b1";
    data["timeoutMs"] = 2000;
    REQUIRE(tool.deserialize(data));
    REQUIRE(tool.id() == "my-id");
    REQUIRE(tool.name() == "my-name");
}

// =====================================================
// 综合场景：多分支配置 + 执行
// =====================================================

TEST_CASE("FlowJoinTool: 多分支等待场景", "[Vision][FlowJoinTool]") {
    FlowJoinTool tool;
    QJsonObject params;
    params["waitBranches"] = "acquisition,preprocess,dbload";
    params["timeoutMs"] = 3000;
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ok);
    // 验证所有分支信息写入 data
    const QString branches = result.data.value("waitBranches").toString();
    REQUIRE(branches.contains("acquisition"));
    REQUIRE(branches.contains("preprocess"));
    REQUIRE(branches.contains("dbload"));
}
