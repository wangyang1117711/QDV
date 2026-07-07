#include "../catch2/catch2_minimal.hpp"
#include "operators/extensions/Histogram/HistogramOperator.h"
#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>
#include <QJsonObject>
#include <QJsonArray>

using namespace QDV;

// 辅助：生成测试用灰度图
static cv::Mat makeGray(int w, int h, int baseValue) {
    cv::Mat m(h, w, CV_8UC1);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            m.at<uchar>(r, c) = static_cast<uchar>((baseValue + r + c) % 256);
    return m;
}

// RT-011: 空图像 → ok=false，不崩溃
TEST_CASE("HistogramOperator: 空图像返回 false (RT-011)", "[Operators][RT-011]") {
    HistogramOperator op;
    cv::Mat empty;
    ToolResult result;
    bool ok = op.execute(empty, result);
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(result.ok);
    REQUIRE_FALSE(result.data["error"].toString().isEmpty());
}

// RT-012: 非常规图像尺寸（1x1 和大图）
TEST_CASE("HistogramOperator: 1x1 图像正常处理 (RT-012)", "[Operators][RT-012]") {
    HistogramOperator op;
    cv::Mat img1x1(1, 1, CV_8UC1, cv::Scalar(128));
    ToolResult result;
    bool ok = op.execute(img1x1, result);
    REQUIRE(ok);
    REQUIRE(result.ok);
    QJsonArray hist = result.data["histogram"].toArray();
    REQUIRE(hist.size() == 256);
    // 1x1 图像只有一个像素值 128，对应 bin 应为 1
    int bin128 = hist[128].toDouble();
    REQUIRE(bin128 == 1);
}

TEST_CASE("HistogramOperator: 100x100 图像正常处理 (RT-012b)", "[Operators][RT-012]") {
    HistogramOperator op;
    cv::Mat img = makeGray(100, 100, 50);
    ToolResult result;
    bool ok = op.execute(img, result);
    REQUIRE(ok);
    REQUIRE(result.ok);
    QJsonArray hist = result.data["histogram"].toArray();
    REQUIRE(hist.size() == 256);
    // 总像素数应等于 100*100
    int total = 0;
    for (const auto& v : hist) total += v.toDouble();
    REQUIRE(total == 10000);
}

// RT-013: bins 超范围（0 或 10000）→ 使用默认 256
TEST_CASE("HistogramOperator: bins=0 使用默认 (RT-013)", "[Operators][RT-013]") {
    HistogramOperator op;
    QJsonObject params;
    params["mode"] = "gray";
    params["bins"] = 0;
    REQUIRE(op.configure(params));

    cv::Mat img = makeGray(20, 20, 10);
    ToolResult result;
    bool ok = op.execute(img, result);
    REQUIRE(ok);
    // bins<=0 应被改为默认 256
    REQUIRE(result.data["bins"].toInt() == 256);
    REQUIRE(result.data["histogram"].toArray().size() == 256);
}

TEST_CASE("HistogramOperator: bins=10000 使用默认 (RT-013b)", "[Operators][RT-013]") {
    HistogramOperator op;
    QJsonObject params;
    params["mode"] = "gray";
    params["bins"] = 10000;
    REQUIRE(op.configure(params));

    cv::Mat img = makeGray(20, 20, 10);
    ToolResult result;
    bool ok = op.execute(img, result);
    REQUIRE(ok);
    // bins>1024 应被改为默认 256
    REQUIRE(result.data["bins"].toInt() == 256);
}

// RT-014: 3 通道彩色图请求 gray 模式 → 自动转灰度
TEST_CASE("HistogramOperator: 彩色图 gray 模式自动转灰度 (RT-014)", "[Operators][RT-014]") {
    HistogramOperator op;
    QJsonObject params;
    params["mode"] = "gray";
    params["bins"] = 256;
    REQUIRE(op.configure(params));

    // 3 通道 BGR 彩色图
    cv::Mat color(20, 20, CV_8UC3, cv::Scalar(100, 150, 200));
    ToolResult result;
    bool ok = op.execute(color, result);
    REQUIRE(ok);
    REQUIRE(result.ok);
    REQUIRE(result.data["mode"].toString() == "gray");
    REQUIRE(result.data["channels"].toInt() == 1);
    // 灰度值 = 0.299*200 + 0.587*150 + 0.114*100 = 59.8 + 88.05 + 11.4 = 159.25 ≈ 159
    // BGR 顺序：B=100, G=150, R=200 → 灰度 = 0.114*100 + 0.587*150 + 0.299*200
    //       = 11.4 + 88.05 + 59.8 = 159.25 → 159
    QJsonArray hist = result.data["histogram"].toArray();
    int bin159 = hist[159].toDouble();
    REQUIRE(bin159 == 400);  // 20*20=400 个像素都在 bin 159
}

// RT-015: 均衡化后图像对比度增强，直方图分布更均匀
TEST_CASE("HistogramOperator: 均衡化对比度增强 (RT-015)", "[Operators][RT-015]") {
    HistogramOperator op;
    QJsonObject params;
    params["mode"] = "equalize";
    REQUIRE(op.configure(params));

    // 低对比度图像：所有像素集中在 100-150 范围
    cv::Mat lowContrast(100, 100, CV_8UC1);
    for (int r = 0; r < 100; ++r)
        for (int c = 0; c < 100; ++c)
            lowContrast.at<uchar>(r, c) = static_cast<uchar>(100 + (r + c) % 50);

    ToolResult result;
    bool ok = op.execute(lowContrast, result);
    REQUIRE(ok);
    REQUIRE(result.ok);
    REQUIRE(result.data["mode"].toString() == "equalize");

    // overlayImage 非空（均衡化后的图像）
    REQUIRE_FALSE(result.overlayImage.empty());

    // 均衡化前后直方图都存在
    QJsonArray before = result.data["histogram_before"].toArray();
    QJsonArray after = result.data["histogram_after"].toArray();
    REQUIRE(before.size() == 256);
    REQUIRE(after.size() == 256);

    // 验证：均衡化后直方图分布更均匀（标准差更小或非零 bin 更多）
    int beforeNonZeroBins = 0;
    int afterNonZeroBins = 0;
    for (int i = 0; i < 256; ++i) {
        if (before[i].toDouble() > 0) beforeNonZeroBins++;
        if (after[i].toDouble() > 0) afterNonZeroBins++;
    }
    // 均衡化后非零 bin 数应 >= 均衡化前（分布更均匀）
    REQUIRE(afterNonZeroBins >= beforeNonZeroBins);
}

// 额外：color 模式测试
TEST_CASE("HistogramOperator: color 模式三通道直方图", "[Operators]") {
    HistogramOperator op;
    QJsonObject params;
    params["mode"] = "color";
    params["bins"] = 256;
    REQUIRE(op.configure(params));

    cv::Mat color(20, 20, CV_8UC3, cv::Scalar(100, 150, 200));  // BGR
    ToolResult result;
    bool ok = op.execute(color, result);
    REQUIRE(ok);
    REQUIRE(result.data["channels"].toInt() == 3);
    REQUIRE(result.data.contains("histogram_b"));
    REQUIRE(result.data.contains("histogram_g"));
    REQUIRE(result.data.contains("histogram_r"));

    QJsonArray histB = result.data["histogram_b"].toArray();
    REQUIRE(histB.size() == 256);
    // B=100 通道，400 像素全在 bin 100
    REQUIRE(histB[100].toDouble() == 400);
}

// 额外：clone / serialize / deserialize
TEST_CASE("HistogramOperator: clone 多态克隆", "[Operators]") {
    HistogramOperator op;
    op.configure(QJsonObject{{"mode", "color"}, {"bins", 128}});
    IOperator* cloned = op.clone();
    REQUIRE(cloned != nullptr);
    REQUIRE(cloned->type() == "Histogram");
    REQUIRE(cloned->version() == "1.0.0");
    delete cloned;
}

TEST_CASE("HistogramOperator: serialize/deserialize 往返", "[Operators]") {
    HistogramOperator op;
    op.configure(QJsonObject{{"mode", "equalize"}, {"bins", 64}, {"rangeMin", 10.0}, {"rangeMax", 200.0}});
    QJsonObject serialized = op.serialize();
    REQUIRE(serialized["mode"].toString() == "equalize");
    REQUIRE(serialized["bins"].toInt() == 64);

    HistogramOperator op2;
    REQUIRE(op2.deserialize(serialized));
    // deserialize 后应能执行
    cv::Mat img = makeGray(20, 20, 50);
    ToolResult result;
    REQUIRE(op2.execute(img, result));
    REQUIRE(result.data["mode"].toString() == "equalize");
}
