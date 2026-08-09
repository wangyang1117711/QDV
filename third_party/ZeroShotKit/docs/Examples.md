# ZeroShotKit 示例代码

本文档提供 8 个完整可编译的示例，覆盖 ZeroShotKit 的主要使用场景。每个示例都包含完整的 `#include`、`main` 函数、错误处理与 CMake 片段。

## 目录

- [示例 1：最简推理（AnomalyCLIP）](#示例-1最简推理anomalyclip)
- [示例 2：批量推理（GroundingDINO）](#示例-2批量推理groundingdino)
- [示例 3：异步推理](#示例-3异步推理)
- [示例 4：PatchCore 流程](#示例-4patchcore-流程)
- [示例 5：流水线推理](#示例-5流水线推理)
- [示例 6：评估流程](#示例-6评估流程)
- [示例 7：UI 集成](#示例-7-ui-集成)
- [示例 8：稳定性推理](#示例-8稳定性推理)
- [通用 CMake 模板](#通用-cmake-模板)

---

## 示例 1：最简推理（AnomalyCLIP）

加载 AnomalyCLIP 模型，对单张图推理，输出异常分数。

```cpp
// example_01_minimal.cpp
#include "ZeroShotKit/ZeroShotKit.h"
#include <QCoreApplication>
#include <QFile>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>

// 支持中文路径的图像读取工具函数
static cv::Mat readImageChineseSafe(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return cv::Mat();
    }
    QByteArray data = file.readAll();
    return cv::imdecode(std::vector<uchar>(data.begin(), data.end()), cv::IMREAD_COLOR);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    zsu::Kit kit;

    // 1. 加载 AnomalyCLIP 模型（路径指向模型所在目录）
    const QString modelDir = "D:/models/anomaly_clip";
    if (!kit.loadModel(zsu::ZeroShotModelType::AnomalyCLIP, modelDir)) {
        std::cerr << "模型加载失败，请检查路径: " << modelDir.toUtf8().constData() << std::endl;
        return 1;
    }
    std::cout << "模型加载成功，类型: " << static_cast<int>(kit.modelType()) << std::endl;

    // 2. 配置文本提示词（normal: 与 anomaly: 前缀必须成对出现）
    kit.setTextPrompts({
        "normal:a photo of a normal product",
        "anomaly:a photo of a damaged product"
    });
    kit.setAnomalyThreshold(0.5f);

    // 3. 读取图像（支持中文路径）
    const QString imagePath = "D:/test/sample.jpg";
    cv::Mat img = readImageChineseSafe(imagePath);
    if (img.empty()) {
        std::cerr << "图像读取失败: " << imagePath.toUtf8().constData() << std::endl;
        return 1;
    }

    // 4. 同步推理
    zsu::ZeroShotResult result = kit.infer(img);
    if (!result.success) {
        std::cerr << "推理失败: " << result.errorMessage.toUtf8().constData() << std::endl;
        return 1;
    }

    // 5. 输出异常分数与性能指标
    std::cout << "=== 推理结果 ===" << std::endl;
    std::cout << "category:       " << result.category.toUtf8().constData() << std::endl;
    std::cout << "anomaly_score:  " << result.anomalyScore << std::endl;
    std::cout << "confidence:     " << result.confidence << std::endl;
    std::cout << "latency_total:  " << result.metrics.totalMs << " ms" << std::endl;
    std::cout << "  preprocess:   " << result.metrics.preprocessMs << " ms" << std::endl;
    std::cout << "  inference:    " << result.metrics.inferenceMs << " ms" << std::endl;
    std::cout << "  postprocess:  " << result.metrics.postprocessMs << " ms" << std::endl;
    std::cout << "backend:        " << result.metrics.backend.toUtf8().constData() << std::endl;

    // 判定结果
    if (result.anomalyScore > kit.anomalyThreshold()) {
        std::cout << "判定: 异常" << std::endl;
    } else {
        std::cout << "判定: 正常" << std::endl;
    }

    return 0;
}
```

CMake 片段：

```cmake
add_executable(example_01_minimal example_01_minimal.cpp)
target_link_libraries(example_01_minimal PRIVATE
    Qt6::Core
    Qt6::Concurrent
    ${OpenCV_LIBS}
    ZeroShotKit::Core
)
```

---

## 示例 2：批量推理（GroundingDINO）

加载 GroundingDINO，遍历目录批量推理，结果保存为 JSON。

```cpp
// example_02_batch.cpp
#include "ZeroShotKit/ZeroShotKit.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>

// 支持中文路径的图像读取工具函数
static cv::Mat readImageChineseSafe(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return cv::Mat();
    QByteArray data = file.readAll();
    return cv::imdecode(std::vector<uchar>(data.begin(), data.end()), cv::IMREAD_COLOR);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    if (argc < 3) {
        std::cerr << "用法: " << argv[0] << " <图像目录> <输出JSON路径>" << std::endl;
        return 1;
    }
    const QString inputDir = QString::fromUtf8(argv[1]);
    const QString outputPath = QString::fromUtf8(argv[2]);

    zsu::Kit kit;

    // 加载 GroundingDINO 模型
    if (!kit.loadModel(zsu::ZeroShotModelType::GroundingDINO, "D:/models/grounding_dino")) {
        std::cerr << "模型加载失败" << std::endl;
        return 1;
    }

    // 配置检测提示词（点号分隔）与检测阈值
    kit.setTextPrompts({"scratch . dent . stain . pill . zipper"});
    kit.setDetectionThreshold(0.3f);

    // 遍历目录，收集图像路径
    QDir dir(inputDir);
    QStringList filters = {"*.jpg", "*.jpeg", "*.png", "*.bmp"};
    QStringList files = dir.entryList(filters, QDir::Files);
    if (files.isEmpty()) {
        std::cerr << "目录中没有图像: " << inputDir.toUtf8().constData() << std::endl;
        return 1;
    }

    // 读取所有图像
    QList<cv::Mat> images;
    QStringList imagePaths;
    for (const QString& fileName : files) {
        QString fullPath = dir.absoluteFilePath(fileName);
        cv::Mat img = readImageChineseSafe(fullPath);
        if (img.empty()) {
            std::cerr << "跳过无法读取的图像: " << fullPath.toUtf8().constData() << std::endl;
            continue;
        }
        images.append(img);
        imagePaths.append(fullPath);
    }

    std::cout << "准备批量推理 " << images.size() << " 张图像..." << std::endl;

    // 批量同步推理
    QList<zsu::ZeroShotResult> results = kit.inferBatch(images);

    // 组装 JSON 报告
    QJsonArray resultArray;
    for (int i = 0; i < results.size() && i < imagePaths.size(); ++i) {
        QJsonObject item;
        item["image_path"] = imagePaths[i];
        item["result"] = results[i].toJson();
        resultArray.append(item);
    }

    QJsonObject root;
    root["model_type"] = "GroundingDINO";
    root["total_images"] = images.size();
    root["results"] = resultArray;

    // 保存到文件
    QFile outFile(outputPath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        std::cerr << "无法写入输出文件: " << outputPath.toUtf8().constData() << std::endl;
        return 1;
    }
    outFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    outFile.close();

    std::cout << "批量推理完成，结果已保存到: " << outputPath.toUtf8().constData() << std::endl;
    std::cout << "成功: " << results.size() << " 张" << std::endl;
    return 0;
}
```

CMake 片段：

```cmake
add_executable(example_02_batch example_02_batch.cpp)
target_link_libraries(example_02_batch PRIVATE
    Qt6::Core
    Qt6::Concurrent
    ${OpenCV_LIBS}
    ZeroShotKit::Core
)
```

---

## 示例 3：异步推理

使用 `Kit::inferAsync`，连接信号，主线程不阻塞。

```cpp
// example_03_async.cpp
#include "ZeroShotKit/ZeroShotKit.h"
#include <QCoreApplication>
#include <QTimer>
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    zsu::Kit kit;

    // 加载 AnomalyCLIP 模型
    if (!kit.loadModel(zsu::ZeroShotModelType::AnomalyCLIP, "D:/models/anomaly_clip")) {
        std::cerr << "模型加载失败" << std::endl;
        return 1;
    }

    // 配置提示词
    kit.setTextPrompts({"normal:a photo of a normal product",
                        "anomaly:a photo of a damaged product"});

    // 连接信号（在主线程处理结果）
    QObject::connect(&kit, &zsu::Kit::inferenceCompleted,
        [](const zsu::ZeroShotResult& result) {
            std::cout << "=== 收到推理结果 ===" << std::endl;
            if (result.success) {
                std::cout << "anomaly_score: " << result.anomalyScore << std::endl;
                std::cout << "latency_ms:    " << result.metrics.totalMs << std::endl;
            } else {
                std::cerr << "推理失败: " << result.errorMessage.toUtf8().constData() << std::endl;
            }
            std::cout << "主线程未阻塞，可以继续处理其他事件" << std::endl;
            QCoreApplication::quit();
        });

    QObject::connect(&kit, &zsu::Kit::errorOccurred,
        [](const QString& msg) {
            std::cerr << "错误: " << msg.toUtf8().constData() << std::endl;
            QCoreApplication::quit();
        });

    QObject::connect(&kit, &zsu::Kit::progressUpdated,
        [](int current, int total) {
            std::cout << "进度: " << current << "/" << total << std::endl;
        });

    // 启动异步推理
    const QString imagePath = "D:/test/sample.jpg";
    std::cout << "启动异步推理: " << imagePath.toUtf8().constData() << std::endl;
    kit.inferAsync(imagePath);

    // 设置超时保护（30 秒未完成则退出）
    QTimer::singleShot(30000, []() {
        std::cerr << "推理超时" << std::endl;
        QCoreApplication::quit();
    });

    // 主线程进入事件循环，不阻塞
    std::cout << "主线程进入事件循环..." << std::endl;
    return app.exec();
}
```

CMake 片段：

```cmake
add_executable(example_03_async example_03_async.cpp)
target_link_libraries(example_03_async PRIVATE
    Qt6::Core
    Qt6::Concurrent
    ${OpenCV_LIBS}
    ZeroShotKit::Core
)
```

---

## 示例 4：PatchCore 流程

添加 10 个正常样本 -> 推理 -> 输出异常分数。

```cpp
// example_04_patchcore.cpp
#include "ZeroShotKit/ZeroShotKit.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>

// 支持中文路径的图像读取工具函数
static cv::Mat readImageChineseSafe(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return cv::Mat();
    QByteArray data = file.readAll();
    return cv::imdecode(std::vector<uchar>(data.begin(), data.end()), cv::IMREAD_COLOR);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    if (argc < 3) {
        std::cerr << "用法: " << argv[0] << " <正常样本目录> <测试图像>" << std::endl;
        return 1;
    }
    const QString normalDir = QString::fromUtf8(argv[1]);
    const QString testImage = QString::fromUtf8(argv[2]);

    zsu::Kit kit;

    // 1. 加载 PatchCore 模型（复用 CLIP 视觉编码器）
    if (!kit.loadModel(zsu::ZeroShotModelType::PatchCore, "D:/models/anomaly_clip")) {
        std::cerr << "模型加载失败" << std::endl;
        return 1;
    }
    std::cout << "PatchCore 模型加载成功" << std::endl;

    // 2. 添加正常样本到 memory bank
    QDir dir(normalDir);
    QStringList filters = {"*.jpg", "*.jpeg", "*.png", "*.bmp"};
    QStringList files = dir.entryList(filters, QDir::Files);

    int added = 0;
    for (const QString& fileName : files) {
        if (added >= 10) break;  // 示例只添加 10 个
        QString fullPath = dir.absoluteFilePath(fileName);
        cv::Mat img = readImageChineseSafe(fullPath);
        if (img.empty()) {
            std::cerr << "跳过: " << fullPath.toUtf8().constData() << std::endl;
            continue;
        }
        if (kit.addNormalSample(img)) {
            ++added;
            std::cout << "已添加正常样本 [" << added << "/10]: "
                      << fileName.toUtf8().constData() << std::endl;
        } else {
            std::cerr << "添加失败: " << fileName.toUtf8().constData() << std::endl;
        }
    }

    if (added < 1) {
        std::cerr << "未添加任何正常样本" << std::endl;
        return 1;
    }

    std::cout << "Memory bank 样本数: " << kit.normalSampleCount() << std::endl;
    std::cout << "渐进式切换阈值: " << kit.engine()->progressiveSwitchThreshold() << std::endl;

    // 3. 读取测试图像
    cv::Mat img = readImageChineseSafe(testImage);
    if (img.empty()) {
        std::cerr << "测试图像读取失败: " << testImage.toUtf8().constData() << std::endl;
        return 1;
    }

    // 4. 推理
    zsu::ZeroShotResult result = kit.infer(img);
    if (!result.success) {
        std::cerr << "推理失败: " << result.errorMessage.toUtf8().constData() << std::endl;
        return 1;
    }

    // 5. 输出结果
    std::cout << "=== PatchCore 推理结果 ===" << std::endl;
    std::cout << "anomaly_score: " << result.anomalyScore << std::endl;
    std::cout << "anomaly_map:   " << result.anomalyMap.cols << "x" << result.anomalyMap.rows
              << " (channels=" << result.anomalyMap.channels() << ")" << std::endl;
    std::cout << "latency_ms:    " << result.metrics.totalMs << std::endl;

    // 异常分数 = 1 - 与 memory bank 中最相似样本的归一化相似度
    if (result.anomalyScore > 0.5) {
        std::cout << "判定: 异常（与正常样本差异较大）" << std::endl;
    } else {
        std::cout << "判定: 正常（与 memory bank 匹配）" << std::endl;
    }

    // 6. （可选）清理 memory bank
    kit.clearNormalSamples();
    std::cout << "清理后样本数: " << kit.normalSampleCount() << std::endl;

    return 0;
}
```

CMake 片段：

```cmake
add_executable(example_04_patchcore example_04_patchcore.cpp)
target_link_libraries(example_04_patchcore PRIVATE
    Qt6::Core
    Qt6::Concurrent
    ${OpenCV_LIBS}
    ZeroShotKit::Core
)
```

---

## 示例 5：流水线推理

配置 3 阶段流水线（异常检测 -> 目标检测 -> 分割），打印每阶段耗时。

```cpp
// example_05_pipeline.cpp
#include "ZeroShotKit/ZeroShotKit.h"
#include <QCoreApplication>
#include <QFile>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>

static cv::Mat readImageChineseSafe(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return cv::Mat();
    QByteArray data = file.readAll();
    return cv::imdecode(std::vector<uchar>(data.begin(), data.end()), cv::IMREAD_COLOR);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <测试图像>" << std::endl;
        return 1;
    }
    const QString testImage = QString::fromUtf8(argv[1]);

    zsu::Kit kit;

    // 加载 AnomalyCLIP 模型（流水线复用同一引擎，三个阶段共用）
    if (!kit.loadModel(zsu::ZeroShotModelType::AnomalyCLIP, "D:/models/anomaly_clip")) {
        std::cerr << "模型加载失败" << std::endl;
        return 1;
    }

    // 配置文本提示词（AnomalyCLIP 阶段使用）
    kit.setTextPrompts({"normal:a photo of a normal product",
                        "anomaly:a photo of a damaged product"});

    // 配置流水线阶段
    zsu::PipelineStageConfig stageCfg;
    stageCfg.enableAnomalyDetect = true;       // 阶段 1: 异常检测
    stageCfg.enableObjectDetect = true;        // 阶段 2: 目标检测
    stageCfg.enableSegmentation = true;        // 阶段 3: 分割
    stageCfg.anomalyTriggerThreshold = 0.5f;   // 异常分数 > 0.5 才触发后续
    stageCfg.detectionTriggerThreshold = 0.3f; // 检测分数 > 0.3 才触发分割
    kit.pipeline()->setStageConfig(stageCfg);

    // 设置串行模式（并行模式可启用 setParallelMode(true)）
    kit.pipeline()->setParallelMode(false);

    // 读取图像
    cv::Mat img = readImageChineseSafe(testImage);
    if (img.empty()) {
        std::cerr << "图像读取失败: " << testImage.toUtf8().constData() << std::endl;
        return 1;
    }

    // 执行流水线推理
    std::cout << "启动流水线推理..." << std::endl;
    zsu::PipelineResult pr = kit.pipeline()->infer(img);
    if (!pr.success) {
        std::cerr << "流水线失败: " << pr.errorMessage.toUtf8().constData() << std::endl;
        return 1;
    }

    // 打印各阶段执行情况与耗时
    std::cout << "=== 流水线结果 ===" << std::endl;
    std::cout << "[阶段1] 异常检测  执行=" << pr.anomalyExecuted
              << "  耗时=" << pr.anomalyLatencyMs << " ms"
              << "  anomaly_score=" << pr.anomalyResult.anomalyScore << std::endl;
    std::cout << "[阶段2] 目标检测  执行=" << pr.detectionExecuted
              << "  耗时=" << pr.detectionLatencyMs << " ms"
              << "  detections=" << pr.detectionResult.detections.size() << std::endl;
    std::cout << "[阶段3] 分割      执行=" << pr.segmentExecuted
              << "  耗时=" << pr.segmentLatencyMs << " ms"
              << "  mask_size=" << pr.segmentResult.mask.cols << "x" << pr.segmentResult.mask.rows
              << std::endl;
    std::cout << "总耗时: " << pr.totalLatencyMs << " ms" << std::endl;

    // 查看流水线统计
    zsu::PipelineStats stats = kit.pipeline()->stats();
    std::cout << std::endl;
    std::cout << "=== 流水线统计 ===" << std::endl;
    std::cout << "total_inferences:        " << stats.totalInferences << std::endl;
    std::cout << "successful_inferences:   " << stats.successfulInferences << std::endl;
    std::cout << "avg_latency_ms:          " << stats.avgLatencyMs << std::endl;
    std::cout << "anomaly_executions:      " << stats.anomalyExecutions << std::endl;
    std::cout << "detection_executions:    " << stats.detectionExecutions << std::endl;
    std::cout << "segment_executions:      " << stats.segmentExecutions << std::endl;

    return 0;
}
```

CMake 片段：

```cmake
add_executable(example_05_pipeline example_05_pipeline.cpp)
target_link_libraries(example_05_pipeline PRIVATE
    Qt6::Core
    Qt6::Concurrent
    ${OpenCV_LIBS}
    ZeroShotKit::Core
)
```

---

## 示例 6：评估流程

从 `normal/` 和 `anomaly/` 目录加载测试集 -> 评估 -> 生成报告。

```cpp
// example_06_evaluation.cpp
#include "ZeroShotKit/ZeroShotKit.h"
#include <QCoreApplication>
#include <QDir>
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    if (argc < 3) {
        std::cerr << "用法: " << argv[0] << " <数据集根目录> <报告输出路径>" << std::endl;
        std::cerr << "  数据集根目录应包含 normal/ 与 anomaly/ 两个子目录" << std::endl;
        return 1;
    }
    const QString datasetDir = QString::fromUtf8(argv[1]);
    const QString reportPath = QString::fromUtf8(argv[2]);

    zsu::Kit kit;

    // 加载 AnomalyCLIP 模型
    if (!kit.loadModel(zsu::ZeroShotModelType::AnomalyCLIP, "D:/models/anomaly_clip")) {
        std::cerr << "模型加载失败" << std::endl;
        return 1;
    }
    kit.setTextPrompts({"normal:a photo of a normal product",
                        "anomaly:a photo of a damaged product"});
    kit.setAnomalyThreshold(0.5f);

    // 1. 从目录加载数据集
    //    目录结构：
    //      <datasetDir>/normal/*.jpg   (正常样本)
    //      <datasetDir>/anomaly/*.jpg  (异常样本)
    std::cout << "加载数据集: " << datasetDir.toUtf8().constData() << std::endl;
    QList<zsu::EvalSample> samples = zsu::EvaluationEngine::loadDatasetFromDirectory(datasetDir);
    if (samples.isEmpty()) {
        std::cerr << "数据集为空，请检查目录结构" << std::endl;
        return 1;
    }

    int normalCount = 0, anomalyCount = 0;
    for (const auto& s : samples) {
        if (s.isAnomaly) ++anomalyCount;
        else ++normalCount;
    }
    std::cout << "已加载 " << samples.size() << " 个样本"
              << "（正常 " << normalCount << "，异常 " << anomalyCount << "）" << std::endl;

    // 2. 添加到评估引擎
    kit.evaluator()->addSamples(samples);

    // 3. 执行评估
    std::cout << "开始评估..." << std::endl;
    zsu::EvalMetrics metrics = kit.evaluator()->evaluate(kit.engine(), 0.5f);

    // 4. 打印关键指标
    std::cout << std::endl;
    std::cout << "=== 评估指标 ===" << std::endl;
    std::cout << "total_samples:       " << metrics.totalSamples << std::endl;
    std::cout << "correct_predictions: " << metrics.correctPredictions << std::endl;
    std::cout << "accuracy:            " << metrics.accuracy << std::endl;
    std::cout << "precision:           " << metrics.precision << std::endl;
    std::cout << "recall:              " << metrics.recall << std::endl;
    std::cout << "f1_score:            " << metrics.f1Score << std::endl;
    std::cout << std::endl;
    std::cout << "--- 异常检测指标 ---" << std::endl;
    std::cout << "auroc:               " << metrics.auroc << std::endl;
    std::cout << "fpr:                 " << metrics.fpr << std::endl;
    std::cout << "tpr:                 " << metrics.tpr << std::endl;
    std::cout << std::endl;
    std::cout << "--- 性能指标 ---" << std::endl;
    std::cout << "avg_latency_ms:      " << metrics.avgLatencyMs << std::endl;
    std::cout << "min_latency_ms:      " << metrics.minLatencyMs << std::endl;
    std::cout << "max_latency_ms:      " << metrics.maxLatencyMs << std::endl;
    std::cout << "p50_latency_ms:      " << metrics.p50LatencyMs << std::endl;
    std::cout << "p99_latency_ms:      " << metrics.p99LatencyMs << std::endl;
    std::cout << "throughput_fps:      " << metrics.throughputFPS << std::endl;

    // 5. 保存报告（JSON 格式）
    if (!kit.evaluator()->saveReport(reportPath, true)) {
        std::cerr << "报告保存失败: " << reportPath.toUtf8().constData() << std::endl;
        return 1;
    }
    std::cout << std::endl;
    std::cout << "JSON 报告已保存: " << reportPath.toUtf8().constData() << std::endl;

    // 6. 同时输出文本报告到控制台
    std::cout << std::endl;
    std::cout << "=== 文本报告 ===" << std::endl;
    std::cout << kit.evaluator()->generateTextReport().toUtf8().constData() << std::endl;

    return 0;
}
```

CMake 片段：

```cmake
add_executable(example_06_evaluation example_06_evaluation.cpp)
target_link_libraries(example_06_evaluation PRIVATE
    Qt6::Core
    Qt6::Concurrent
    ${OpenCV_LIBS}
    ZeroShotKit::Core
)
```

---

## 示例 7：UI 集成

QWidget 主窗口嵌入 `ZeroShotPanel` + `ZeroShotResultPanel`。

```cpp
// example_07_ui.cpp
#include <QApplication>
#include <QMainWindow>
#include <QSplitter>
#include <QMenuBar>
#include <QAction>
#include <QMessageBox>
#include <QStatusBar>
#include "ZeroShotKit/ZeroShotKit.h"
#include "ZeroShotPanel.h"
#include "ZeroShotResultPanel.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        // 创建 Kit（生命周期由 MainWindow 管理）
        m_kit = new zsu::Kit(this);

        // 创建 UI 面板
        m_panel = new QDVMini::ZeroShotPanel(this);
        m_resultPanel = new QDVMini::ZeroShotResultPanel(this);

        // 注入引擎到配置面板
        m_panel->setEngine(m_kit->engine());
        m_panel->setModelNotesManager(m_kit->notesManager());
        m_panel->setBadCaseRecorder(m_kit->badCaseRecorder());

        // 布局：左侧配置面板，右侧结果展示面板
        QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
        splitter->addWidget(m_panel);
        splitter->addWidget(m_resultPanel);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 2);
        setCentralWidget(splitter);

        resize(1280, 800);
        setWindowTitle("ZeroShotKit UI 示例");

        setupMenu();
        setupConnections();

        statusBar()->showMessage("就绪");
    }

private slots:
    void onInferenceCompleted(const zsu::ZeroShotResult& result) {
        m_resultPanel->setResult(result);
        statusBar()->showMessage(QString("推理完成，耗时 %1 ms").arg(result.metrics.totalMs), 5000);
    }

    void onBatchCompleted(const QList<zsu::ZeroShotResult>& results) {
        m_resultPanel->setResults(results);
        statusBar()->showMessage(QString("批量推理完成，共 %1 张").arg(results.size()), 5000);
    }

    void onProgress(int current, int total) {
        m_resultPanel->setProgress(current, total);
        statusBar()->showMessage(QString("进度: %1 / %2").arg(current).arg(total));
    }

    void onError(const QString& msg) {
        QMessageBox::warning(this, "错误", msg);
    }

private:
    void setupMenu() {
        QMenu* fileMenu = menuBar()->addMenu("文件(&F)");
        QAction* quitAct = fileMenu->addAction("退出(&Q)");
        connect(quitAct, &QAction::triggered, this, &QMainWindow::close);

        QMenu* helpMenu = menuBar()->addMenu("帮助(&H)");
        QAction* aboutAct = helpMenu->addAction("关于(&A)");
        connect(aboutAct, &QAction::triggered, this, [this]() {
            QMessageBox::information(this, "关于",
                "ZeroShotKit UI 示例\n基于 Qt6 + OpenCV + ONNX Runtime");
        });
    }

    void setupConnections() {
        // Kit 信号 -> 主窗口槽
        connect(m_kit, &zsu::Kit::inferenceCompleted,
                this, &MainWindow::onInferenceCompleted);
        connect(m_kit, &zsu::Kit::batchCompleted,
                this, &MainWindow::onBatchCompleted);
        connect(m_kit, &zsu::Kit::progressUpdated,
                this, &MainWindow::onProgress);
        connect(m_kit, &zsu::Kit::errorOccurred,
                this, &MainWindow::onError);

        // 配置面板 -> 触发推理
        // 实际项目应通过 ZeroShotPanel 的 signals 触发 kit->inferAsync
        // 此处仅演示连接方式
    }

private:
    zsu::Kit* m_kit = nullptr;
    QDVMini::ZeroShotPanel* m_panel = nullptr;
    QDVMini::ZeroShotResultPanel* m_resultPanel = nullptr;
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    return app.exec();
}

#include "example_07_ui.moc"  // Q_OBJECT 内嵌时需要 MOC
```

CMake 片段：

```cmake
# UI 示例需要启用 ZEROSHOTKIT_BUILD_UI
set(ZEROSHOTKIT_BUILD_UI ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/ZeroShotKit)

add_executable(example_07_ui example_07_ui.cpp)
target_link_libraries(example_07_ui PRIVATE
    Qt6::Core
    Qt6::Concurrent
    Qt6::Widgets
    ${OpenCV_LIBS}
    ZeroShotKit::Core
    ZeroShotKit::UI
)
```

---

## 示例 8：稳定性推理

配置 `StabilityConfig` 多次推理取稳定值。

```cpp
// example_08_stability.cpp
#include "ZeroShotKit/ZeroShotKit.h"
#include <QCoreApplication>
#include <QFile>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>

static cv::Mat readImageChineseSafe(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return cv::Mat();
    QByteArray data = file.readAll();
    return cv::imdecode(std::vector<uchar>(data.begin(), data.end()), cv::IMREAD_COLOR);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <测试图像>" << std::endl;
        return 1;
    }
    const QString testImage = QString::fromUtf8(argv[1]);

    zsu::Kit kit;

    // 加载 GroundingDINO 模型（稳定性推理对检测场景效果最明显）
    if (!kit.loadModel(zsu::ZeroShotModelType::GroundingDINO, "D:/models/grounding_dino")) {
        std::cerr << "模型加载失败" << std::endl;
        return 1;
    }
    kit.setTextPrompts({"scratch . dent . stain"});
    kit.setDetectionThreshold(0.3f);

    // 读取图像
    cv::Mat img = readImageChineseSafe(testImage);
    if (img.empty()) {
        std::cerr << "图像读取失败: " << testImage.toUtf8().constData() << std::endl;
        return 1;
    }

    // === 第一阶段：未启用稳定性推理（单次） ===
    std::cout << "=== 单次推理（未启用稳定性） ===" << std::endl;
    zsu::ZeroShotResult singleResult = kit.infer(img);
    if (singleResult.success) {
        std::cout << "detections: " << singleResult.detections.size()
                  << "  latency: " << singleResult.metrics.totalMs << " ms" << std::endl;
        for (size_t i = 0; i < singleResult.detections.size(); ++i) {
            const auto& d = singleResult.detections[i];
            std::cout << "  [" << i << "] " << d.className.toUtf8().constData()
                      << "  conf=" << d.confidence
                      << "  box=(" << d.cx << "," << d.cy << "," << d.w << "," << d.h << ")"
                      << std::endl;
        }
    }

    // === 第二阶段：启用稳定性推理 ===
    std::cout << std::endl;
    std::cout << "=== 配置稳定性推理 ===" << std::endl;
    zsu::StabilityConfig cfg = kit.stabilityConfig();
    cfg.enableMultiRunStability = true;  // 启用多次推理取稳定值
    cfg.numRuns = 5;                     // 推理 5 次取稳定值
    cfg.nmsIouThreshold = 0.45f;         // NMS IoU 阈值
    cfg.confidenceThreshold = 0.3f;      // 置信度过滤
    kit.setStabilityConfig(cfg);

    std::cout << "numRuns:                 " << cfg.numRuns << std::endl;
    std::cout << "nmsIouThreshold:         " << cfg.nmsIouThreshold << std::endl;
    std::cout << "confidenceThreshold:     " << cfg.confidenceThreshold << std::endl;
    std::cout << "enableMultiRunStability: " << cfg.enableMultiRunStability << std::endl;

    // 启用后 Kit::infer 会自动调用 ZeroShotEngine::inferStable
    std::cout << std::endl;
    std::cout << "=== 稳定性推理（5 次取稳定值） ===" << std::endl;
    zsu::ZeroShotResult stableResult = kit.infer(img);
    if (!stableResult.success) {
        std::cerr << "推理失败: " << stableResult.errorMessage.toUtf8().constData() << std::endl;
        return 1;
    }

    std::cout << "detections: " << stableResult.detections.size()
              << "  latency: " << stableResult.metrics.totalMs << " ms"
              << "  (含 5 次推理耗时)" << std::endl;
    for (size_t i = 0; i < stableResult.detections.size(); ++i) {
        const auto& d = stableResult.detections[i];
        std::cout << "  [" << i << "] " << d.className.toUtf8().constData()
                  << "  conf=" << d.confidence
                  << "  box=(" << d.cx << "," << d.cy << "," << d.w << "," << d.h << ")"
                  << std::endl;
    }

    // === 第三阶段：对比单次与稳定性结果 ===
    std::cout << std::endl;
    std::cout << "=== 结果对比 ===" << std::endl;
    std::cout << "单次推理:   detections=" << singleResult.detections.size()
              << "  latency=" << singleResult.metrics.totalMs << " ms" << std::endl;
    std::cout << "稳定性推理: detections=" << stableResult.detections.size()
              << "  latency=" << stableResult.metrics.totalMs << " ms" << std::endl;
    std::cout << "说明: 稳定性推理通过多次推理取交集/众数，结果更稳定但耗时更高" << std::endl;

    // === 第四阶段：直接调用 NMS 静态方法 ===
    std::cout << std::endl;
    std::cout << "=== 手动调用 NMS 去重 ===" << std::endl;
    if (!singleResult.detections.empty()) {
        auto nmsed = zsu::ZeroShotEngine::applyNMS(singleResult.detections, 0.45f);
        std::cout << "NMS 前: " << singleResult.detections.size()
                  << "  NMS 后: " << nmsed.size() << std::endl;
    }

    return 0;
}
```

CMake 片段：

```cmake
add_executable(example_08_stability example_08_stability.cpp)
target_link_libraries(example_08_stability PRIVATE
    Qt6::Core
    Qt6::Concurrent
    ${OpenCV_LIBS}
    ZeroShotKit::Core
)
```

---

## 通用 CMake 模板

以下 CMake 模板可同时编译上述所有示例（除示例 7 需 UI 支持）：

```cmake
cmake_minimum_required(VERSION 3.16)
project(ZeroShotKitExamples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# 查找依赖
find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Widgets)
find_package(OpenCV REQUIRED)

# 配置 ONNX Runtime（按需修改路径）
# set(ONNXRUNTIME_INCLUDE_DIRS "D:/onnxruntime/include")
# set(ONNXRUNTIME_LIBS "D:/onnxruntime/lib/onnxruntime.lib")

# 启用 UI（示例 7 需要）
set(ZEROSHOTKIT_BUILD_UI ON CACHE BOOL "" FORCE)

# 添加 ZeroShotKit
add_subdirectory(third_party/ZeroShotKit)

# 通用链接库
set(ZSU_EXAMPLE_LIBS
    Qt6::Core
    Qt6::Concurrent
    ${OpenCV_LIBS}
    ZeroShotKit::Core
)

# 示例 1: 最简推理
add_executable(example_01_minimal example_01_minimal.cpp)
target_link_libraries(example_01_minimal PRIVATE ${ZSU_EXAMPLE_LIBS})

# 示例 2: 批量推理
add_executable(example_02_batch example_02_batch.cpp)
target_link_libraries(example_02_batch PRIVATE ${ZSU_EXAMPLE_LIBS})

# 示例 3: 异步推理
add_executable(example_03_async example_03_async.cpp)
target_link_libraries(example_03_async PRIVATE ${ZSU_EXAMPLE_LIBS})

# 示例 4: PatchCore 流程
add_executable(example_04_patchcore example_04_patchcore.cpp)
target_link_libraries(example_04_patchcore PRIVATE ${ZSU_EXAMPLE_LIBS})

# 示例 5: 流水线推理
add_executable(example_05_pipeline example_05_pipeline.cpp)
target_link_libraries(example_05_pipeline PRIVATE ${ZSU_EXAMPLE_LIBS})

# 示例 6: 评估流程
add_executable(example_06_evaluation example_06_evaluation.cpp)
target_link_libraries(example_06_evaluation PRIVATE ${ZSU_EXAMPLE_LIBS})

# 示例 7: UI 集成（额外需要 Qt6::Widgets 与 ZeroShotKit::UI）
add_executable(example_07_ui example_07_ui.cpp)
target_link_libraries(example_07_ui PRIVATE
    Qt6::Core Qt6::Concurrent Qt6::Widgets
    ${OpenCV_LIBS}
    ZeroShotKit::Core
    ZeroShotKit::UI
)

# 示例 8: 稳定性推理
add_executable(example_08_stability example_08_stability.cpp)
target_link_libraries(example_08_stability PRIVATE ${ZSU_EXAMPLE_LIBS})
```

### 注意事项

1. 所有示例都使用 `readImageChineseSafe` 工具函数读取图像，以支持中文路径。这是 `Kit::readImage` 私有静态方法的公开实现版本。
2. 模型路径 `D:/models/anomaly_clip` 等为示例值，运行前需替换为实际模型路径。
3. 示例 7（UI 集成）使用 `#include "example_07_ui.moc"`，这是因为 `MainWindow` 类带 `Q_OBJECT` 宏且定义在 `.cpp` 文件中。CMake 的 `AUTOMOC` 会自动生成对应 moc 文件。
4. 异步示例（示例 3、示例 7）必须运行 Qt 事件循环（`app.exec()`），否则信号不会被处理。
5. 批量推理示例（示例 2）会一次性读取所有图像到内存，大批量场景应分批处理以避免内存溢出。
