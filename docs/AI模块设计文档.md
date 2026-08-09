# AI 模块设计文档

> 版本：v2.8.0 | 更新日期：2026-07-22

## 1. 架构概述

AI 模块是 QDetectVision 工业视觉检测系统的深度学习推理子系统，承担模型加载、推理执行、训练桥接与数据集管理职责。模块以静态库 `AI` 形式构建，对外暴露稳定的 C++ / Qt 接口，并通过适配器接入算子 SDK（`IInferenceEngine`）。

### 1.1 模块定位

- **所属层级**：业务服务层（位于 `Core`、`OperatorSDK` 之上，`UI` / `TrainingInference` 之下）
- **构建产物**：`AI` 静态库（`src/AI/CMakeLists.txt`）
- **运行时依赖**：Qt6 Core / OpenCV / ONNX Runtime（可选）/ CUDA（可选）

### 1.2 子系统组成

| 子系统 | 核心类 | 职责 |
| --- | --- | --- |
| 推理引擎 | `InferenceEngine` | 多后端推理（OpenCV DNN / ONNX Runtime），支持分类、检测（YOLOv5/v8）、分割 |
| 模型管理 | `ModelManager` | 单例，LRU 缓存模型，manifest.json 维护模型注册表 |
| 训练桥接 | `TrainingBridge` | 通过 `QProcess` 调用 Python 训练脚本，解析进度并自动注册产物 |
| 视觉分类器 | `VisionClassifier` | 高层封装，面向业务流的简洁分类接口 |
| 推理结果缓存 | `InferenceCache` | 第二层缓存，按模型 + 输入指纹索引推理结果 |
| 图像缓存 | `ImageCache` | 第三层缓存，缓存解码后的 `cv::Mat` |
| 分类指标 | `ClassificationMetrics` | 准确率、精确率、召回率、F1、混淆矩阵 |
| 数据集校验 | `DatasetValidator` | 四维校验框架（配对 / 格式 / 兼容 / 分布） |
| 数据集导出 | `ExportManager` | YOLO / COCO 格式导出 |
| 接口适配 | `InferenceEngineAdapter` | 将 `InferenceEngine` 适配为 `IInferenceEngine` |

### 1.3 三层缓存架构

```
┌─────────────────────────────────────────────────────────────┐
│                 InferenceEngine                             │
│   第一层：QCache<QString, RecognitionResult>  m_inferenceCache │
│           容量 50，Key = imageId                             │
└─────────────────────────────────────────────────────────────┘
                          │ (可选注入)
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                 InferenceCache                              │
│   第二层：QCache<QString, QJsonObject>                       │
│           容量 50                                            │
│           Key = modelPath|mtime|inputSize|conf|imgFingerprint│
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                 ImageCache                                  │
│   第三层：QCache<QString, CacheEntry>                        │
│           容量 100                                           │
│           Key = filePath|lastModified                        │
└─────────────────────────────────────────────────────────────┘
```

## 2. 核心类与接口

### 2.1 InferenceEngine（推理引擎）

推理引擎是 AI 模块的核心，支持多后端、多任务类型推理。

```cpp
class InferenceEngine : public QObject {
    Q_OBJECT
public:
    // 推理后端枚举
    enum Backend {
        BackendOpenCVDNN = 0,     // OpenCV DNN（默认，纯 CPU）
        BackendONNXRuntime = 1   // ONNX Runtime（GPU 加速，需启用）
    };

    enum Precision { FP32 = 0, FP16 = 1, INT8 = 2 };
    enum InferenceMode { SingleMode = 0, BatchMode = 1, RealtimeMode = 2 };

    // 模型加载与卸载
    bool loadModel(const QString& modelPath,
                   const QSize& inputSize = QSize(224, 224),
                   const cv::Scalar& mean = cv::Scalar(0, 0, 0),
                   double scale = 1.0 / 255.0,
                   bool swapRB = true,
                   const cv::Scalar& std = cv::Scalar(1.0, 1.0, 1.0));
    bool unloadModel();

    // 分类推理
    bool infer(const cv::Mat& input, QJsonObject& result);
    RecognitionResult inferWithResult(const cv::Mat& input,
                                       const QString& imageId = QString());

    // v5.4.0 检测/分割推理
    bool detect(const cv::Mat& input, double confThreshold, double iouThreshold,
                QJsonObject& result);
    bool segment(const cv::Mat& input, cv::Mat& mask, cv::Mat& overlay,
                 QJsonObject& result);

    // 批量推理
    bool inferBatch(const QList<cv::Mat>& inputs, QList<QJsonObject>& results);

    // 预热（消除首次推理延迟）
    bool warmUp(int iterations = 3);

    // 状态查询
    bool isModelLoaded() const;
    InferenceMetrics lastMetrics() const;
    ErrorState lastError() const;

signals:
    void modelLoaded(bool success);
    void inferenceCompleted(bool success);
    void progressUpdated(int current, int total);
    void inferenceError(int errorCode, const QString& errorType,
                        const QString& message);
};
```

**关键设计要点**：

- ImageNet 标准归一化常量内建（`IMAGENET_MEAN_*` / `IMAGENET_STD_*`），训练与推理保持一致。
- 错误状态机 `ErrorState` 内置 `MAX_RETRIES = 3`，支持 `canRetry()` / `recordRetry()`。
- YOLO 后处理支持 YOLOv5（输出形状 `[1, anchors, 5+nc]`）和 YOLOv8（输出形状 `[1, 4+nc, anchors]`）两种格式。
- NMS（非极大值抑制）通过 `computeIoU()` + 阈值过滤实现。
- ONNX Runtime 会话以 `void*` 不透明指针持有（`m_ortSession` / `m_ortEnv` / `m_ortMemoryInfo`），避免头文件污染。

### 2.2 ModelManager（模型管理器）

单例模式管理多模型生命周期，LRU 淘汰策略保证内存占用受控。

```cpp
class ModelManager : public QObject {
    Q_OBJECT
public:
    static ModelManager* instance();
    static constexpr int DEFAULT_CACHE_SIZE = 3;
    static constexpr const char* DEFAULT_MODEL_ID = "default_model";
    static constexpr const char* DEFAULT_MODEL_NAME = "YOLO";

    // 模型加载
    bool loadModel(const QString& modelPath, const QString& modelId,
                   const QSize& inputSize = QSize(224, 224));
    bool unloadModel(const QString& modelId);
    void unloadAll();

    // 默认模型管理
    QString defaultModelDirectory() const;       // applicationDirPath()/models
    QStringList findDefaultModels() const;
    bool loadDefaultModel(const QString& modelId = DEFAULT_MODEL_ID);

    // 训练产物注册
    bool registerTrainedModel(const QString& onnxPath,
                              const QString& labelsPath,
                              const QString& modelName);

    // 自定义模型管理（P0 Task 2）
    bool addCustomModel(const QString& onnxPath, const QString& displayName,
                        const QString& labelsPath = QString());
    bool removeCustomModelTransactional(const QString& modelId,
                                         bool deleteRelatedData = false);
    bool verifyModelIntegrity(const QString& modelId);   // SHA256 校验

    // 模型类型与尺寸自动识别
    QString autoDetectModelType(const QString& modelPath);  // yolo / classification
    QSize autoDetectInputSize(const QString& modelPath);   // YOLO→640x640

    // 加载兼容性预检
    QPair<bool, QString> probeModelLoadability(const QString& modelPath) const;
    bool updateModelLoadability(const QString& modelId = QString());

    // manifest 维护
    bool loadManifest();
    bool saveManifest();
    QJsonArray manifestModels() const;

signals:
    void modelLoaded(const QString& modelId);
    void modelUnloaded(const QString& modelId);
    void modelEvicted(const QString& modelId);
    void modelRegistered(const QString& modelName, const QString& onnxPath);
    void modelsChanged();
};
```

**关键设计要点**：

- **LRU 淘汰**：`m_accessOrder` 维护访问顺序链表，超出 `cacheSize` 时驱逐最久未用的引擎。
- **manifest.json**：维护模型注册表，记录 SHA256 哈希、大小、loadable 状态、load_error 字段。
- **事务性删除**：将模型文件 rename 为 `.trash` 后缀，保证删除操作可追溯、可恢复。
- **目录同步**：`syncManifestWithDirectory()` 在 manifest 为空时扫描 `.onnx` 文件并自动注册。
- **预检兼容性**：避免将不兼容的 ONNX 算子加载到 OpenCV DNN 后端导致运行时崩溃。

### 2.3 TrainingBridge（训练桥接）

通过子进程方式调用 Python 训练脚本，隔离训练与推理运行时环境。

```cpp
class TrainingBridge : public QObject {
    Q_OBJECT
public:
    bool isTraining() const;

    // 训练参数与状态快照（用于项目保存/加载）
    TrainingParamsSnapshot paramsSnapshot() const;
    TrainingStateSnapshot stateSnapshot() const;
    void applySnapshot(const TrainingParamsSnapshot& params,
                       const TrainingStateSnapshot& state);

    void startTraining(const QString& dataManifestPath,
                       const QString& outputDir,
                       const QString& modelType = "resnet18",
                       int numEpochs = 10,
                       int batchSize = 8,
                       double learningRate = 0.001,
                       const QString& pythonPath = "python");
    void cancelTraining();

    static bool checkPythonEnvironment(const QString& pythonPath, QString& errorOut);
    static QString generateDatasetManifest(
        const QList<QPair<QString, QString>>& imageLabelPairs,
        const QStringList& allLabels,
        double validationSplit,
        const QString& outputPath);

signals:
    void trainingProgress(const QVariantMap& progress);
    void trainingCompleted(const QVariantMap& result);
    void trainingError(const QString& phase, const QString& message);
    void logOutput(const QString& message);
    void modelRegistered(const QString& modelId, const QString& onnxPath);
};
```

**关键设计要点**：

- **心跳机制**：`m_heartbeatTimer` 周期检查 `m_lastOutputTime`，长时间无输出判定为卡死。
- **训练产物自动注册**：训练完成后自动调用 `ModelManager::registerTrainedModel()` 注册 ONNX 与标签文件。
- **去重保护**：`m_completedEmitted` 防止 complete 信号被重复触发。
- **命名空间隔离**：置于 `QDV` 命名空间下，避免与其他模块同名类冲突。

### 2.4 VisionClassifier（视觉分类器）

业务层封装，简化分类调用流程。

```cpp
class VisionClassifier {
public:
    bool loadModel(const std::string& onnxPath,
                   const std::vector<std::string>& labels);
    std::vector<ClassificationResult> classify(
        const cv::Mat& image,
        const ClassifyParams& params = ClassifyParams());
    void warmup(int iterations = 3);
    bool isLoaded() const;
    const std::vector<std::string>& getLabels() const;
};
```

**默认参数**：

```cpp
struct ClassifyParams {
    int inputWidth = 224;
    int inputHeight = 224;
    cv::Scalar mean = cv::Scalar(0.485, 0.456, 0.406);   // ImageNet
    double std = 1.0 / 255.0;
    bool swapRB = true;
    int topK = 5;
};
```

### 2.5 InferenceEngineAdapter（接口适配器）

将 `InferenceEngine` 适配为算子 SDK 接口 `IInferenceEngine`，打破 Vision → AI 反向依赖。

```cpp
class InferenceEngineAdapter : public IInferenceEngine {
public:
    explicit InferenceEngineAdapter(InferenceEngine* engine);  // 不拥有所有权
    bool loadModel(...) override;
    bool warmUp(int iterations = 3) override;
    bool infer(const cv::Mat& input, QJsonObject& result) override;
    bool detect(...) override;
    bool segment(...) override;
    void setCategoryLabels(const QStringList& labels) override;
    InferenceMetricsLite lastMetrics() const override;
    QString backend() const override;   // "OpenCV_DNN" / "ONNXRuntime"
};
```

**关键设计要点**：

- 引擎所有权归外部注入方，适配器不 `delete`。
- 当 `m_engine == nullptr`（RT-006 引擎未注入场景）时所有方法安全降级返回 `false`。

### 2.6 DatasetValidator（数据集四维校验）

```cpp
class DatasetValidator : public QObject {
    Q_OBJECT
public:
    ValidationReport validate(const ValidationRequest& request);
};
```

**四维校验框架**：

| 维度 | 校验内容 | 严重级别 |
| --- | --- | --- |
| Pairing（配对） | 文件存在 / 无标注 / 类别引用 | Critical |
| Format（格式） | 尺寸 / bbox 越界 / 面积 / 占比 / polygon 顶点 | Error |
| Compatibility（兼容） | 模型存在 / 尺寸匹配 / 类别数匹配 | Error |
| Distribution（分布） | 类别无样本 / 样本<10 / 不平衡 / 尺寸变异 | Warning |

**健康评分**：满分 100，Critical -30 / Error -10 / Warning -2。
**门禁**：`isHealthy()` 无 Critical/Error；`canTrain()` 无 Critical 且 Error ≤ 5。

### 2.7 ExportManager（数据集导出）

```cpp
class ExportManager : public QObject {
    Q_OBJECT
public:
    static ExportManager* instance();
    ExportResult exportYOLO(const QList<ExportImageEntry>& entries,
                             const QStringList& classNames,
                             const QString& outputDir,
                             double validationSplit = 0.15);
    ExportResult exportCOCO(const QList<ExportImageEntry>& entries,
                             const QStringList& classNames,
                             const QString& outputDir,
                             double validationSplit = 0.15);
};
```

**支持格式**：

- **YOLO**：`images/{train,val}` + `labels/{train,val}` + `data.yaml`
- **COCO**：`images/` + `annotations.json`（含 bbox/polygon）

**划分策略**：85:15 等距轮转采样，保证训练/验证集类别分布一致。

## 3. 数据流

### 3.1 分类推理数据流

```
[图像输入 cv::Mat]
      │
      ▼
[ImageCache 第三层]──miss──►[QFile+cv::imdecode 解码]
      │ hit                          │
      ▼                              │
[preprocess 预处理]◄─────────────────┘
  - resize 到 inputSize
  - 归一化 (mean, scale, std)
  - swapRB
      │
      ▼
[InferenceCache 第二层]──miss──►[后端推理]
      │ hit                          │
      ▼                             │
  ┌───┴────┐                        │
  │OpenCV  │◄───────────────────────┘
  │DNN     │
  │or ORT  │
  └───┬────┘
      │
      ▼
[postprocess 后处理]
  - 分类: extractTopK(softmax, k)
  - 检测: postprocessYoloV8 / postprocessYoloV5 + NMS
  - 分割: postprocessSegment + 彩色叠加
      │
      ▼
[InferenceEngine 第一层缓存]
      │
      ▼
[RecognitionResult / QJsonObject 输出]
```

### 3.2 训练数据流

```
[用户配置训练参数]
      │
      ▼
[TrainingBridge::generateDatasetManifest]
      │  生成数据集清单 (imageLabelPairs → JSON)
      ▼
[TrainingBridge::startTraining]
      │
      ▼
[QProcess 启动 Python 训练脚本]
      │
      ├──► stdout 解析 ──► trainingProgress 信号
      │
      ├──► stderr 解析 ──► trainingError 信号
      │
      └──► 心跳定时器 ──► 卡死检测
      │
      ▼
[训练完成 onProcessFinished]
      │
      ▼
[ModelManager::registerTrainedModel]
  - 复制 onnx 和 labels 到 models/
  - 更新 manifest.json
  - 处理命名冲突（时间戳后缀）
      │
      ▼
[modelRegistered 信号]
```

### 3.3 数据集校验数据流

```
[ValidationRequest]
  - datasetPath / imagePaths / labelPaths
  - expectedLabels / expectedInputSize / expectedNumClasses
  - modelPath
      │
      ▼
[validatePairing]    ─► 文件存在性、标注完整性、类别引用
[validateFormat]     ─► 尺寸、bbox 越界、面积/占比、polygon 顶点
[validateCompatibility] ─► 模型存在、尺寸匹配、类别数匹配
[validateDistribution]  ─► 类别无样本、样本<10、不平衡、尺寸变异
      │
      ▼
[calculateScore]  ─► 100 - Critical*30 - Error*10 - Warning*2
      │
      ▼
[ValidationReport]
  - isHealthy() / canTrain() 门禁判定
  - toJson() 序列化
```

## 4. 配置项

### 4.1 CMake 配置

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `HAS_ONNX_RUNTIME` | `OFF` | 启用 ONNX Runtime 后端条件编译框架（实际启用需配合 `ONNXRUNTIME_ROOT_DIR`） |
| `ONNXRUNTIME_ROOT_DIR` | `D:/onnxruntime-gpu` | ONNX Runtime 安装根目录（遵循非必要不放 C 盘原则） |
| `ENABLE_GPU` | `OFF` | 启用 CUDA 支持（链接 `CUDA_LIBRARIES`） |

### 4.2 运行时配置

- **模型默认目录**：`QCoreApplication::applicationDirPath()/models`
- **manifest.json 路径**：`<models 目录>/manifest.json`
- **缓存容量**：
  - 模型 LRU 缓存：`DEFAULT_CACHE_SIZE = 3`
  - 推理结果缓存（第一层）：50 条
  - 推理结果缓存（第二层 `InferenceCache`）：50 条
  - 图像缓存（`ImageCache`）：100 条
- **YOLO 阈值默认值**：
  - 置信度阈值：`m_confThreshold = 0.25`
  - NMS IoU 阈值：`m_iouThreshold = 0.45`
- **错误重试**：`ErrorState::MAX_RETRIES = 3`
- **训练参数默认**：`resnet18`、`numEpochs=20`、`batchSize=8`、`lr=0.001`、`valSplit=0.2`

## 5. 依赖关系

### 5.1 内部依赖

```
AI ─► Core
   ─► Monitoring
   ─► OperatorSDK（IInferenceEngine 接口）
```

### 5.2 第三方依赖

| 依赖 | 版本 | 用途 |
| --- | --- | --- |
| Qt6 Core | 6.x | QObject、QJsonObject、QCache、QProcess |
| OpenCV | 4.13.0（MinGW 编译） | `cv::dnn::Net`、`cv::imdecode`、图像处理 |
| ONNX Runtime | GPU 版本 | 可选后端，位于 `D:/onnxruntime-gpu` |
| CUDA | 可选 | GPU 加速（`ENABLE_GPU=ON` 时链接） |

### 5.3 ONNX Runtime 集成说明

- **头文件**：`onnxruntime_cxx_api.h`
- **库文件**：`onnxruntime.lib`（链接）/ `onnxruntime.dll`（运行时）
- **查找逻辑**：在根 CMakeLists.txt 中检测 `D:/onnxruntime-gpu/include/onnxruntime_cxx_api.h` 与 `lib/onnxruntime.lib` 是否同时存在。
- **DLL 部署**：`apps/SmartVision/CMakeLists.txt` 自动将 `onnxruntime.dll` 复制到运行目录；若缺失则推理会在加载模型时失败。
- **ZeroShotKit 集成**：第三方零样本推理库 `third_party/ZeroShotKit` 通过 `ZEROSHOTKIT_ENABLE_ORT` 控制是否启用 ONNX Runtime。
- **当前状态**：`HAS_ONNX_RUNTIME` 选项当前仅建立条件编译框架，实际启用通过 ZeroShotKit 的 ORT 集成完成。

## 6. 安全考虑

### 6.1 模型完整性校验

- `verifyModelIntegrity()` 流式计算 SHA256 并与 manifest 记录的哈希/大小比对，防止模型文件被篡改。
- 训练产物注册时同步写入 SHA256 到 manifest。

### 6.2 删除操作事务性

- `removeCustomModelTransactional()` 采用 rename-to-`.trash` 策略，避免直接删除导致数据丢失。
- `deleteRelatedData` 参数控制是否同时清理关联数据（labels、训练记录、评估报告）。

### 6.3 子进程安全

- `TrainingBridge` 通过 `QProcess` 隔离 Python 训练脚本，避免训练崩溃影响主进程。
- 心跳定时器检测卡死，超时自动终止。

### 6.4 输入校验

- `DatasetValidator` 在训练前强制执行四维校验，门禁不通过则禁止训练。
- `probeModelLoadability()` 预检模型兼容性，避免运行时崩溃。

### 6.5 缓存键设计

- `InferenceCache::generateKey()` 包含 `modelMtime`，模型文件变更时自动失效缓存。
- `ImageCache` 通过 `lastModified` 时间戳保证缓存一致性。

## 7. 后续改进计划

### 7.1 已识别的待改进项

1. **ONNX Runtime 完整集成**：`HAS_ONNX_RUNTIME` 选项当前仅建立条件编译框架，实际 ORT 推理路径（`runONNXRuntime`）需要进一步与 ZeroShotKit 协同完善。
2. **FP16 / INT8 量化推理**：`Precision` 枚举已定义但未完全落地，需要后续在 ORT 后端实现量化推理路径。
3. **RealtimeMode 实时推理**：`InferenceMode::RealtimeMode` 已定义但未实现，需要后续支持流式图像输入。
4. **批量推理性能**：`inferBatch()` 当前实现为循环单张推理，可进一步利用 ORT 的批处理能力。
5. **缓存淘汰策略**：当前为 LRU，可考虑加入 LFU（最不常用）以适应不同访问模式。

### 7.2 建议的演进方向

- **多模型并发推理**：`ModelManager` 当前为单线程串行加载，可扩展为线程池并发推理。
- **模型版本管理**：manifest.json 已具备版本字段基础，可进一步完善版本回滚机制。
- **分布式推理**：长链路项目可考虑将推理服务化，通过网络接口暴露推理能力。
- **A/B 测试框架**：基于 `ModelManager` 的多模型管理能力，可实现新旧模型并行推理对比。
