// LoopTool 单元测试（v2.7.0 升级覆盖）
//
// 覆盖范围：
//  - grid 模式：按 gridCols×gridRows 生成 ROI 列表
//  - list 模式：解析 roiList 字符串 "x,y,w,h;..."
//  - detection 模式：降级为 grid
//  - v2.7.0 count 模式：生成 N 个全图 ROI
//  - v2.7.0 while 模式：生成占位 ROI + isWhile 标记
//  - maxIterations 限制
//  - 序列化/反序列化往返
//  - 输出端口 roiList / count 元数据
//
// 测试框架：项目自制 catch2_minimal.hpp

#include "../catch2/catch2_minimal.hpp"
#include "Vision/LoopTool.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QVariantList>
#include <QVariantMap>
#include <opencv2/core.hpp>

using namespace QDV;

// 构造一个非空测试图像（200x200 BGR 灰色）
static cv::Mat makeTestImage() {
    return cv::Mat(200, 200, CV_8UC3, cv::Scalar(128, 128, 128));
}

// =====================================================
// 基本属性
// =====================================================

TEST_CASE("LoopTool: type() 返回 'Loop'", "[Vision][LoopTool]") {
    LoopTool tool;
    REQUIRE(tool.type() == "Loop");
}

TEST_CASE("LoopTool: 默认 mode 为 grid", "[Vision][LoopTool]") {
    LoopTool tool;
    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("mode").toString() == "grid");
}

// =====================================================
// grid 模式：生成 ROI 列表
// =====================================================

TEST_CASE("LoopTool.grid: 默认 3x3 生成 9 个 ROI", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    // 不设置 gridCols/gridRows → 默认 3x3
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ok);

    const QVariantList rois = result.ports.value("roiList").toList();
    REQUIRE(rois.size() == 9);
    // count 端口
    REQUIRE(result.ports.value("count").toInt() == 9);
    // data 通道
    REQUIRE(result.data.value("count").toInt() == 9);
    REQUIRE(result.data.value("mode").toString() == "grid");
}

TEST_CASE("LoopTool.grid: 自定义 gridCols/gridRows", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    params["gridCols"] = 4;
    params["gridRows"] = 2;
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));

    const QVariantList rois = result.ports.value("roiList").toList();
    REQUIRE(rois.size() == 8);   // 4*2
    // 每个 roi 含 x/y/w/h 字段
    const QVariantMap first = rois.at(0).toMap();
    REQUIRE(first.contains("x"));
    REQUIRE(first.contains("y"));
    REQUIRE(first.contains("w"));
    REQUIRE(first.contains("h"));
}

TEST_CASE("LoopTool.grid: 验证单元格尺寸正确", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    params["gridCols"] = 2;
    params["gridRows"] = 2;
    REQUIRE(tool.configure(params));

    // 200x200 图像，2x2 网格 → 每格 100x100
    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));

    const QVariantList rois = result.ports.value("roiList").toList();
    REQUIRE(rois.size() == 4);
    // 左上角单元格 (0,0) 100x100
    const QVariantMap tl = rois.at(0).toMap();
    REQUIRE(tl.value("x").toInt() == 0);
    REQUIRE(tl.value("y").toInt() == 0);
    REQUIRE(tl.value("w").toInt() == 100);
    REQUIRE(tl.value("h").toInt() == 100);
}

TEST_CASE("LoopTool.grid: overlap 参数生效", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    params["gridCols"] = 2;
    params["gridRows"] = 1;
    params["gridOverlap"] = 20;   // overlap 20px
    REQUIRE(tool.configure(params));

    // 200x100 图像，2x1 网格，基础 100x100，overlap=20 → 单元格向相邻方向扩展 10px
    cv::Mat img(100, 200, CV_8UC3, cv::Scalar(0));
    ToolResult result;
    REQUIRE(tool.execute(img, result));

    const QVariantList rois = result.ports.value("roiList").toList();
    REQUIRE(rois.size() == 2);
    // 第一个单元格 x=0, w=110（基础 100 + overlap/2）
    const QVariantMap first = rois.at(0).toMap();
    REQUIRE(first.value("x").toInt() == 0);
    REQUIRE(first.value("w").toInt() == 110);
}

TEST_CASE("LoopTool.grid: overlay 绘制 ROI 框", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    params["gridCols"] = 2;
    params["gridRows"] = 2;
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE_FALSE(result.overlayImage.empty());
    // overlay 应与输入同尺寸
    REQUIRE(result.overlayImage.cols == 200);
    REQUIRE(result.overlayImage.rows == 200);
}

// =====================================================
// list 模式：解析 roiList 字符串
// =====================================================

TEST_CASE("LoopTool.list: 解析 ROI 字符串", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "list";
    params["roiList"] = "10,10,50,50;60,60,30,30;100,100,20,20";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ok);

    const QVariantList rois = result.ports.value("roiList").toList();
    REQUIRE(rois.size() == 3);
    // 第一个 ROI
    const QVariantMap r1 = rois.at(0).toMap();
    REQUIRE(r1.value("x").toInt() == 10);
    REQUIRE(r1.value("y").toInt() == 10);
    REQUIRE(r1.value("w").toInt() == 50);
    REQUIRE(r1.value("h").toInt() == 50);
    // 第三个 ROI
    const QVariantMap r3 = rois.at(2).toMap();
    REQUIRE(r3.value("w").toInt() == 20);
}

TEST_CASE("LoopTool.list: 中文分隔符支持", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "list";
    // 中英文逗号/分号混用
    params["roiList"] = "1,2，3,4；5,6,7,8";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    const QVariantList rois = result.ports.value("roiList").toList();
    REQUIRE(rois.size() == 2);
}

TEST_CASE("LoopTool.list: 空字符串降级为 grid", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "list";
    params["roiList"] = "";   // 空
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    // 降级 grid → 默认 3x3=9 个
    const QVariantList rois = result.ports.value("roiList").toList();
    REQUIRE(rois.size() == 9);
    // effective mode 写回 data
    REQUIRE(result.data.value("mode").toString() == "grid");
}

TEST_CASE("LoopTool.list: 宽高非正被跳过", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "list";
    // 第二段 w=0 应被跳过
    params["roiList"] = "1,2,10,10;3,4,0,5;5,6,7,8";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    const QVariantList rois = result.ports.value("roiList").toList();
    REQUIRE(rois.size() == 2);
}

TEST_CASE("LoopTool.list: 段不足 4 个数字被跳过", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "list";
    params["roiList"] = "1,2,3;4,5,6,7,8";  // 第一段仅 3 个数字
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    const QVariantList rois = result.ports.value("roiList").toList();
    REQUIRE(rois.size() == 1);
}

// =====================================================
// detection 模式：降级为 grid
// =====================================================

TEST_CASE("LoopTool.detection: 降级为 grid", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "detection";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.ok);
    // 降级 grid 后应有 ROI 输出
    const QVariantList rois = result.ports.value("roiList").toList();
    REQUIRE_FALSE(rois.isEmpty());
    REQUIRE(result.data.value("mode").toString() == "grid");
}

// =====================================================
// v2.7.0 count 模式：生成 N 个全图 ROI
// =====================================================

TEST_CASE("LoopTool.count: 生成 N 个全图 ROI", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "count";
    params["count"] = 5;
    REQUIRE(tool.configure(params));

    cv::Mat img(100, 200, CV_8UC3, cv::Scalar(0));
    ToolResult result;
    REQUIRE(tool.execute(img, result));
    REQUIRE(result.ok);

    const QVariantList rois = result.ports.value("roiList").toList();
    REQUIRE(rois.size() == 5);
    // 每个 ROI 都是全图
    for (int i = 0; i < rois.size(); ++i) {
        const QVariantMap roi = rois.at(i).toMap();
        REQUIRE(roi.value("w").toInt() == 200);
        REQUIRE(roi.value("h").toInt() == 100);
        REQUIRE(roi.value("index").toInt() == i);   // 含迭代索引
    }
    REQUIRE(result.ports.value("count").toInt() == 5);
}

TEST_CASE("LoopTool.count: 默认 count=1", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "count";
    // 不设置 count → 默认 1
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    const QVariantList rois = result.ports.value("roiList").toList();
    REQUIRE(rois.size() == 1);
}

TEST_CASE("LoopTool.count: 上限钳制", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "count";
    params["count"] = 99999;   // 超上限
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    const QVariantList rois = result.ports.value("roiList").toList();
    // 上限 10000
    REQUIRE(rois.size() == 10000);
}

// =====================================================
// v2.7.0 while 模式：生成占位 ROI + isWhile 标记
// =====================================================

TEST_CASE("LoopTool.while: 生成占位 ROI 并标记 isWhile", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "while";
    params["whileCondition"] = "count>5";
    params["maxWhileIterations"] = 10;
    REQUIRE(tool.configure(params));

    cv::Mat img(100, 100, CV_8UC3, cv::Scalar(0));
    ToolResult result;
    REQUIRE(tool.execute(img, result));
    REQUIRE(result.ok);

    const QVariantList rois = result.ports.value("roiList").toList();
    REQUIRE(rois.size() == 10);
    // 每个 ROI 含 isWhile=true
    for (int i = 0; i < rois.size(); ++i) {
        const QVariantMap roi = rois.at(i).toMap();
        REQUIRE(roi.value("isWhile").toBool() == true);
        REQUIRE(roi.value("index").toInt() == i);
    }
}

TEST_CASE("LoopTool.while: 默认 maxWhileIterations", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "while";
    params["whileCondition"] = "1==1";
    // 不设置 maxWhileIterations → 默认 1000
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    const QVariantList rois = result.ports.value("roiList").toList();
    // 1000 钳制到 10000 上限以下，仍为 1000
    REQUIRE(rois.size() == 1000);
}

TEST_CASE("LoopTool.while: maxWhileIterations 上限钳制", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "while";
    params["whileCondition"] = "1==1";
    params["maxWhileIterations"] = 200000;   // 超上限 100000
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    const QVariantList rois = result.ports.value("roiList").toList();
    // maxWhileIterations 超过 100000 时不生效（保持默认 1000）
    REQUIRE(rois.size() == 1000);
}

// =====================================================
// maxIterations 限制
// =====================================================

TEST_CASE("LoopTool.maxIterations: 限制 grid 模式 ROI 数量", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    params["gridCols"] = 10;
    params["gridRows"] = 10;
    params["maxIterations"] = 5;   // 限制 5 个
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    const QVariantList rois = result.ports.value("roiList").toList();
    REQUIRE(rois.size() == 5);
}

TEST_CASE("LoopTool.maxIterations: data 通道记录", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    params["maxIterations"] = 50;
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.value("maxIterations").toInt() == 50);
}

// =====================================================
// 空输入图像
// =====================================================

TEST_CASE("LoopTool: 空输入图像返回失败", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    REQUIRE(tool.configure(params));

    cv::Mat empty;
    ToolResult result;
    REQUIRE_FALSE(tool.execute(empty, result));
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.data.contains("error"));
}

// =====================================================
// startIndex
// =====================================================

TEST_CASE("LoopTool.startIndex: 钳制到合法范围", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    params["gridCols"] = 2;
    params["gridRows"] = 2;
    params["startIndex"] = 1;
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    // 当前索引应为 1
    REQUIRE(result.data.value("currentIndex").toInt() == 1);
    REQUIRE(result.data.value("startIndex").toInt() == 1);
}

TEST_CASE("LoopTool.startIndex: 超出范围被钳制为 0", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    params["gridCols"] = 2;
    params["gridRows"] = 2;
    params["startIndex"] = 999;   // 超出
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.value("currentIndex").toInt() == 0);
}

TEST_CASE("LoopTool.startIndex: 负值被钳制为 0", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    params["startIndex"] = -5;
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.value("startIndex").toInt() == 0);
}

// =====================================================
// configure: 非法 mode 回退 grid
// =====================================================

TEST_CASE("LoopTool.configure: 非法 mode 回退 grid", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "weirdMode";
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    // 回退 grid → 默认 3x3=9 个
    REQUIRE(result.data.value("mode").toString() == "grid");
}

TEST_CASE("LoopTool.configure: gridCols 非法回退默认 3", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    params["gridCols"] = 0;   // 非法
    params["gridRows"] = -1;  // 非法
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    // 默认 3x3=9 个
    REQUIRE(result.ports.value("count").toInt() == 9);
    REQUIRE(result.data.value("gridCols").toInt() == 3);
    REQUIRE(result.data.value("gridRows").toInt() == 3);
}

TEST_CASE("LoopTool.configure: gridOverlap 负值回退 0", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    params["gridOverlap"] = -10;
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.value("gridOverlap").toInt() == 0);
}

TEST_CASE("LoopTool.configure: maxIterations 非法回退默认 100", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    params["maxIterations"] = 0;   // 非法
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    REQUIRE(result.data.value("maxIterations").toInt() == 100);
}

// =====================================================
// 端口元数据
// =====================================================

TEST_CASE("LoopTool.outputPorts: 声明 roiList 和 count 端口", "[Vision][LoopTool]") {
    LoopTool tool;
    const QList<PortDescriptor> ports = tool.outputPorts();
    REQUIRE(ports.size() == 2);
    // roiList 端口
    REQUIRE(ports.at(0).name == "roiList");
    REQUIRE(ports.at(0).type == PortType::Points);
    REQUIRE(ports.at(0).dir == PortDirection::Out);
    // count 端口
    REQUIRE(ports.at(1).name == "count");
    REQUIRE(ports.at(1).type == PortType::Number);
    REQUIRE(ports.at(1).dir == PortDirection::Out);
}

TEST_CASE("LoopTool.inputPorts: 声明 image 端口", "[Vision][LoopTool]") {
    LoopTool tool;
    const QList<PortDescriptor> ports = tool.inputPorts();
    REQUIRE(ports.size() == 1);
    REQUIRE(ports.at(0).name == "image");
    REQUIRE(ports.at(0).type == PortType::Image);
    REQUIRE(ports.at(0).dir == PortDirection::In);
}

// =====================================================
// 序列化 / 反序列化 往返
// =====================================================

TEST_CASE("LoopTool.serialize/deserialize: grid 模式往返", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    params["gridCols"] = 4;
    params["gridRows"] = 5;
    params["gridOverlap"] = 10;
    params["startIndex"] = 2;
    params["maxIterations"] = 200;
    REQUIRE(tool.configure(params));

    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("type").toString() == "Loop");
    REQUIRE(saved.value("mode").toString() == "grid");
    REQUIRE(saved.value("gridCols").toInt() == 4);
    REQUIRE(saved.value("gridRows").toInt() == 5);
    REQUIRE(saved.value("gridOverlap").toInt() == 10);
    REQUIRE(saved.value("startIndex").toInt() == 2);
    REQUIRE(saved.value("maxIterations").toInt() == 200);

    LoopTool restored;
    REQUIRE(restored.deserialize(saved));
    REQUIRE(restored.serialize().value("mode").toString() == "grid");
    REQUIRE(restored.serialize().value("gridCols").toInt() == 4);
    REQUIRE(restored.serialize().value("gridRows").toInt() == 5);

    // 反序列化后执行行为一致
    ToolResult result;
    REQUIRE(restored.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("count").toInt() == 20);   // 4*5
}

TEST_CASE("LoopTool.serialize/deserialize: list 模式往返", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "list";
    params["roiList"] = "1,2,3,4;5,6,7,8";
    REQUIRE(tool.configure(params));

    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("mode").toString() == "list");
    REQUIRE(saved.value("roiList").toString() == "1,2,3,4;5,6,7,8");

    LoopTool restored;
    REQUIRE(restored.deserialize(saved));
    REQUIRE(restored.serialize().value("roiList").toString() == "1,2,3,4;5,6,7,8");

    ToolResult result;
    REQUIRE(restored.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("count").toInt() == 2);
}

TEST_CASE("LoopTool.serialize/deserialize: count 模式往返", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "count";
    params["count"] = 7;
    REQUIRE(tool.configure(params));

    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("mode").toString() == "count");
    REQUIRE(saved.value("count").toInt() == 7);

    LoopTool restored;
    REQUIRE(restored.deserialize(saved));
    REQUIRE(restored.serialize().value("count").toInt() == 7);

    ToolResult result;
    REQUIRE(restored.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("count").toInt() == 7);
}

TEST_CASE("LoopTool.serialize/deserialize: while 模式往返", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "while";
    params["whileCondition"] = "i<10";
    params["maxWhileIterations"] = 50;
    REQUIRE(tool.configure(params));

    const QJsonObject saved = tool.serialize();
    REQUIRE(saved.value("mode").toString() == "while");
    REQUIRE(saved.value("whileCondition").toString() == "i<10");
    REQUIRE(saved.value("maxWhileIterations").toInt() == 50);

    LoopTool restored;
    REQUIRE(restored.deserialize(saved));
    REQUIRE(restored.serialize().value("whileCondition").toString() == "i<10");
    REQUIRE(restored.serialize().value("maxWhileIterations").toInt() == 50);

    ToolResult result;
    REQUIRE(restored.execute(makeTestImage(), result));
    REQUIRE(result.ports.value("count").toInt() == 50);
}

TEST_CASE("LoopTool.deserialize: 非法 mode 被忽略", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject data;
    data["type"] = "Loop";
    data["mode"] = "weirdMode";   // 非法，被忽略
    REQUIRE(tool.deserialize(data));
    // 保持默认 grid
    REQUIRE(tool.serialize().value("mode").toString() == "grid");
}

// =====================================================
// 端口输出结构完整性
// =====================================================

TEST_CASE("LoopTool: 输出 data 通道字段完整", "[Vision][LoopTool]") {
    LoopTool tool;
    QJsonObject params;
    params["mode"] = "grid";
    params["gridCols"] = 2;
    params["gridRows"] = 2;
    REQUIRE(tool.configure(params));

    ToolResult result;
    REQUIRE(tool.execute(makeTestImage(), result));
    // data 通道包含所有元数据
    REQUIRE(result.data.contains("mode"));
    REQUIRE(result.data.contains("count"));
    REQUIRE(result.data.contains("currentIndex"));
    REQUIRE(result.data.contains("startIndex"));
    REQUIRE(result.data.contains("maxIterations"));
    REQUIRE(result.data.contains("gridCols"));
    REQUIRE(result.data.contains("gridRows"));
    REQUIRE(result.data.contains("gridOverlap"));
}
