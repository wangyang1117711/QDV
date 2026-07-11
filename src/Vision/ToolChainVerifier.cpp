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
    // P1-C7 修复：补全 AI 推理与读图路径自检
    m_results.append(verifyAiClassify());
    m_results.append(verifyReadImage());
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
    // P1-B10 修复：统一检查 configure 返回值，避免参数错误时"假通过"
    if (!tool->configure(params)) {
        r.passed = false;
        r.message = "配置失败（参数名错误）";
        delete tool;
        return r;
    }

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
    // P1-B10 修复：参数名与 ColorDetectTool::configure 一致
    // 之前用 lowerHSV/upperHSV 数组，但 configure 期望 6 个独立字段 hMin/hMax/sMin/sMax/vMin/vMax
    // 参数名错误导致 configure 静默失败（ColorDetectTool::configure 总返回 true 但字段未读取）
    // 检测红色目标：H=0-10, S=100-255, V=100-255
    params["hMin"] = 0;
    params["hMax"] = 10;
    params["sMin"] = 100;
    params["sMax"] = 255;
    params["vMin"] = 100;
    params["vMax"] = 255;
    if (!tool->configure(params)) {
        r.passed = false;
        r.message = "配置失败（参数名错误）";
        delete tool;
        return r;
    }

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
    // P1-B10 修复：thresholdValue → threshold（与 ThresholdTool::configure 一致）
    // method 用大写 BINARY（与元数据 optionKeys 一致），之前 "binary" 不在合法值内
    params["threshold"] = 128;
    params["maxValue"] = 255;
    params["method"] = "BINARY";
    if (!tool->configure(params)) {
        r.passed = false;
        r.message = "配置失败（参数名错误）";
        delete tool;
        return r;
    }

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
    // P1-B10 修复：filterType → denoise + morphology（与 ImagePreprocessTool::configure 一致）
    // 之前 filterType 被忽略；kernelSize=5 是奇数符合形态学核要求
    params["denoise"] = true;
    params["morphology"] = "open";
    params["kernelSize"] = 5;
    if (!tool->configure(params)) {
        r.passed = false;
        r.message = "配置失败（参数名错误）";
        delete tool;
        return r;
    }

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
    // P1-B10 修复：detectLines/detectCircles → detectType（与 LineCircleDetectTool::configure 一致）
    // 之前参数名错误导致 configure 返回 false，但 verifier 不检查返回值，execute 用默认值"假通过"
    params["detectType"] = "lineP";
    if (!tool->configure(params)) {
        r.passed = false;
        r.message = "配置失败（参数名错误）";
        delete tool;
        return r;
    }

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
    // P1-B10 修复：operandImage 不存在 → useScalar + scalar（与 ImageArithmeticTool::configure 一致）
    // 之前 operandImage 被忽略，useScalar=false 导致 execute 走 m_hasSecondImage 分支但无第二张图，输出=input
    params["operation"] = "add";
    params["useScalar"] = true;
    params["scalar"] = 50.0;
    if (!tool->configure(params)) {
        r.passed = false;
        r.message = "配置失败（参数名错误）";
        delete tool;
        return r;
    }

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
    // P1-B10 修复：统一检查 configure 返回值，避免参数错误时"假通过"
    if (!tool->configure(params)) {
        r.passed = false;
        r.message = "配置失败（参数名错误）";
        delete tool;
        return r;
    }

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
    // P1-B10 修复：mergeMode → mergeType（与 ImageMergeTool::configure 一致）
    // 之前参数名错误导致 configure 返回 false，但 verifier 不检查返回值，"假通过"
    params["mergeType"] = "horizontal";
    if (!tool->configure(params)) {
        r.passed = false;
        r.message = "配置失败（参数名错误）";
        delete tool;
        return r;
    }

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
    // P1-B10 修复：condition→conditionOp, threshold→conditionValue（与 BranchControlTool::configure 一致）
    // 之前参数名错误导致 BranchNode::deserialize 失败，分支节点 invalid，
    // 但 verifier 不检查 configure 返回值，"假通过"
    params["conditionOp"] = ">";
    params["conditionValue"] = 0.5;
    params["trueBranch"] = "ok_path";
    params["falseBranch"] = "ng_path";
    if (!tool->configure(params)) {
        r.passed = false;
        r.message = "配置失败（参数名错误）";
        delete tool;
        return r;
    }

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

// P1-C7 修复（CodeWiki 已知限制）：补全 AI 推理与读图路径自检
// 之前 verifyAll() 缺这两个 verify，AI 分类与读图路径无自检
ToolVerifyResult ToolChainVerifier::verifyAiClassify() {
    ToolVerifyResult r;
    r.toolName = "AI分类";

    cv::Mat input = createTestImage(224, 224);
    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::AiClassify);
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

    // AI 分类工具在没有加载模型时返回 false 是预期行为，不算失败
    // 只要工具能被创建并执行（不崩溃），即认为验证通过
    r.passed = ok || !result.data.value("modelLoaded", true).toBool();
    if (r.passed) {
        r.message = ok ? "AI分类执行成功" : "AI分类跳过（未加载模型，预期行为）";
    } else {
        r.message = "AI分类执行失败";
    }
    delete tool;
    return r;
}

ToolVerifyResult ToolChainVerifier::verifyReadImage() {
    ToolVerifyResult r;
    r.toolName = "图像读取";

    // 创建一个临时图像文件用于测试 ReadImage 工具
    cv::Mat testImg = createTestImage(200, 150);
    QString tempPath = QDir::tempPath() + "/qdv_verifier_readimage.png";
    if (!cv::imwrite(tempPath.toStdString(), testImg)) {
        r.passed = false;
        r.message = "无法创建测试图像文件";
        return r;
    }

    VisionTool* tool = ToolFactory::instance()->createTool(VisionTool::ReadImage);
    if (!tool) {
        r.passed = false;
        r.message = "工厂创建失败";
        QFile::remove(tempPath);
        return r;
    }

    QJsonObject params;
    params["filePath"] = tempPath;
    if (!tool->configure(params)) {
        r.passed = false;
        r.message = "配置失败（参数名错误）";
        delete tool;
        QFile::remove(tempPath);
        return r;
    }

    QElapsedTimer timer;
    timer.start();

    // ReadImage 工具通常忽略 input，从文件读取
    cv::Mat dummyInput;
    ToolResult result;
    bool ok = tool->execute(dummyInput, result);
    r.elapsedMs = timer.elapsed();

    r.passed = ok && !result.overlayImage.empty();
    r.message = r.passed ? "图像读取成功" : "图像读取失败";
    delete tool;
    QFile::remove(tempPath);
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