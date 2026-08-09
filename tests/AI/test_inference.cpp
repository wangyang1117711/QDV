#include "../catch2/catch2_minimal.hpp"
#include "AI/InferenceEngine.h"
#include "AI/ModelManager.h"
#include <opencv2/opencv.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace {
// 测试辅助：定位轻量测试模型。
// 测试可执行文件通常位于 build/bin，沿目录向上回溯即可到达项目根目录，
// 从而找到源码树中的 tests/fixtures/dummy_classifier.onnx。
QString findTestModelPath() {
    QString envPath = qEnvironmentVariable("QDV_TEST_MODEL");
    if (!envPath.isEmpty() && QFileInfo::exists(envPath)) {
        return envPath;
    }

    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        QString candidate = dir.absoluteFilePath("tests/fixtures/dummy_classifier.onnx");
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QString();
}
} // namespace

TEST_CASE("InferenceEngine构造", "[inference]") {
    InferenceEngine engine;
    REQUIRE_FALSE(engine.isModelLoaded());
    REQUIRE_EQUAL(engine.backend(), InferenceEngine::BackendOpenCVDNN);
}

TEST_CASE("InferenceEngine加载不存在的模型", "[inference]") {
    InferenceEngine engine;
    bool ok = engine.loadModel("nonexistent_model.onnx");
    REQUIRE_FALSE(ok);
}

TEST_CASE("InferenceEngine无模型时推理安全", "[inference]") {
    InferenceEngine engine;
    cv::Mat input(224, 224, CV_8UC3, cv::Scalar(128, 128, 128));
    QJsonObject result;
    bool ok = engine.infer(input, result);
    REQUIRE_FALSE(ok);
}

TEST_CASE("InferenceEngine卸载安全", "[inference]") {
    InferenceEngine engine;
    bool ok = engine.unloadModel();
    REQUIRE(ok);
}

TEST_CASE("InferenceEngine预热无模型安全", "[inference]") {
    InferenceEngine engine;
    bool ok = engine.warmUp(3);
    REQUIRE_FALSE(ok);
}

TEST_CASE("ModelManager单例", "[inference]") {
    ModelManager* mgr = ModelManager::instance();
    REQUIRE(mgr != nullptr);
}

TEST_CASE("ModelManager默认缓存大小", "[inference]") {
    ModelManager* mgr = ModelManager::instance();
    REQUIRE_EQUAL(mgr->cacheSize(), ModelManager::DEFAULT_CACHE_SIZE);
}

TEST_CASE("ModelManager加载不存在的模型", "[inference]") {
    ModelManager* mgr = ModelManager::instance();
    bool ok = mgr->loadModel("nonexistent.onnx", "test_model");
    REQUIRE_FALSE(ok);
}

TEST_CASE("ModelManager空ID获取Engine安全", "[inference]") {
    ModelManager* mgr = ModelManager::instance();
    InferenceEngine* engine = mgr->getEngine("");
    REQUIRE(engine == nullptr);
}

TEST_CASE("InferenceEngine后端切换", "[inference]") {
    InferenceEngine engine;
    engine.setBackend(InferenceEngine::BackendOpenCVDNN);
    REQUIRE_EQUAL(engine.backend(), InferenceEngine::BackendOpenCVDNN);

    engine.setBackend(InferenceEngine::BackendONNXRuntime);
    REQUIRE_EQUAL(engine.backend(), InferenceEngine::BackendONNXRuntime);
}

// 修复验证：preprocess 必须将单通道/4 通道输入统一转换为 3 通道 NCHW blob，
// 否则下游分类/检测模型会因"Number of input channels should be multiple of 3 but got 1"失败。
TEST_CASE("InferenceEngine preprocess 单通道输入自动转 3 通道", "[inference]") {
    InferenceEngine engine;
    QString modelPath = findTestModelPath();
    REQUIRE_FALSE(modelPath.isEmpty());
    bool loaded = engine.loadModel(modelPath, QSize(224, 224),
                                   cv::Scalar(0.485, 0.456, 0.406), 1.0 / 255.0, true,
                                   cv::Scalar(0.229, 0.224, 0.225));
    REQUIRE(loaded);

    cv::Mat gray(800, 800, CV_8UC1, cv::Scalar(128));
    cv::Mat blob = engine.preprocess(gray);

    REQUIRE(blob.dims == 4);
    REQUIRE(blob.size[0] == 1);
    REQUIRE(blob.size[1] == 3);   // 必须转为 3 通道
    REQUIRE(blob.size[2] == 224); // resize 到模型输入尺寸
    REQUIRE(blob.size[3] == 224);
}

TEST_CASE("InferenceEngine preprocess 4 通道输入自动转 3 通道", "[inference]") {
    InferenceEngine engine;
    QString modelPath = findTestModelPath();
    REQUIRE_FALSE(modelPath.isEmpty());
    bool loaded = engine.loadModel(modelPath, QSize(224, 224),
                                   cv::Scalar(0.485, 0.456, 0.406), 1.0 / 255.0, true,
                                   cv::Scalar(0.229, 0.224, 0.225));
    REQUIRE(loaded);

    cv::Mat bgra(800, 800, CV_8UC4, cv::Scalar(128, 128, 128, 255));
    cv::Mat blob = engine.preprocess(bgra);

    REQUIRE(blob.dims == 4);
    REQUIRE(blob.size[0] == 1);
    REQUIRE(blob.size[1] == 3);
    REQUIRE(blob.size[2] == 224);
    REQUIRE(blob.size[3] == 224);
}

TEST_CASE("InferenceEngine preprocess 3 通道输入保持 3 通道", "[inference]") {
    InferenceEngine engine;
    QString modelPath = findTestModelPath();
    REQUIRE_FALSE(modelPath.isEmpty());
    bool loaded = engine.loadModel(modelPath, QSize(224, 224),
                                   cv::Scalar(0.485, 0.456, 0.406), 1.0 / 255.0, true,
                                   cv::Scalar(0.229, 0.224, 0.225));
    REQUIRE(loaded);

    cv::Mat bgr(800, 800, CV_8UC3, cv::Scalar(128, 128, 128));
    cv::Mat blob = engine.preprocess(bgr);

    REQUIRE(blob.dims == 4);
    REQUIRE(blob.size[0] == 1);
    REQUIRE(blob.size[1] == 3);
    REQUIRE(blob.size[2] == 224);
    REQUIRE(blob.size[3] == 224);
}
