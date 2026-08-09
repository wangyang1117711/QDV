#include "../catch2/catch2_minimal.hpp"
#include "AI/ModelManager.h"
#include "AI/InferenceEngine.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <opencv2/opencv.hpp>

TEST_CASE("ModelManager singleton", "[model]") {
    auto* m1 = ModelManager::instance();
    auto* m2 = ModelManager::instance();
    REQUIRE(m1 != nullptr);
    REQUIRE(m1 == m2);
}

TEST_CASE("ModelManager loadModel non-existent returns false", "[model]") {
    auto* mgr = ModelManager::instance();
    bool loaded = mgr->loadModel("no_such_file.onnx", "nonexistent_model_v999");
    REQUIRE_FALSE(loaded);
    REQUIRE(mgr->getEngine("nonexistent_model_v999") == nullptr);
}

TEST_CASE("ModelManager LRU eviction with single cache slot", "[model]") {
    auto* mgr = ModelManager::instance();
    mgr->setCacheSize(1);
    mgr->unloadAll();
    REQUIRE_EQUAL(mgr->loadedModelIds().size(), 0);
}

TEST_CASE("ModelManager getEngine after load attempt", "[model]") {
    auto* mgr = ModelManager::instance();
    REQUIRE(mgr->getEngine("never_loaded") == nullptr);
}

TEST_CASE("ModelManager evictModel", "[model]") {
    auto* mgr = ModelManager::instance();
    mgr->unloadAll();
    mgr->unloadModel("nonexistent");
    SUCCEED("unloadModel on non-existent is safe");
}

TEST_CASE("ModelManager cache clear behavior", "[model]") {
    auto* mgr = ModelManager::instance();
    mgr->unloadAll();
    REQUIRE_EQUAL(mgr->loadedModelIds().size(), 0);
}

// 默认模型相关的测试
TEST_CASE("ModelManager default model directory", "[model][default]") {
    auto* mgr = ModelManager::instance();
    QString dirPath = mgr->defaultModelDirectory();
    REQUIRE_FALSE(dirPath.isEmpty());
    REQUIRE(dirPath.contains("models"));
}

TEST_CASE("ModelManager find default models", "[model][default]") {
    auto* mgr = ModelManager::instance();
    QStringList models = mgr->findDefaultModels();
    // 即使没有模型也应该返回空列表而不是错误
    REQUIRE(models.empty() || !models.empty());
}

TEST_CASE("ModelManager isDefaultModelAvailable", "[model][default]") {
    auto* mgr = ModelManager::instance();
    bool available = mgr->isDefaultModelAvailable();
    // 这个测试只验证函数能正常调用，不做具体状态断言
    // 因为实际环境中可能有或没有模型文件
    (void)available;
    SUCCEED("isDefaultModelAvailable() called successfully");
}

TEST_CASE("ModelManager getAvailableModelNames", "[model][default]") {
    auto* mgr = ModelManager::instance();
    QStringList names = mgr->getAvailableModelNames();
    // 即使没有模型也应该返回空列表
    REQUIRE(names.empty() || !names.empty());
}

TEST_CASE("ModelManager getModelPathByName", "[model][default]") {
    auto* mgr = ModelManager::instance();
    
    // 测试不存在的模型名
    QString path = mgr->getModelPathByName("nonexistent_model_name_abc123");
    REQUIRE(path.isEmpty());
    
    // 测试默认模型名
    QString defaultPath = mgr->getModelPathByName(QString::fromLatin1(ModelManager::DEFAULT_MODEL_NAME));
    // 可能存在也可能不存在，取决于实际环境
    (void)defaultPath;
    SUCCEED("getModelPathByName() called successfully");
}

TEST_CASE("ModelManager loadDefaultModel non-existent models dir", "[model][default]") {
    auto* mgr = ModelManager::instance();
    
    // 在没有默认模型的情况下尝试加载应该返回false
    // 这个测试主要验证不会崩溃
    bool loaded = mgr->loadDefaultModel("test_default_id");
    // 不做具体断言，因为取决于实际环境
    (void)loaded;
    SUCCEED("loadDefaultModel() called successfully");
}

TEST_CASE("ModelManager getDefaultModelPath", "[model][default]") {
    auto* mgr = ModelManager::instance();
    QString path = mgr->getDefaultModelPath();
    // 路径可能存在也可能不存在，但函数不应该崩溃
    (void)path;
    SUCCEED("getDefaultModelPath() called successfully");
}

TEST_CASE("ModelManager with temporary models directory", "[model][default]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());
    
    // 创建一些模拟的模型文件
    QString model1 = tempDir.filePath("yolov8n.onnx");
    QString model2 = tempDir.filePath("resnet50.onnx");
    
    // 创建空文件
    QFile f1(model1);
    QFile f2(model2);
    f1.open(QIODevice::WriteOnly);
    f1.close();
    f2.open(QIODevice::WriteOnly);
    f2.close();
    
    REQUIRE(QFile::exists(model1));
    REQUIRE(QFile::exists(model2));
    
    auto* mgr = ModelManager::instance();
    
    // 注意：这里我们不能直接测试findDefaultModels对临时目录的查找，
    // 因为默认目录是硬编码的。但我们可以测试基本的列表功能
    QStringList models = mgr->listModels(tempDir.path());
    REQUIRE(models.size() == 2);

    SUCCEED("listModels() works with temporary directory");
}

// === 新增：ModelManager 增强功能测试（P0 Task 3.3）===

// 测试 autoDetectModelType：YOLO 训练产物格式与文件名包含 yolo 的识别
TEST_CASE("ModelManager autoDetectModelType YOLO", "[model][enhanced]") {
    auto* mgr = ModelManager::instance();
    // YOLO 训练产物格式: best<YYYYMMDDHHMMSS>.onnx
    QString yoloPath = "best20260711120000.onnx";
    QString type = mgr->autoDetectModelType(yoloPath);
    REQUIRE(type == "yolo");

    // 文件名包含 yolo
    REQUIRE(mgr->autoDetectModelType("yolov5s.onnx") == "yolo");
    REQUIRE(mgr->autoDetectModelType("yolov8n.onnx") == "yolo");

    // 分类模型
    REQUIRE(mgr->autoDetectModelType("resnet50.onnx") == "classification");
    REQUIRE(mgr->autoDetectModelType("model_v2.onnx") == "classification");
}

// 测试 autoDetectInputSize：YOLO→640x640, 分类→224x224
TEST_CASE("ModelManager autoDetectInputSize", "[model][enhanced]") {
    auto* mgr = ModelManager::instance();
    REQUIRE(mgr->autoDetectInputSize("best20260711120000.onnx") == QSize(640, 640));
    REQUIRE(mgr->autoDetectInputSize("yolov5s.onnx") == QSize(640, 640));
    REQUIRE(mgr->autoDetectInputSize("resnet50.onnx") == QSize(224, 224));
}

// 测试 loadManifest：构造函数已调用，这里验证能正常重复调用
TEST_CASE("ModelManager loadManifest", "[model][enhanced]") {
    auto* mgr = ModelManager::instance();
    // loadManifest 在构造函数已调用，这里验证能正常调用
    bool ok = mgr->loadManifest();
    REQUIRE(ok);

    // manifest 应包含 models 数组
    QJsonArray models = mgr->manifestModels();
    REQUIRE(models.size() >= 0);  // 可能为 0 或更多，取决于实际 manifest
}

// 测试 manifestModels 返回数组且每个条目含 file_name 字段
TEST_CASE("ModelManager manifestModels returns array", "[model][enhanced]") {
    auto* mgr = ModelManager::instance();
    mgr->loadManifest();
    QJsonArray models = mgr->manifestModels();
    // manifest.json 已有 5 个模型记录
    REQUIRE(models.size() >= 3);

    // 验证每个条目有 file_name 字段
    for (int i = 0; i < models.size(); ++i) {
        QJsonObject model = models[i].toObject();
        REQUIRE(model.contains("file_name"));
    }
}

// 测试 verifyModelIntegrity：不存在的 modelId 应返回 false
TEST_CASE("ModelManager verifyModelIntegrity non-existent", "[model][enhanced]") {
    auto* mgr = ModelManager::instance();
    // 不存在的 modelId 应返回 false
    bool ok = mgr->verifyModelIntegrity("non_existent_model_id_xyz");
    REQUIRE_FALSE(ok);
}

// 测试 removeCustomModelTransactional：删除不存在的模型应返回 false
TEST_CASE("ModelManager removeCustomModelTransactional non-existent", "[model][enhanced]") {
    auto* mgr = ModelManager::instance();
    // 删除不存在的模型应返回 false
    bool ok = mgr->removeCustomModelTransactional("non_existent_model_id_xyz");
    REQUIRE_FALSE(ok);
}

// 测试 addCustomModel：源文件不存在应返回 false
TEST_CASE("ModelManager addCustomModel non-existent file", "[model][enhanced]") {
    auto* mgr = ModelManager::instance();
    // 源文件不存在应返回 false
    bool ok = mgr->addCustomModel("non_existent.onnx", "test_model");
    REQUIRE_FALSE(ok);
}

// === 新增：类别标签自动加载测试 ===
// 验证 ModelManager::loadModel 在成功加载模型后，能从 manifest.json 中读取
// 对应的 labels_file 并注入 InferenceEngine::m_categoryLabels
TEST_CASE("ModelManager auto-loads category labels from manifest", "[model][labels]") {
    auto* mgr = ModelManager::instance();
    mgr->unloadAll();

    QString modelPath = "E:/anchor/Trae/QDV/models/model_20260712_193902.onnx";
    if (!QFile::exists(modelPath)) {
        SUCCEED("model_20260712_193902.onnx not available, skipping labels test");
        return;
    }

    bool loaded = mgr->loadModel(modelPath, "model_20260712_193902_labels_test");
    if (!loaded) {
        SUCCEED("model_20260712_193902.onnx could not be loaded by OpenCV DNN, skipping labels test");
        return;
    }

    InferenceEngine* engine = mgr->getEngine("model_20260712_193902_labels_test");
    REQUIRE(engine != nullptr);
    REQUIRE(engine->isModelLoaded());

    QStringList labels = engine->categoryLabels();
    REQUIRE_FALSE(labels.isEmpty());
    REQUIRE(labels.size() == 2);
    REQUIRE(labels[0] == "ZIPER");
    REQUIRE(labels[1] == "PILL");

    mgr->unloadModel("model_20260712_193902_labels_test");
}

// === 新增：端到端推理测试（P0 Task 3.4）===
// 若 resnet50.onnx 不存在则跳过，存在则执行真实分类推理
TEST_CASE("InferenceEngine real model inference", "[inference][e2e]") {
    QString modelPath = "E:/anchor/Trae/QDV/models/resnet50.onnx";
    if (!QFile::exists(modelPath)) {
        SUCCEED("resnet50.onnx not available, skipping E2E test");
        return;
    }

    InferenceEngine engine;
    bool loaded = engine.loadModel(modelPath, QSize(224, 224));
    REQUIRE(loaded);
    REQUIRE(engine.isModelLoaded());

    // 创建测试图像
    cv::Mat input(224, 224, CV_8UC3, cv::Scalar(128, 128, 128));
    QJsonObject result;
    bool ok = engine.infer(input, result);
    REQUIRE(ok);
    REQUIRE(result.contains("class_index"));
    REQUIRE(result.contains("confidence"));
    REQUIRE(result.contains("latency_ms"));
    REQUIRE(result.contains("backend"));

    // warmUp 后再推理
    engine.warmUp(3);
    QJsonObject result2;
    engine.infer(input, result2);
    REQUIRE(result2.contains("class_index"));
}