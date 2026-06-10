#include "Vision/ToolChainVerifier.h"
#include "Core/VisionTool.h"
#include "Vision/ToolFactory.h"
#include "Vision/ToolChainExecutor.h"
#include <QElapsedTimer>
#include <opencv2/imgproc.hpp>

using namespace QDV;

ToolChainVerifier::ToolChainVerifier() {
}

cv::Mat ToolChainVerifier::createTestImage(int width, int height) {
    cv::Mat image(height, width, CV_8UC3, cv::Scalar(60, 60, 60));
    cv::rectangle(image, cv::Rect(width / 4, height / 4, width / 2, height / 2),
                  cv::Scalar(200, 200, 200), -1);
    cv::rectangle(image, cv::Rect(width / 8, height / 8, width / 16, height / 16),
                  cv::Scalar(0, 0, 255), -1);
    cv::circle(image, cv::Point(width * 3 / 4, height * 3 / 4),
               std::min(width, height) / 12, cv::Scalar(0, 255, 0), -1);
    return image;
}

cv::Mat ToolChainVerifier::createColorTestImage() {
    cv::Mat image(200, 300, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::rectangle(image, cv::Rect(0, 0, 100, 200), cv::Scalar(0, 0, 255), -1);
    cv::rectangle(image, cv::Rect(100, 0, 100, 200), cv::Scalar(0, 255, 0), -1);
    cv::rectangle(image, cv::Rect(200, 0, 100, 200), cv::Scalar(255, 0, 0), -1);
    return image;
}

cv::Mat ToolChainVerifier::createPatternTestImage() {
    cv::Mat image(200, 200, CV_8UC1, cv::Scalar(40));
    cv::rectangle(image, cv::Rect(50, 50, 100, 100), cv::Scalar(200), -1);
    cv::circle(image, cv::Point(100, 100), 30, cv::Scalar(255), 2);
    return image;
}

cv::Mat ToolChainVerifier::createGeometricTestImage() {
    cv::Mat image(300, 300, CV_8UC3, cv::Scalar(50, 50, 50));
    cv::rectangle(image, cv::Rect(50, 50, 200, 150), cv::Scalar(255, 255, 255), 2);
    cv::circle(image, cv::Point(200, 200), 80, cv::Scalar(255, 255, 255), 2);
    cv::line(image, cv::Point(10, 280), cv::Point(290, 20), cv::Scalar(255, 255, 255), 2);
    return image;
}

cv::Mat ToolChainVerifier::createLineCircleTestImage() {
    cv::Mat image(300, 300, CV_8UC1, cv::Scalar(0));
    cv::line(image, cv::Point(20, 150), cv::Point(280, 150), cv::Scalar(255), 2);
    cv::line(image, cv::Point(150, 20), cv::Point(150, 280), cv::Scalar(255), 2);
    cv::circle(image, cv::Point(100, 100), 40, cv::Scalar(255), 2);
    cv::circle(image, cv::Point(200, 200), 60, cv::Scalar(255), 2);
    return image;
}

QList<ToolVerifyResult> ToolChainVerifier::verifyAll() {
    m_results.clear();
    m_results.append(verifyTemplateMatch());
    m_results.append(verifyEdgeDetect());
    m_results.append(verifyBlobDetect());
    m_results.append(verifyColorDetect());
    m_results.append(verifyThreshold());
    m_results.append(verifyImagePreprocess());
    m_results.append(verifyContourAnalyze());
    m_results.append(verifyGeometryMeasure());
    m_results.append(verifyLineCircleDetect());
    m_results.append(verifyImageArithmetic());
    m_results.append(verifyImageTransform());
    m_results.append(verifyImageMerge());
    m_results.append(verifyBranchControl());
    return m_results;
}

ToolVerifyResult ToolChainVerifier::verifyTemplateMatch() {
    ToolVerifyResult r;
    r.toolName = "模板匹配";

    cv::Mat pattern = createPatternTestImage();
    cv::Mat search = cv::Mat(400, 400, CV_8UC1, cv::Scalar(40));
    pattern.copyTo(search(cv::Rect(150, 100, pattern.cols, pattern.rows)));

    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::TemplateMatch);
    if (!tool) {
        r.passed = false;
        r.message = "工厂创建失败";
        return r;
    }

    QElapsedTimer timer;
    timer.start();

    ToolResult result;
    bool ok = tool->execute(search, result);
    r.elapsedMs = timer.elapsed();

    r.passed = ok && result.score >= 0.0;
    r.message = r.passed ? "匹配检测通过" : "匹配失败";
    delete tool;
    return r;
}

ToolVerifyResult ToolChainVerifier::verifyEdgeDetect() {
    ToolVerifyResult r;
    r.toolName = "边缘检测";

    cv::Mat input = createTestImage(320, 240);
    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::EdgeDetect);
    if (!tool) {
        r.passed = false;
        r.message = "工厂创建失败";
        return r;
    }

    QJsonObject params;
    params["lowThreshold"] = 50;
    params["highThreshold"] = 150;
    params["apertureSize"] = 3;
    tool->configure(params);

    QElapsedTimer timer;
    timer.start();

    ToolResult result;
    bool ok = tool->execute(input, result);
    r.elapsedMs = timer.elapsed();

    int edgeCount = result.data.value("edgeCount").toInt(0);
    r.passed = ok && edgeCount > 0;
    r.message = r.passed ? QString("检测到 %1 个边缘像素").arg(edgeCount)
                         : "未检测到边缘";
    delete tool;
    return r;
}

ToolVerifyResult ToolChainVerifier::verifyBlobDetect() {
    ToolVerifyResult r;
    r.toolName = "斑块检测";

    cv::Mat input = createTestImage(320, 240);
    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::BlobDetect);
    if (!tool) {
        r.passed = false;
        r.message = "工厂创建失败";
        return r;
    }

    QElapsedTimer timer;
    timer.start();

    ToolResult result;
    bool ok = tool->execute(input, result);
    r.elapsedMs = timer.elapsed();

    r.passed = ok;
    r.message = r.passed ? "斑块检测执行成功" : "执行失败";
    delete tool;
    return r;
}

ToolVerifyResult ToolChainVerifier::verifyColorDetect() {
    ToolVerifyResult r;
    r.toolName = "颜色识别";

    cv::Mat input = createColorTestImage();
    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::ColorDetect);
    if (!tool) {
        r.passed = false;
        r.message = "工厂创建失败";
        return r;
    }

    QJsonObject params;
    QJsonArray lower, upper;
    lower.append(0); lower.append(100); lower.append(100);
    upper.append(10); upper.append(255); upper.append(255);
    params["lowerHSV"] = lower;
    params["upperHSV"] = upper;
    tool->configure(params);

    QElapsedTimer timer;
    timer.start();

    ToolResult result;
    bool ok = tool->execute(input, result);
    r.elapsedMs = timer.elapsed();

    r.passed = ok;
    r.message = r.passed ? "颜色检测执行成功" : "执行失败";
    delete tool;
    return r;
}

ToolVerifyResult ToolChainVerifier::verifyThreshold() {
    ToolVerifyResult r;
    r.toolName = "阈值分割";

    cv::Mat gray = createPatternTestImage();
    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::Threshold);
    if (!tool) {
        r.passed = false;
        r.message = "工厂创建失败";
        return r;
    }

    QJsonObject params;
    params["thresholdValue"] = 128;
    params["maxValue"] = 255;
    params["method"] = "binary";
    tool->configure(params);

    QElapsedTimer timer;
    timer.start();

    ToolResult result;
    bool ok = tool->execute(gray, result);
    r.elapsedMs = timer.elapsed();

    r.passed = ok;
    r.message = r.passed ? "阈值分割执行成功" : "执行失败";
    delete tool;
    return r;
}

ToolVerifyResult ToolChainVerifier::verifyImagePreprocess() {
    ToolVerifyResult r;
    r.toolName = "图像预处理";

    cv::Mat input = createTestImage(320, 240);
    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::ImagePreprocess);
    if (!tool) {
        r.passed = false;
        r.message = "工厂创建失败";
        return r;
    }

    QJsonObject params;
    params["filterType"] = "gaussian";
    params["kernelSize"] = 5;
    tool->configure(params);

    QElapsedTimer timer;
    timer.start();

    ToolResult result;
    bool ok = tool->execute(input, result);
    r.elapsedMs = timer.elapsed();

    r.passed = ok;
    r.message = r.passed ? "预处理执行成功" : "执行失败";
    delete tool;
    return r;
}

ToolVerifyResult ToolChainVerifier::verifyContourAnalyze() {
    ToolVerifyResult r;
    r.toolName = "轮廓分析";

    cv::Mat input = createTestImage(320, 240);
    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::ContourAnalyze);
    if (!tool) {
        r.passed = false;
        r.message = "工厂创建失败";
        return r;
    }

    QElapsedTimer timer;
    timer.start();

    ToolResult result;
    bool ok = tool->execute(input, result);
    r.elapsedMs = timer.elapsed();

    r.passed = ok;
    r.message = r.passed ? "轮廓分析执行成功" : "执行失败";
    delete tool;
    return r;
}

ToolVerifyResult ToolChainVerifier::verifyGeometryMeasure() {
    ToolVerifyResult r;
    r.toolName = "几何测量";

    cv::Mat input = createGeometricTestImage();
    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::GeometryMeasure);
    if (!tool) {
        r.passed = false;
        r.message = "工厂创建失败";
        return r;
    }

    QElapsedTimer timer;
    timer.start();

    ToolResult result;
    bool ok = tool->execute(input, result);
    r.elapsedMs = timer.elapsed();

    r.passed = ok;
    r.message = r.passed ? "几何测量执行成功" : "执行失败";
    delete tool;
    return r;
}

ToolVerifyResult ToolChainVerifier::verifyLineCircleDetect() {
    ToolVerifyResult r;
    r.toolName = "直线/圆检测";

    cv::Mat input = createLineCircleTestImage();
    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::LineCircleDetect);
    if (!tool) {
        r.passed = false;
        r.message = "工厂创建失败";
        return r;
    }

    QJsonObject params;
    params["detectLines"] = true;
    params["detectCircles"] = true;
    tool->configure(params);

    QElapsedTimer timer;
    timer.start();

    ToolResult result;
    bool ok = tool->execute(input, result);
    r.elapsedMs = timer.elapsed();

    r.passed = ok;
    r.message = r.passed ? "直线/圆检测执行成功" : "执行失败";
    delete tool;
    return r;
}

ToolVerifyResult ToolChainVerifier::verifyImageArithmetic() {
    ToolVerifyResult r;
    r.toolName = "图像运算";

    cv::Mat img1 = createTestImage(200, 150);
    cv::Mat img2 = createTestImage(200, 150);
    cv::flip(img2, img2, 1);

    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::ImageArithmetic);
    if (!tool) {
        r.passed = false;
        r.message = "工厂创建失败";
        return r;
    }

    QJsonObject params;
    params["operation"] = "add";
    params["operandImage"] = QString();  // tool handles this internally
    tool->configure(params);

    QElapsedTimer timer;
    timer.start();

    ToolResult result;
    bool ok = tool->execute(img1, result);
    r.elapsedMs = timer.elapsed();

    r.passed = ok;
    r.message = r.passed ? "图像运算执行成功" : "执行失败";
    delete tool;
    return r;
}

ToolVerifyResult ToolChainVerifier::verifyImageTransform() {
    ToolVerifyResult r;
    r.toolName = "图像变换";

    cv::Mat input = createTestImage(320, 240);
    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::ImageTransform);
    if (!tool) {
        r.passed = false;
        r.message = "工厂创建失败";
        return r;
    }

    QJsonObject params;
    params["transformType"] = "rotate";
    params["angle"] = 45.0;
    tool->configure(params);

    QElapsedTimer timer;
    timer.start();

    ToolResult result;
    bool ok = tool->execute(input, result);
    r.elapsedMs = timer.elapsed();

    r.passed = ok;
    r.message = r.passed ? "图像变换执行成功" : "执行失败";
    delete tool;
    return r;
}

ToolVerifyResult ToolChainVerifier::verifyImageMerge() {
    ToolVerifyResult r;
    r.toolName = "图像合并";

    cv::Mat img1 = createTestImage(200, 150);
    cv::Mat img2 = createTestImage(200, 150);
    cv::flip(img2, img2, 0);

    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::ImageMerge);
    if (!tool) {
        r.passed = false;
        r.message = "工厂创建失败";
        return r;
    }

    QJsonObject params;
    params["mergeMode"] = "horizontal";
    tool->configure(params);

    QElapsedTimer timer;
    timer.start();

    ToolResult result;
    bool ok = tool->execute(img1, result);
    r.elapsedMs = timer.elapsed();

    r.passed = ok;
    r.message = r.passed ? "图像合并执行成功" : "执行失败";
    delete tool;
    return r;
}

ToolVerifyResult ToolChainVerifier::verifyBranchControl() {
    ToolVerifyResult r;
    r.toolName = "分支控制";

    cv::Mat input = createTestImage(200, 150);
    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::BranchControl);
    if (!tool) {
        r.passed = false;
        r.message = "工厂创建失败";
        return r;
    }

    QJsonObject params;
    params["condition"] = "score_above";
    params["threshold"] = 0.5;
    params["trueBranch"] = "ok_path";
    params["falseBranch"] = "ng_path";
    tool->configure(params);

    QElapsedTimer timer;
    timer.start();

    ToolResult result;
    result.score = 0.85;
    bool ok = tool->execute(input, result);
    r.elapsedMs = timer.elapsed();

    r.passed = ok;
    r.message = r.passed ? "分支控制执行成功" : "执行失败";
    delete tool;
    return r;
}

int ToolChainVerifier::passedCount() const {
    int count = 0;
    for (const auto& r : m_results) {
        if (r.passed) count++;
    }
    return count;
}

int ToolChainVerifier::totalCount() const {
    return m_results.size();
}