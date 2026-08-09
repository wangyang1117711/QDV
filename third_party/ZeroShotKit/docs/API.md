# ZeroShotKit API 参考

本文档覆盖 ZeroShotKit 模块全部公共 API。所有 Core 层类型与类位于命名空间 `zsu`，UI 层类位于命名空间 `QDVMini`。

## 目录

- [命名空间 zsu](#命名空间-zsu)
- [数据类型定义](#数据类型定义)
- [Kit 门面类](#kit-门面类)
- [ZeroShotEngine 零样本引擎](#zeroshotengine-零样本引擎)
- [ORTInferenceEngine ORT 推理引擎](#ortinferenceengine-ort-推理引擎)
- [PipelineEngine 流水线引擎](#pipelineengine-流水线引擎)
- [EvaluationEngine 评估引擎](#evaluationengine-评估引擎)
- [BadCaseRecorder Bad Case 记录器](#badcaserecorder-bad-case-记录器)
- [ModelNotesManager 模型注意事项管理器](#modelnotesmanager-模型注意事项管理器)

---

## 命名空间 zsu

ZeroShotKit Core 层所有公共类型与类都位于 `zsu` 命名空间下，`zsu` 是 ZeroShotKit 的缩写。引用方式：

```cpp
#include "ZeroShotKit/ZeroShotKit.h"

zsu::Kit kit;
zsu::ZeroShotResult result;
```

UI 层组件位于 `QDVMini` 命名空间下，与主项目保持一致。

---

## 数据类型定义

以下类型定义在 `ZeroShotTypes.h` 中，是 ZeroShotKit 各组件共用的公共数据结构。

### 枚举 Backend

推理后端类型，用于 `ORTInferenceEngine` 选择底层执行后端。

| 枚举值 | 整数值 | 含义 |
| --- | --- | --- |
| `OpenCVDNN` | 0 | OpenCV DNN 后端，主项目原有后端，用于对比基准 |
| `ONNXRuntime` | 1 | ONNX Runtime 后端，ZeroShotKit 默认与推荐后端 |

### 枚举 ErrorState

推理过程中的错误状态，由 `ORTInferenceEngine::errorState()` 返回。

| 枚举值 | 整数值 | 含义 |
| --- | --- | --- |
| `NoError` | 0 | 无错误 |
| `ModelNotFound` | 1 | 模型文件未找到 |
| `ModelLoadFailed` | 2 | 模型加载失败（解析错误、版本不兼容等） |
| `ModelForwardTestFailed` | 3 | 模型前向测试失败（首次加载时的预热推理失败） |
| `PreprocessFailed` | 4 | 预处理失败（如图像为空、尺寸非法） |
| `InferenceFailed` | 5 | 推理执行失败（ORT Run 调用异常） |
| `PostprocessFailed` | 6 | 后处理失败（输出张量解析错误） |
| `UnexpectedError` | 7 | 未预期错误（其他未分类异常） |
| `FatalError` | 8 | 致命错误（无法恢复） |
| `Cancelled` | 9 | 推理被取消（通过 `cancelInference()` 触发） |
| `Timeout` | 10 | 推理超时（超过 `singleInferenceTimeoutMs`） |

### 枚举 ZeroShotModelType

零样本模型类型，用于 `Kit::loadModel` 与 `ZeroShotEngine::loadModel` 指定加载的模型。

| 枚举值 | 含义 |
| --- | --- |
| `AnomalyCLIP` | 基于 CLIP ViT-B/32 的零样本异常检测模型 |
| `GroundingDINO` | 开集目标检测模型，支持文本提示词检测任意类别 |
| `MobileSAM` | 轻量分割模型，输出二值掩码 |
| `OpenCLIP` | 零样本分类模型（预留，未完全实现） |
| `PatchCore` | 基于正常样本特征建模的异常检测方法 |
| `Unknown` | 未知模型类型（初始状态） |

### 枚举 ReviewStatus

人工复核状态，配合 `ZeroShotResultPanel` 复核模式使用。

| 枚举值 | 整数值 | 含义 |
| --- | --- | --- |
| `Pending` | 0 | 待复核 |
| `Confirmed` | 1 | 已确认（用户认可推理结果） |
| `Rejected` | 2 | 已拒绝（用户拒绝推理结果，记为 Bad Case） |

### 结构 InferenceMetrics

推理性能指标，嵌入在 `ZeroShotResult::metrics` 中。

| 字段 | 类型 | 默认值 | 含义 |
| --- | --- | --- | --- |
| `preprocessMs` | `qint64` | 0 | 预处理耗时（毫秒） |
| `inferenceMs` | `qint64` | 0 | 推理耗时（毫秒） |
| `postprocessMs` | `qint64` | 0 | 后处理耗时（毫秒） |
| `totalMs` | `qint64` | 0 | 总耗时（毫秒），通常等于前三项之和 |
| `backend` | `QString` | 空 | 使用的后端名称（如 "ONNXRuntime" / "OpenCVDNN"） |

### 结构 ModelInputSpec

模型输入规格，由 `ORTInferenceEngine::inputSpec()` 返回，描述模型支持的输入格式约束。

| 字段 | 类型 | 默认值 | 含义 |
| --- | --- | --- | --- |
| `supportedFormats` | `QStringList` | `{JPG, PNG, BMP, TIFF, WEBP}` | 支持的图像格式 |
| `minWidth` | `int` | 32 | 最小宽度（像素） |
| `minHeight` | `int` | 32 | 最小高度（像素） |
| `maxWidth` | `int` | 4096 | 最大宽度（像素） |
| `maxHeight` | `int` | 4096 | 最大高度（像素） |
| `recommendedSize` | `QSize` | 224x224 | 推荐输入尺寸 |
| `maxFileSizeBytes` | `qint64` | 50 MB | 最大文件大小 |

### 结构 ZeroShotResult

零样本推理结果，所有模型统一返回此结构。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `success` | `bool` | 推理是否成功 |
| `errorMessage` | `QString` | 失败时的错误描述 |
| `metrics` | `InferenceMetrics` | 性能指标 |
| `category` | `QString` | 分类结果（AnomalyCLIP / OpenCLIP） |
| `confidence` | `double` | 分类置信度 |
| `anomalyScore` | `double` | 异常分数 [0,1]，>0.5 倾向异常 |
| `detections` | `std::vector<Detection>` | 检测框列表（Grounding DINO） |
| `mask` | `cv::Mat` | 二值掩码（MobileSAM） |
| `anomalyMap` | `cv::Mat` | 像素级异常分数图（AnomalyCLIP / PatchCore） |

内嵌结构 `ZeroShotResult::Detection`：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `cx`, `cy` | `float` | 边界框中心点（归一化坐标 [0,1]） |
| `w`, `h` | `float` | 边界框宽高（归一化坐标 [0,1]） |
| `confidence` | `float` | 检测置信度 |
| `classId` | `int` | 类别 ID |
| `className` | `QString` | 类别名称 |

成员方法：

- `QJsonObject toJson() const`：转为 JSON 对象，与主项目 `InferenceEngine` 结果格式兼容，字段包括 `status` / `category` / `confidence` / `anomaly_score` / `latency_ms` / `preprocess_ms` / `inference_ms` / `postprocess_ms` / `backend` / `detections` / `num_detections`。

### 结构 ORTSessionConfig

ONNX Runtime 会话配置，由 `ORTInferenceEngine::setORTConfig` 设置。

| 字段 | 类型 | 默认值 | 含义 |
| --- | --- | --- | --- |
| `intraOpNumThreads` | `int` | 4 | 线程内并行数 |
| `interOpNumThreads` | `int` | 2 | 线程间并行数 |
| `executionMode` | `QString` | "SEQUENTIAL" | 执行模式，可选 `SEQUENTIAL` / `PARALLEL` |
| `optimizationLevel` | `QString` | "ALL" | 优化级别，可选 `DISABLE` / `BASIC` / `EXTENDED` / `ALL` |
| `enableMemPattern` | `bool` | true | 内存模式优化 |
| `enableCpuMemArena` | `bool` | true | CPU 内存竞技场 |
| `enableInt8Quantization` | `bool` | false | 是否启用 INT8 量化 |
| `quantizationModelPath` | `QString` | 空 | 量化后的模型路径 |

### 结构 StabilityConfig

推理稳定性配置，由 `Kit::setStabilityConfig` / `ZeroShotEngine::setStabilityConfig` 设置。

| 字段 | 类型 | 默认值 | 含义 |
| --- | --- | --- | --- |
| `numRuns` | `int` | 1 | 多次推理次数，1=单次，>1=取稳定值 |
| `nmsIouThreshold` | `float` | 0.45f | NMS 的 IoU 阈值 |
| `confidenceThreshold` | `float` | 0.3f | 置信度过滤阈值 |
| `enableMultiRunStability` | `bool` | false | 是否启用多次推理取稳定值 |

### 结构 ModelNote

模型注意事项，由 `ModelNotesManager` 管理并暴露给 UI 展示。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `modelType` | `QString` | 模型类型名称（如 "anomaly_clip"） |
| `displayName` | `QString` | 用户可见名称（如 "AnomalyCLIP (零样本异常检测)"） |
| `description` | `QString` | 模型简述 |
| `notes` | `QStringList` | 注意事项列表 |
| `inputFormat` | `QStringList` | 输入格式要求 |
| `limitations` | `QStringList` | 限制条件 |
| `tips` | `QStringList` | 使用建议 |

### 结构 BadCaseRecord

Bad Case 记录条目，由 `BadCaseRecorder::record` 添加。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `imagePath` | `QString` | 图像路径 |
| `modelType` | `QString` | 模型类型 |
| `originalResult` | `ZeroShotResult` | 原始推理结果 |
| `correctedResult` | `ZeroShotResult` | 用户修正后的结果 |
| `userComment` | `QString` | 用户备注 |
| `timestamp` | `qint64` | 记录时间（Unix 毫秒） |

### 结构 PipelineStageConfig

流水线阶段配置，由 `PipelineEngine::setStageConfig` 设置。

| 字段 | 类型 | 默认值 | 含义 |
| --- | --- | --- | --- |
| `enableAnomalyDetect` | `bool` | true | 是否启用阶段 1 异常检测 |
| `enableObjectDetect` | `bool` | true | 是否启用阶段 2 目标检测 |
| `enableSegmentation` | `bool` | true | 是否启用阶段 3 分割 |
| `anomalyTriggerThreshold` | `float` | 0.5f | 异常分数超过此值才触发后续阶段 |
| `detectionTriggerThreshold` | `float` | 0.3f | 检测分数超过此值才触发分割 |

### 结构 PipelineResult

流水线推理结果，由 `PipelineEngine::infer` 返回。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `success` | `bool` | 流水线是否成功 |
| `errorMessage` | `QString` | 错误描述 |
| `anomalyResult` | `ZeroShotResult` | 阶段 1 异常检测结果 |
| `detectionResult` | `ZeroShotResult` | 阶段 2 目标检测结果 |
| `segmentResult` | `ZeroShotResult` | 阶段 3 分割结果 |
| `anomalyLatencyMs` | `qint64` | 阶段 1 耗时（毫秒） |
| `detectionLatencyMs` | `qint64` | 阶段 2 耗时（毫秒） |
| `segmentLatencyMs` | `qint64` | 阶段 3 耗时（毫秒） |
| `totalLatencyMs` | `qint64` | 总耗时（毫秒） |
| `anomalyExecuted` | `bool` | 阶段 1 是否实际执行 |
| `detectionExecuted` | `bool` | 阶段 2 是否实际执行 |
| `segmentExecuted` | `bool` | 阶段 3 是否实际执行 |
| `batchIndex` | `int` | 批量推理索引（单张推理为 -1） |

### 结构 PipelineStats

流水线统计信息，由 `PipelineEngine::stats` 返回。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `totalInferences` | `int` | 总推理次数 |
| `successfulInferences` | `int` | 成功推理次数 |
| `failedInferences` | `int` | 失败推理次数 |
| `totalLatencyMs` | `qint64` | 总延迟（毫秒） |
| `minLatencyMs` | `qint64` | 最小延迟 |
| `maxLatencyMs` | `qint64` | 最大延迟 |
| `avgLatencyMs` | `double` | 平均延迟 |
| `anomalyExecutions` | `int` | 阶段 1 执行次数 |
| `detectionExecutions` | `int` | 阶段 2 执行次数 |
| `segmentExecutions` | `int` | 阶段 3 执行次数 |

成员方法：

- `void reset()`：重置所有统计字段为初始值。
- `void update(qint64 latency, bool success)`：更新一次推理的统计。

### 结构 EvalSample

评估测试样本，由 `EvaluationEngine::addSample` 添加。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `imagePath` | `QString` | 图像路径 |
| `image` | `cv::Mat` | 图像数据 |
| `isAnomaly` | `bool` | 是否为异常样本（ground truth） |
| `anomalyScore` | `float` | 真实异常分数（如果有） |
| `category` | `QString` | 真实类别 |
| `predictedCategory` | `QString` | 预测类别 |
| `predictedScore` | `float` | 预测异常分数 |
| `latencyMs` | `qint64` | 推理延迟 |
| `correct` | `bool` | 预测是否正确 |

### 结构 EvalMetrics

评估指标，由 `EvaluationEngine::evaluate` 返回。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `totalSamples` | `int` | 总样本数 |
| `correctPredictions` | `int` | 正确预测数 |
| `accuracy` | `double` | 准确率 |
| `truePositives` | `int` | 真阳性 |
| `falsePositives` | `int` | 假阳性 |
| `trueNegatives` | `int` | 真阴性 |
| `falseNegatives` | `int` | 假阴性 |
| `precision` | `double` | 精确率 |
| `recall` | `double` | 召回率 |
| `f1Score` | `double` | F1 分数 |
| `auroc` | `double` | ROC 曲线下面积 |
| `fpr` | `double` | 假阳性率 |
| `tpr` | `double` | 真阳性率 |
| `totalLatencyMs` | `qint64` | 总延迟 |
| `avgLatencyMs` | `double` | 平均延迟 |
| `minLatencyMs` | `qint64` | 最小延迟 |
| `maxLatencyMs` | `qint64` | 最大延迟 |
| `p50LatencyMs` | `double` | P50 延迟 |
| `p99LatencyMs` | `double` | P99 延迟 |
| `throughputFPS` | `double` | 吞吐量（帧/秒） |

成员方法：

- `QJsonObject toJson() const`：转为 JSON 对象。

---

## Kit 门面类

`zsu::Kit` 是 ZeroShotKit 的门面类，封装零样本功能的完整调用流程。继承自 `QObject`，内部聚合 `ZeroShotEngine` / `PipelineEngine` / `EvaluationEngine` / `BadCaseRecorder` / `ModelNotesManager` 五个子组件。

头文件：`include/ZeroShotKit/ZeroShotKit.h`

### 构造与析构

```cpp
explicit Kit(QObject* parent = nullptr);
~Kit();
```

- `parent`：父 QObject，传入后子组件会随父对象自动释放。
- 构造时自动创建五个子组件并绑定流水线引擎到零样本引擎（三个阶段共用同一引擎实例）。

示例：

```cpp
zsu::Kit kit;       // 独立持有
zsu::Kit kit(this); // 由父对象管理生命周期
```

### 模型管理

#### loadModel

```cpp
bool loadModel(ZeroShotModelType type, const QString& modelPath);
```

加载指定类型的零样本模型。

- `type`：模型类型，取 `ZeroShotModelType` 枚举值。
- `modelPath`：模型路径。对于 AnomalyCLIP，指向 `clip_vision_vit_b32.onnx` 所在目录。
- 返回：加载成功返回 `true`，失败返回 `false`。

示例：

```cpp
if (!kit.loadModel(zsu::ZeroShotModelType::AnomalyCLIP, "D:/models/anomaly_clip")) {
    qWarning() << "模型加载失败";
}
```

注意事项：

- 同一时刻只能加载一种模型，重复调用会卸载前一个模型。
- 加载大模型时首次前向测试可能耗时 30 秒以上，可由底层 `ZeroShotEngine` 控制。

#### isModelLoaded

```cpp
bool isModelLoaded() const;
```

返回当前是否已加载模型。

#### modelType

```cpp
ZeroShotModelType modelType() const;
```

返回当前加载的模型类型，未加载时返回 `ZeroShotModelType::Unknown`。

### 配置

#### setTextPrompts / textPrompts

```cpp
void setTextPrompts(const QStringList& prompts);
QStringList textPrompts() const;
```

设置/获取文本提示词。

- AnomalyCLIP：使用 `normal:xxx` / `anomaly:xxx` 前缀格式。
- Grounding DINO：使用 `class1 . class2 . class3` 点号分隔格式。

示例：

```cpp
kit.setTextPrompts({"normal:a photo of a normal product",
                    "anomaly:a photo of a damaged product"});
```

#### setAnomalyThreshold / anomalyThreshold

```cpp
void setAnomalyThreshold(float threshold);
float anomalyThreshold() const;
```

设置/获取异常分数阈值，默认 0.5。`anomalyScore > threshold` 时判定为异常。

#### setDetectionThreshold / detectionThreshold

```cpp
void setDetectionThreshold(float threshold);
float detectionThreshold() const;
```

设置/获取目标检测阈值，默认 0.3。低于此值的检测框会被过滤。

#### setStabilityConfig / stabilityConfig

```cpp
void setStabilityConfig(const StabilityConfig& config);
StabilityConfig stabilityConfig() const;
```

设置/获取推理稳定性配置。当 `enableMultiRunStability` 为 `true` 时，`Kit::infer` 会自动调用 `ZeroShotEngine::inferStable` 进行多次推理取稳定值。

示例：

```cpp
zsu::StabilityConfig cfg;
cfg.enableMultiRunStability = true;
cfg.numRuns = 3;
cfg.nmsIouThreshold = 0.45f;
kit.setStabilityConfig(cfg);
```

### 同步推理

#### infer

```cpp
ZeroShotResult infer(const cv::Mat& image);
```

对单张图像进行同步推理。

- `image`：输入图像（BGR 格式，与 OpenCV 默认一致）。
- 返回：`ZeroShotResult`，包含异常分数、检测框、掩码等结果。
- 注意：若 `stabilityConfig().enableMultiRunStability` 为 `true`，内部会调用 `inferStable`。

#### inferBatch

```cpp
QList<ZeroShotResult> inferBatch(const QList<cv::Mat>& images);
```

对多张图像进行同步批量推理，内部循环调用 `infer`。

- 支持通过 `cancel()` 中断。
- 返回：与输入图像一一对应的结果列表。

### 异步推理

#### inferAsync

```cpp
void inferAsync(const QString& imagePath);
```

异步对单张图像推理。内部使用 `QtConcurrent::run` 在工作线程执行，完成后通过 `inferenceCompleted` 信号通知。

- `imagePath`：图像文件路径，支持中文路径（内部使用 `QFile` + `cv::imdecode` 读取）。
- 读取失败时通过 `errorOccurred` 信号通知。

#### inferBatchAsync

```cpp
void inferBatchAsync(const QStringList& imagePaths);
```

异步批量推理。每完成一张图像通过 `progressUpdated` 信号报告进度，全部完成后通过 `batchCompleted` 信号返回结果列表。

#### cancel

```cpp
void cancel();
```

取消正在进行的异步推理。设置取消标志位，工作线程在下次循环检查时退出。

#### isRunning

```cpp
bool isRunning() const;
```

返回是否正在执行异步推理。

### PatchCore 正常样本管理

#### addNormalSample

```cpp
bool addNormalSample(const cv::Mat& image);
```

添加一个正常样本到 PatchCore memory bank。需要先加载 PatchCore 模型。

#### removeLastNormalSample

```cpp
bool removeLastNormalSample();
```

移除最后添加的正常样本。返回是否移除成功（memory bank 为空时返回 `false`）。

#### clearNormalSamples

```cpp
void clearNormalSamples();
```

清空 memory bank。

#### normalSampleCount

```cpp
int normalSampleCount() const;
```

返回 memory bank 中当前样本数。当样本数达到 `progressiveSwitchThreshold`（默认 10）时，PatchCore 切换为就绪状态。

### 辅助组件访问

```cpp
ZeroShotEngine* engine() const;
PipelineEngine* pipeline() const;
EvaluationEngine* evaluator() const;
BadCaseRecorder* badCaseRecorder() const;
ModelNotesManager* notesManager() const;
```

返回内部各子组件的指针，用于直接调用子组件的高级 API。返回的指针所有权归 `Kit`，调用方不应释放。

示例：

```cpp
// 直接调用 ZeroShotEngine 的量化开关
kit.engine()->setUseQuantizedModel(true);

// 直接调用 PipelineEngine 的流水线推理
zsu::PipelineResult pr = kit.pipeline()->infer(image);

// 直接使用 EvaluationEngine 评估
kit.evaluator()->addSamples(samples);
zsu::EvalMetrics m = kit.evaluator()->evaluate(kit.engine(), 0.5f);
```

### 信号

#### inferenceCompleted

```cpp
void inferenceCompleted(const zsu::ZeroShotResult& result);
```

`inferAsync` 完成时发射，携带单张推理结果。信号在主线程发射（通过 `QFutureWatcher::finished` 转发）。

#### batchCompleted

```cpp
void batchCompleted(const QList<zsu::ZeroShotResult>& results);
```

`inferBatchAsync` 完成时发射，携带批量推理结果列表。

#### progressUpdated

```cpp
void progressUpdated(int current, int total);
```

`inferBatchAsync` 执行过程中每完成一张图像时发射。

- `current`：当前已完成数量（从 1 开始）。
- `total`：总数量。

#### errorOccurred

```cpp
void errorOccurred(const QString& message);
```

异步推理过程中发生错误时发射，如无法读取图像。

---

## ZeroShotEngine 零样本引擎

`zsu::ZeroShotEngine` 统一调度 AnomalyCLIP / Grounding DINO / MobileSAM / OpenCLIP / PatchCore 五种模型。继承自 `QObject`。

头文件：`include/ZeroShotKit/ZeroShotEngine.h`

### 模型加载

```cpp
bool loadModel(ZeroShotModelType modelType, const QString& modelPath,
               const QSize& inputSize = QSize(224, 224));
```

- `modelType`：模型类型。
- `modelPath`：模型路径。对于 AnomalyCLIP，指向 `clip_vision_vit_b32.onnx` 所在目录。
- `inputSize`：模型输入尺寸，默认 224x224。MobileSAM 推荐 256x256。
- 返回：加载成功返回 `true`。

### 文本提示配置

```cpp
void setTextPrompts(const QStringList& prompts);
QStringList textPrompts() const;
```

设置/获取文本提示词。格式与 `Kit::setTextPrompts` 一致。

### 阈值配置

```cpp
void setAnomalyThreshold(float t);
float anomalyThreshold() const;
void setDetectionThreshold(float t);
float detectionThreshold() const;
```

异常阈值默认 0.5，检测阈值默认 0.3。

### 量化模型支持

```cpp
void setUseQuantizedModel(bool enable);
bool useQuantizedModel() const;
bool lastLoadUsedQuantized() const;
```

- `setUseQuantizedModel`：启用后，`loadModel` 会优先加载 `_int8.onnx` 量化版本。
- `lastLoadUsedQuantized`：返回上一次模型加载是否实际使用了量化版本（即使启用了量化，若量化模型文件不存在会回退到原始模型）。

### PatchCore 正常样本建模

```cpp
bool addNormalSample(const cv::Mat& image);
bool removeLastNormalSample();
void clearNormalSamples();
int normalSampleCount() const;
void setProgressiveSwitchThreshold(int n);
int progressiveSwitchThreshold() const;
```

- `addNormalSample`：添加正常样本到 memory bank，复用 CLIP 视觉编码器提取特征。
- `setProgressiveSwitchThreshold`：设置渐进式切换阈值，默认 10。当 memory bank 样本数超过此值时，PatchCore 切换为就绪状态。

### 推理

#### infer

```cpp
ZeroShotResult infer(const cv::Mat& input);
```

对单张图像进行推理，根据当前加载的模型类型分派到对应的子方法（`inferAnomalyCLIP` / `inferGroundingDINO` / `inferMobileSAM` / `inferOpenCLIP` / `inferPatchCore`）。

#### inferStable

```cpp
ZeroShotResult inferStable(const cv::Mat& input);
```

稳定性推理。当 `StabilityConfig::enableMultiRunStability` 为 `true` 时，对同一张图推理多次，取检测框的交集/众数，保证结果稳定。

### 稳定性配置

```cpp
void setStabilityConfig(const StabilityConfig& config);
StabilityConfig stabilityConfig() const;
```

### NMS 去重（静态方法）

```cpp
static std::vector<ZeroShotResult::Detection> applyNMS(
    const std::vector<ZeroShotResult::Detection>& detections,
    float iouThreshold);
```

对检测结果做非极大值抑制，去除 IoU > `iouThreshold` 的重叠框。静态方法，可独立调用。

### 模型状态

```cpp
bool isModelLoaded() const;
ZeroShotModelType modelType() const;
int textEmbeddingCount() const;
```

- `textEmbeddingCount`：返回预计算的文本嵌入数量，调试用。

### 信号

```cpp
void inferenceCompleted(const ZeroShotResult& result);
void stageProgress(const QString& stage, qint64 elapsedMs);
```

- `stageProgress`：流水线阶段进度，携带阶段名称与耗时。

---

## ORTInferenceEngine ORT 推理引擎

`zsu::ORTInferenceEngine` 封装 ONNX Runtime C++ API，提供模型加载、预处理、推理、后处理完整链路。继承自 `QObject`。

头文件：`include/ZeroShotKit/ORTInferenceEngine.h`

### 后端配置

```cpp
void setBackend(Backend backend);
Backend backend() const;
static QStringList availableBackends();
```

- `setBackend`：设置推理后端，可选 `Backend::OpenCVDNN` 或 `Backend::ONNXRuntime`。
- `availableBackends`：返回可用后端列表。

### 类别标签

```cpp
void setClassLabels(const QStringList& labels);
QStringList classLabels() const;
```

设置/获取类别标签，用于 YOLO 后处理时将 classId 映射到 className。

### YOLO 后处理阈值

```cpp
void setConfThreshold(float t);
float confThreshold() const;
void setIoUThreshold(float t);
float ioUThreshold() const;
```

- `confThreshold`：置信度阈值，默认 0.25。
- `ioUThreshold`：NMS IoU 阈值，默认 0.45。

### ORT 会话配置

```cpp
void setORTConfig(const ORTSessionConfig& config);
ORTSessionConfig ortConfig() const;
```

设置/获取 ONNX Runtime 会话配置（线程数、优化级别等）。

### 模型加载

```cpp
bool loadModel(const QString& modelPath,
               const QSize& inputSize = QSize(224, 224),
               const cv::Scalar& mean = cv::Scalar(0, 0, 0),
               double scale = 1.0 / 255.0,
               bool swapRB = true,
               bool skipForwardTest = false);
bool unloadModel();
```

- `modelPath`：ONNX 模型文件路径。
- `inputSize`：模型输入尺寸。
- `mean`：预处理均值，默认 0。
- `scale`：预处理缩放因子，默认 1/255。
- `swapRB`：是否交换 R/B 通道，默认 true（OpenCV BGR → RGB）。
- `skipForwardTest`：是否跳过前向测试。大模型首次 forward 可能 30+ 秒，跳过可加速加载但失去错误早期发现。
- 返回：加载成功返回 `true`。

### 推理

#### infer（带原始输出）

```cpp
bool infer(const cv::Mat& input, cv::Mat& rawOutput, QJsonObject& result);
```

- `input`：输入图像。
- `rawOutput`：输出原始张量数据。
- `result`：输出后处理后的 JSON 结果。
- 返回：推理成功返回 `true`。

#### infer（仅 JSON 结果）

```cpp
bool infer(const cv::Mat& input, QJsonObject& result);
```

简化版本，不返回原始张量。

#### inferBatch

```cpp
bool inferBatch(const QList<cv::Mat>& inputs, QList<QJsonObject>& results);
```

批量推理，内部循环调用 `infer`。

#### inferRaw

```cpp
bool inferRaw(const cv::Mat& preprocessedBlob, cv::Mat& rawOutput, QJsonObject& result);
```

跳过预处理的推理。输入 `preprocessedBlob` 应已是 NCHW blob，适用于 `ZeroShotEngine` 等自定义预处理场景。

### 状态查询

```cpp
bool isModelLoaded() const;
QString currentModelPath() const;
QSize modelInputSize() const;
InferenceMetrics lastMetrics() const;
ErrorState errorState() const;
QString lastError() const;
void resetError();
ModelInputSpec inputSpec() const;
```

- `lastMetrics`：返回上一次推理的性能指标。
- `errorState`：返回当前错误状态。
- `lastError`：返回最近一次错误的描述字符串。
- `resetError`：重置错误状态为 `NoError`。

### 预热

```cpp
bool warmUp(int iterations = 3);
```

模型预热，执行指定次数的空推理以初始化内存池、优化图等。首次推理往往较慢，预热后可获得稳定延迟。

### 取消与超时控制

```cpp
void cancelInference();
bool isCancelled() const;
void resetCancelFlag();
void setSingleInferenceTimeoutMs(qint64 ms);
qint64 singleInferenceTimeoutMs() const;
```

- `cancelInference`：设置取消标志位，线程安全。推理循环会检查此标志并提前返回。
- `setSingleInferenceTimeoutMs`：设置单次推理超时时间，默认 30000ms。

### 信号

```cpp
void modelLoaded(bool success);
void inferenceCompleted(bool success);
void progressUpdated(int current, int total);
void stageProgress(const QString& stageDesc, qint64 elapsedMs);
```

---

## PipelineEngine 流水线引擎

`zsu::PipelineEngine` 实现多阶段流水线推理（异常检测→目标检测→分割）。继承自 `QObject`。

头文件：`include/ZeroShotKit/PipelineEngine.h`

流水线阶段定义：

- Stage 1：AnomalyCLIP / PatchCore 异常检测
- Stage 2：Grounding DINO 目标检测（当异常分数 > `anomalyTriggerThreshold` 时触发）
- Stage 3：MobileSAM 分割（当检测到目标时触发）

### 引擎绑定

```cpp
void setAnomalyEngine(ZeroShotEngine* engine);
void setDetectionEngine(ZeroShotEngine* engine);
void setSegmentEngine(ZeroShotEngine* engine);
ZeroShotEngine* anomalyEngine() const;
ZeroShotEngine* detectionEngine() const;
ZeroShotEngine* segmentEngine() const;
```

绑定已加载模型的 `ZeroShotEngine`。`PipelineEngine` 不负责模型加载，三个阶段可绑定同一引擎实例（如 `Kit` 默认行为），也可绑定不同实例以支持并行。

### 阶段配置

```cpp
void setStageConfig(const PipelineStageConfig& config);
PipelineStageConfig stageConfig() const;
```

设置/获取流水线阶段配置，控制各阶段启用与触发阈值。

### 执行模式

```cpp
void setParallelMode(bool enable);
bool parallelMode() const;
```

- `setParallelMode(true)`：启用并行模式（使用 `QtConcurrent`）。
- 默认为串行模式。

### 同步推理

```cpp
PipelineResult infer(const cv::Mat& input);
QList<PipelineResult> inferBatch(const QList<cv::Mat>& inputs);
```

- `infer`：单张图像流水线推理。
- `inferBatch`：批量流水线推理。

### 异步推理

```cpp
void inferAsync(const cv::Mat& input);
void inferBatchAsync(const QList<cv::Mat>& inputs);
bool isRunning() const;
void cancel();
bool isCancelled() const;
void resetCancel();
```

异步推理使用 `QtConcurrent`，通过信号通知结果。

### 统计

```cpp
PipelineStats stats() const;
void resetStats();
```

返回/重置流水线统计信息，包括总推理次数、成功/失败次数、延迟分布、各阶段执行次数。

### 信号

```cpp
void inferenceCompleted(const PipelineResult& result);
void batchCompleted(const QList<PipelineResult>& results);
void stageCompleted(const QString& stageName, qint64 elapsedMs);
void progressUpdated(int current, int total);
```

- `stageCompleted`：单个阶段完成时发射，携带阶段名称与耗时。

---

## EvaluationEngine 评估引擎

`zsu::EvaluationEngine` 实现测试数据集加载、评估执行、指标计算、报告生成。继承自 `QObject`。

头文件：`include/ZeroShotKit/EvaluationEngine.h`

### 数据集管理

```cpp
void addSample(const EvalSample& sample);
void addSamples(const QList<EvalSample>& samples);
void clearSamples();
int sampleCount() const;
```

添加/清空测试样本。`addSamples` 用于批量添加。

### 评估执行

```cpp
EvalMetrics evaluate(ZeroShotEngine* engine, float threshold = 0.5f);
EvalMetrics evaluatePipeline(PipelineEngine* engine, float threshold = 0.5f);
EvalSample evaluateSingle(ZeroShotEngine* engine, const EvalSample& sample, float threshold);
```

- `evaluate`：使用 `ZeroShotEngine` 评估所有样本。
- `evaluatePipeline`：使用 `PipelineEngine` 评估所有样本。
- `evaluateSingle`：评估单个样本，返回带预测结果的 `EvalSample`。
- `threshold`：异常判定阈值。

### 结果访问

```cpp
EvalMetrics lastMetrics() const;
QList<EvalSample> evaluatedSamples() const;
```

- `lastMetrics`：返回上一次评估的指标。
- `evaluatedSamples`：返回评估后的样本列表（包含预测结果）。

### 报告生成

```cpp
QJsonObject generateJsonReport() const;
QString generateTextReport() const;
bool saveReport(const QString& filePath, bool jsonFormat = true) const;
```

- `generateJsonReport`：生成 JSON 格式报告。
- `generateTextReport`：生成文本格式报告。
- `saveReport`：保存报告到文件，`jsonFormat` 为 `true` 保存 JSON，否则保存文本。

### 辅助方法（静态）

```cpp
static QList<EvalSample> loadDatasetFromDirectory(const QString& dirPath);
static double calculateAUROC(const QList<EvalSample>& samples);
static double percentile(const QList<qint64>& values, double p);
```

- `loadDatasetFromDirectory`：从目录加载测试集。`dirPath` 下应有 `normal/` 与 `anomaly/` 两个子目录，分别存放正常与异常样本图像。
- `calculateAUROC`：计算 ROC 曲线下面积。
- `percentile`：计算百分位数。`p` 取 0-100，如 50 表示 P50，99 表示 P99。

### 信号

```cpp
void progressUpdated(int current, int total);
void evaluationCompleted(const EvalMetrics& metrics);
```

---

## BadCaseRecorder Bad Case 记录器

`zsu::BadCaseRecorder` 记录推理结果与用户修正结果的差异，用于模型优化回归测试。继承自 `QObject`。

头文件：`include/ZeroShotKit/BadCaseRecorder.h`

### API

```cpp
explicit BadCaseRecorder(QObject* parent = nullptr);

void record(const BadCaseRecord& entry);
const QList<BadCaseRecord>& records() const;
void clear();
bool exportToJson(const QString& filePath) const;
bool importFromJson(const QString& filePath);
int count() const;
```

- `record`：记录一条 Bad Case。
- `records`：返回所有记录的只读引用。
- `clear`：清空所有记录。
- `exportToJson`：导出为 JSON 文件。
- `importFromJson`：从 JSON 文件导入。
- `count`：返回记录数。

### 信号

```cpp
void recordAdded(const BadCaseRecord& entry);
void recordsCleared();
```

示例：

```cpp
zsu::BadCaseRecord rec;
rec.imagePath = "D:/test/badcase_001.jpg";
rec.modelType = "AnomalyCLIP";
rec.originalResult = originalResult;
rec.correctedResult = correctedResult;
rec.userComment = "用户备注：实际为正常样本，模型误判为异常";
rec.timestamp = QDateTime::currentMSecsSinceEpoch();

kit.badCaseRecorder()->record(rec);
kit.badCaseRecorder()->exportToJson("D:/badcases.json");
```

---

## ModelNotesManager 模型注意事项管理器

`zsu::ModelNotesManager` 从 JSON 文件加载各模型注意事项，支持默认文件 + 用户覆盖两层配置。继承自 `QObject`。

头文件：`include/ZeroShotKit/ModelNotesManager.h`

### API

```cpp
explicit ModelNotesManager(QObject* parent = nullptr);

bool loadDefaultNotes(const QString& defaultPath);
bool loadUserOverride(const QString& userPath);
bool saveUserOverride(const QString& userPath) const;

ModelNote getNote(ZeroShotModelType type) const;
void setNote(ZeroShotModelType type, const ModelNote& note);
QList<ModelNote> allNotes() const;

static QString modelTypeKey(ZeroShotModelType type);
static ZeroShotModelType keyToModelType(const QString& key);
```

- `loadDefaultNotes`：从默认路径（通常是 `resources/models/model_notes.json`）加载注意事项。
- `loadUserOverride`：从用户配置目录加载覆盖配置，会合并到默认配置上。
- `saveUserOverride`：保存用户修改到用户配置目录。
- `getNote`：获取指定模型类型的注意事项。
- `setNote`：设置/更新指定模型类型的注意事项（应随后调用 `saveUserOverride` 持久化）。
- `allNotes`：返回所有模型注意事项列表。
- `modelTypeKey` / `keyToModelType`：模型类型枚举与字符串 key 互转。例如 `AnomalyCLIP` ↔ `"anomaly_clip"`。

示例：

```cpp
kit.notesManager()->loadDefaultNotes("D:/ZeroShotKit/resources/models/model_notes.json");

zsu::ModelNote note = kit.notesManager()->getNote(zsu::ZeroShotModelType::AnomalyCLIP);
qDebug() << note.displayName;
qDebug() << "注意事项:" << note.notes;
qDebug() << "输入格式:" << note.inputFormat;
qDebug() << "限制条件:" << note.limitations;
qDebug() << "使用建议:" << note.tips;
```

JSON 配置格式（`model_notes.json`）：

```json
{
    "anomaly_clip": {
        "display_name": "AnomalyCLIP (零样本异常检测)",
        "description": "基于 CLIP ViT-B/32 的零样本异常检测模型...",
        "notes": ["提示词必须以 'normal:' 或 'anomaly:' 开头", ...],
        "input_format": ["图像尺寸：自动 resize 到 224x224", ...],
        "limitations": ["只能做正常/异常二分类", ...],
        "tips": ["若需区分多个类别，请改用 Grounding DINO", ...]
    },
    "grounding_dino": { ... },
    "mobile_sam": { ... },
    "patch_core": { ... }
}
```

各模型 key 与 `ZeroShotModelType` 的对应：

| 模型类型 | JSON key |
| --- | --- |
| `AnomalyCLIP` | `anomaly_clip` |
| `GroundingDINO` | `grounding_dino` |
| `MobileSAM` | `mobile_sam` |
| `OpenCLIP` | `open_clip` |
| `PatchCore` | `patch_core` |
