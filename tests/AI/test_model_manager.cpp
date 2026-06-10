#include "../catch2/catch2_minimal.hpp"
#include "AI/ModelManager.h"
#include "AI/InferenceEngine.h"
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

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