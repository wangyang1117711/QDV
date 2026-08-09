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
    // v5.4 测试扩展：队列模式，按顺序返回不同推理结果（用于多目标顺序测试）
    // useQueue=true 且 resultQueue 非空时，每次 infer 弹出队首；否则用默认固定结果
    // 注意：队列模式非线程安全，仅供单线程多目标测试使用（并发测试 RT-010 不启用此模式）
    bool useQueue = false;
    QJsonArray resultQueue;

    bool loadModel(const QString&, const QSize&, const cv::Scalar&, double, bool,
                   const cv::Scalar& = cv::Scalar(1.0, 1.0, 1.0)) override {
        loadModelCallCount++;
        return loadModelSuccess;
    }
    bool warmUp(int = 3) override { return true; }
    bool infer(const cv::Mat&, QJsonObject& result) override {
        inferCallCount++;
        if (!inferSuccess) return inferSuccess;
        if (useQueue && !resultQueue.isEmpty()) {
            // 队列模式：按顺序返回预置结果（用于多目标顺序测试区分不同 ROI）
            result = resultQueue[0].toObject();
            resultQueue.removeFirst();
        } else {
            // 默认模式：固定返回（保持与现有 RT-006~RT-010 测试兼容）
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
    // v5.4.0：新增接口方法的空实现
    bool detect(const cv::Mat&, double, double, QJsonObject& result) override {
        result["detections"] = QJsonArray();
        result["numDetections"] = 0;
        return true;
    }
    bool segment(const cv::Mat&, cv::Mat& mask, cv::Mat& overlay, QJsonObject& result) override {
        mask = cv::Mat();
        overlay = cv::Mat();
        result["classStats"] = QJsonObject();
        return true;
    }
    void setCategoryLabels(const QStringList&) override {}
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

// ============================================================================
// v5.4 升级测试：outputConfig 输出开关 + 多目标分类
// ============================================================================

// v5.4 用例 1：单目标按 outputConfig 过滤输出
// 配置 className=true / confidence=false / classId=false，验证开关语义对引擎原生字段同样生效
TEST_CASE("AiClassifyTool v5.4: 单目标按 outputConfig 过滤输出", "[Vision][v5.4][OutputConfig]") {
    AiClassifyTool tool;
    StubEngine* engine = new StubEngine();
    tool.setInferenceEngine(engine);

    // 构造 outputConfig：className 启用，confidence/classId 禁用
    QJsonObject en; en["enabled"] = true;
    QJsonObject dis; dis["enabled"] = false;
    QJsonObject oc;
    oc["className"] = en;
    oc["confidence"] = dis;
    oc["classId"] = dis;

    QJsonObject params;
    params["modelPath"] = "/fake/model.onnx";
    params["outputConfig"] = oc;
    REQUIRE(tool.configure(params));

    cv::Mat input(100, 100, CV_8UC3, cv::Scalar(1, 2, 3));
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE(ok);
    // className 启用 → 应存在（含向后兼容别名 category_name）
    REQUIRE(result.data.contains("className"));
    REQUIRE(result.data.contains("category_name"));
    // confidence/classId 禁用 → 应不存在（即使引擎返回了 confidence，也被 remove）
    REQUIRE_FALSE(result.data.contains("confidence"));
    REQUIRE_FALSE(result.data.contains("classId"));
    // topK 不受开关控制，始终存在（用于叠加图绘制）
    REQUIRE(result.data.contains("topK"));
    // score 不受开关影响（ToolResult 内置字段用于分支判断）
    REQUIRE_NEAR(result.score, 0.9, 1e-9);
    delete engine;
}

// v5.4 用例 2：单目标默认输出全启用（向后兼容）
// 不配置 outputConfig，验证所有标量输出存在（旧节点行为不变）
TEST_CASE("AiClassifyTool v5.4: 单目标默认输出全启用（向后兼容）", "[Vision][v5.4][OutputConfig]") {
    AiClassifyTool tool;
    StubEngine* engine = new StubEngine();
    tool.setInferenceEngine(engine);

    QJsonObject params;
    params["modelPath"] = "/fake/model.onnx";
    // 不设置 outputConfig → 所有输出默认启用
    REQUIRE(tool.configure(params));

    cv::Mat input(100, 100, CV_8UC3, cv::Scalar(1, 2, 3));
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE(ok);
    // 默认全部启用
    REQUIRE(result.data.contains("classId"));
    REQUIRE(result.data.contains("className"));
    REQUIRE(result.data.contains("confidence"));
    REQUIRE(result.data.contains("category_name"));
    delete engine;
}

// v5.4 用例 3：多目标分类输出数组
// 配置 detectionBoxes（2 个 ROI）+ 启用 classArray/confidenceArray，验证数组长度
TEST_CASE("AiClassifyTool v5.4: 多目标分类输出数组", "[Vision][v5.4][MultiTarget]") {
    AiClassifyTool tool;
    StubEngine* engine = new StubEngine();
    tool.setInferenceEngine(engine);

    // 队列模式：第 1 个给单目标（整图），后 2 个给多目标 ROI
    engine->useQueue = true;
    QJsonObject r0; r0["confidence"] = 0.9; r0["class_index"] = 0; r0["category"] = "whole";
    QJsonObject r1; r1["confidence"] = 0.8; r1["class_index"] = 1; r1["category"] = "cat";
    QJsonObject r2; r2["confidence"] = 0.7; r2["class_index"] = 2; r2["category"] = "dog";
    engine->resultQueue.append(r0);
    engine->resultQueue.append(r1);
    engine->resultQueue.append(r2);

    QJsonObject en; en["enabled"] = true;
    QJsonObject oc;
    oc["classArray"] = en;
    oc["confidenceArray"] = en;

    QJsonObject b1; b1["x"] = 10; b1["y"] = 10; b1["w"] = 50; b1["h"] = 50;
    QJsonObject b2; b2["x"] = 70; b2["y"] = 70; b2["w"] = 30; b2["h"] = 30;
    QJsonArray boxes; boxes.append(b1); boxes.append(b2);

    QJsonObject params;
    params["modelPath"] = "/fake/model.onnx";
    params["outputConfig"] = oc;
    params["detectionBoxes"] = boxes;
    REQUIRE(tool.configure(params));

    cv::Mat input(100, 100, CV_8UC3, cv::Scalar(1, 2, 3));
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE(ok);
    REQUIRE(result.data.contains("classArray"));
    REQUIRE(result.data.contains("confidenceArray"));
    REQUIRE(result.data["classArray"].isArray());
    REQUIRE(result.data["confidenceArray"].isArray());
    QJsonArray classArr = result.data["classArray"].toArray();
    QJsonArray confArr = result.data["confidenceArray"].toArray();
    REQUIRE(classArr.size() == 2);
    REQUIRE(confArr.size() == 2);
    // 单目标的整图分类结果仍存在（默认启用，与多目标数组并存）
    REQUIRE(result.data.contains("className"));
    delete engine;
}

// v5.4 用例 4：多目标数组顺序与 detectionBoxes 一致
// 通过队列返回不同 category/confidence 区分两个 ROI，验证顺序保序
TEST_CASE("AiClassifyTool v5.4: 多目标数组顺序与 detectionBoxes 一致", "[Vision][v5.4][MultiTarget]") {
    AiClassifyTool tool;
    StubEngine* engine = new StubEngine();
    tool.setInferenceEngine(engine);

    engine->useQueue = true;
    QJsonObject r0; r0["confidence"] = 0.9; r0["class_index"] = 0; r0["category"] = "whole";
    QJsonObject r1; r1["confidence"] = 0.8; r1["class_index"] = 1; r1["category"] = "cat";
    QJsonObject r2; r2["confidence"] = 0.7; r2["class_index"] = 2; r2["category"] = "dog";
    engine->resultQueue.append(r0);
    engine->resultQueue.append(r1);
    engine->resultQueue.append(r2);

    QJsonObject en; en["enabled"] = true;
    QJsonObject oc;
    oc["classArray"] = en;
    oc["confidenceArray"] = en;

    QJsonObject b1; b1["x"] = 10; b1["y"] = 10; b1["w"] = 50; b1["h"] = 50;
    QJsonObject b2; b2["x"] = 70; b2["y"] = 70; b2["w"] = 30; b2["h"] = 30;
    QJsonArray boxes; boxes.append(b1); boxes.append(b2);

    QJsonObject params;
    params["modelPath"] = "/fake/model.onnx";
    params["outputConfig"] = oc;
    params["detectionBoxes"] = boxes;
    REQUIRE(tool.configure(params));

    cv::Mat input(100, 100, CV_8UC3, cv::Scalar(1, 2, 3));
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE(ok);
    QJsonArray classArr = result.data["classArray"].toArray();
    QJsonArray confArr = result.data["confidenceArray"].toArray();
    REQUIRE(classArr.size() == 2);
    REQUIRE(confArr.size() == 2);
    // categoryLabels 为空 → className 取自 category 字段
    // 顺序与 detectionBoxes 一致：第 1 个 ROI=cat，第 2 个 ROI=dog
    REQUIRE(classArr[0].toString() == "cat");
    REQUIRE(classArr[1].toString() == "dog");
    REQUIRE_NEAR(confArr[0].toDouble(), 0.8, 1e-9);
    REQUIRE_NEAR(confArr[1].toDouble(), 0.7, 1e-9);
    delete engine;
}

// v5.4 用例 5：多目标默认不输出数组（向后兼容）
// 配置 detectionBoxes 但不启用 classArray/confidenceArray，验证数组字段缺失
// 而单目标的 className 仍存在（单目标默认启用）
TEST_CASE("AiClassifyTool v5.4: 多目标默认不输出数组（向后兼容）", "[Vision][v5.4][MultiTarget]") {
    AiClassifyTool tool;
    StubEngine* engine = new StubEngine();
    tool.setInferenceEngine(engine);

    QJsonObject b1; b1["x"] = 10; b1["y"] = 10; b1["w"] = 50; b1["h"] = 50;
    QJsonObject b2; b2["x"] = 70; b2["y"] = 70; b2["w"] = 30; b2["h"] = 30;
    QJsonArray boxes; boxes.append(b1); boxes.append(b2);

    QJsonObject params;
    params["modelPath"] = "/fake/model.onnx";
    params["detectionBoxes"] = boxes;
    // 不设置 outputConfig → classArray/confidenceArray 默认 false
    REQUIRE(tool.configure(params));

    cv::Mat input(100, 100, CV_8UC3, cv::Scalar(1, 2, 3));
    ToolResult result;
    bool ok = tool.execute(input, result);
    REQUIRE(ok);
    // 多目标数组默认不输出
    REQUIRE_FALSE(result.data.contains("classArray"));
    REQUIRE_FALSE(result.data.contains("confidenceArray"));
    // 单目标 className 仍存在（默认启用）
    REQUIRE(result.data.contains("className"));
    delete engine;
}
