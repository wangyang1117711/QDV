#include "../catch2/catch2_minimal.hpp"
#include "Vision/AiClassifyTool.h"
#include "OperatorSDK/IInferenceEngine.h"
#include <opencv2/core/mat.hpp>
#include <QJsonObject>
#include <thread>
#include <vector>
#include <atomic>

using namespace QDV;

// 测试用桩引擎：可控制 loadModel/infer 返回值，记录调用次数
namespace {
class StubEngine : public IInferenceEngine {
public:
    bool loadModelSuccess = true;
    bool inferSuccess = true;
    std::atomic<int> inferCallCount{0};
    std::atomic<int> loadModelCallCount{0};
    QString backendName = "OpenCV_DNN";

    bool loadModel(const QString&, const QSize&, const cv::Scalar&, double, bool) override {
        loadModelCallCount++;
        return loadModelSuccess;
    }
    bool warmUp(int = 3) override { return true; }
    bool infer(const cv::Mat&, QJsonObject& result) override {
        inferCallCount++;
        if (inferSuccess) {
            result["confidence"] = 0.9;
            result["class_index"] = 0;
            result["category"] = "test";
        }
        return inferSuccess;
    }
    InferenceMetricsLite lastMetrics() const override {
        InferenceMetricsLite m;
        m.totalMs = 10;
        m.backend = backendName;
        return m;
    }
    QString backend() const override { return backendName; }
};
} // namespace

// RT-006：引擎未注入 → execute 返回 false，不崩溃
TEST_CASE("AiClassifyTool DI: 引擎未注入返回 false (RT-006)", "[Vision][RT-006]") {
    AiClassifyTool tool;
    // 不调用 setInferenceEngine，m_engine 为 nullptr
    cv::Mat input(10, 10, CV_8UC3, cv::Scalar(1, 2, 3));
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.data["error"].toString() == "No inference engine injected");
}

// RT-007：引擎加载失败 → execute 返回 false，不崩溃
TEST_CASE("AiClassifyTool DI: 引擎加载失败返回 false (RT-007)", "[Vision][RT-007]") {
    AiClassifyTool tool;
    StubEngine* engine = new StubEngine();
    engine->loadModelSuccess = false;  // 模拟 loadModel 失败
    tool.setInferenceEngine(engine);
    tool.setModelPath("/fake/model.onnx");

    cv::Mat input(10, 10, CV_8UC3, cv::Scalar(1, 2, 3));
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.data.contains("error"));
    delete engine;
}

// RT-008：多次注入 → 使用最后一次注入的引擎
TEST_CASE("AiClassifyTool DI: 多次注入使用最后引擎 (RT-008)", "[Vision][RT-008]") {
    AiClassifyTool tool;
    StubEngine* engineA = new StubEngine();
    engineA->backendName = "OpenCV_DNN";
    StubEngine* engineB = new StubEngine();
    engineB->backendName = "ONNXRuntime";

    tool.setInferenceEngine(engineA);
    REQUIRE(tool.inferenceEngine() == engineA);

    tool.setInferenceEngine(engineB);  // 覆盖
    REQUIRE(tool.inferenceEngine() == engineB);

    // 验证使用的是 engineB
    tool.setModelPath("/fake/model.onnx");
    cv::Mat input(10, 10, CV_8UC3, cv::Scalar(1, 2, 3));
    ToolResult result;
    tool.execute(input, result);
    REQUIRE(engineB->inferCallCount > 0);
    REQUIRE(engineA->inferCallCount == 0);  // engineA 从未被调用

    delete engineA;
    delete engineB;
}

// RT-009：引擎后端切换 → 两个引擎都能正常推理
TEST_CASE("AiClassifyTool DI: 后端切换 (RT-009)", "[Vision][RT-009]") {
    AiClassifyTool tool;
    StubEngine* cvEngine = new StubEngine();
    cvEngine->backendName = "OpenCV_DNN";
    StubEngine* onnxEngine = new StubEngine();
    onnxEngine->backendName = "ONNXRuntime";

    // 注入 OpenCV_DNN 引擎推理
    tool.setInferenceEngine(cvEngine);
    tool.setModelPath("/fake/model.onnx");
    cv::Mat input(10, 10, CV_8UC3, cv::Scalar(1, 2, 3));
    ToolResult result;
    bool ok1 = tool.execute(input, result);
    REQUIRE(ok1);
    REQUIRE(cvEngine->backend() == "OpenCV_DNN");

    // 切换到 ONNXRuntime 引擎（需重置 warmedUp 状态——通过新实例）
    AiClassifyTool tool2;
    tool2.setInferenceEngine(onnxEngine);
    tool2.setModelPath("/fake/model2.onnx");
    ToolResult result2;
    bool ok2 = tool2.execute(input, result2);
    REQUIRE(ok2);
    REQUIRE(onnxEngine->backend() == "ONNXRuntime");

    delete cvEngine;
    delete onnxEngine;
}

// RT-010：并发推理 → 线程安全，无竞争
TEST_CASE("AiClassifyEngineAdapter DI: 并发推理线程安全 (RT-010)", "[Vision][RT-010]") {
    StubEngine engine;
    engine.inferSuccess = true;

    AiClassifyTool tool;
    tool.setInferenceEngine(&engine);
    tool.setModelPath("/fake/model.onnx");

    cv::Mat input(10, 10, CV_8UC3, cv::Scalar(1, 2, 3));

    const int THREAD_COUNT = 8;
    const int ITERS_PER_THREAD = 10;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    for (int t = 0; t < THREAD_COUNT; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < ITERS_PER_THREAD; ++i) {
                ToolResult result;
                if (tool.execute(input, result)) {
                    successCount++;
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    // 验证：所有线程的调用都被引擎记录（atomic 计数无竞争）
    int expected = THREAD_COUNT * ITERS_PER_THREAD;
    REQUIRE(engine.inferCallCount == expected);
    // 成功数应 >= 1（首次 loadModel 成功后后续都成功）
    REQUIRE(successCount > 0);
}
