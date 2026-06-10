#include "../catch2/catch2_minimal.hpp"
#include "AI/InferenceEngine.h"
#include "AI/ModelManager.h"
#include <opencv2/opencv.hpp>

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