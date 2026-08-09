# ZeroShotKit 模型说明

本文档介绍 ZeroShotKit 支持的零样本模型，包括每个模型的定位、输入输出格式、文件路径要求、注意事项、局限性，以及量化模型支持与文本提示词编写指南。

## 目录

- [支持的模型列表](#支持的模型列表)
- [AnomalyCLIP（零样本异常检测）](#anomalyclip零样本异常检测)
- [GroundingDINO（开集目标检测）](#groundingdino开集目标检测)
- [MobileSAM（轻量分割）](#mobilesam轻量分割)
- [PatchCore（正常样本建模）](#patchcore正常样本建模)
- [OpenCLIP（零样本分类，预留）](#openclip零样本分类预留)
- [模型文件目录结构示例](#模型文件目录结构示例)
- [量化模型支持](#量化模型支持)
- [文本提示词编写指南](#文本提示词编写指南)

---

## 支持的模型列表

ZeroShotKit 当前支持 5 种零样本模型，对应 `zsu::ZeroShotModelType` 枚举：

| 模型 | 枚举值 | 定位 | 状态 |
| --- | --- | --- | --- |
| AnomalyCLIP | `AnomalyCLIP` | 零样本异常检测（CLIP ViT-B/32） | 已实现 |
| GroundingDINO | `GroundingDINO` | 开集目标检测 | 已实现（mock 模型） |
| MobileSAM | `MobileSAM` | 轻量分割 | 已实现（mock 模型） |
| PatchCore | `PatchCore` | 正常样本建模异常检测 | 已实现 |
| OpenCLIP | `OpenCLIP` | 零样本分类 | 预留（未完全实现） |

---

## AnomalyCLIP（零样本异常检测）

### 模型定位与适用场景

AnomalyCLIP 是基于 CLIP ViT-B/32 视觉编码器的零样本异常检测模型。它通过文本提示词与图像特征的余弦相似度判定异常分数，无需训练即可对新产品进行异常检测。

适用场景：

- 工业产品外观缺陷检测（划痕、凹痕、污渍等）
- 无标注数据场景下的快速异常筛查
- 多品类混线生产场景（不同产品只需更换提示词）
- 与 PatchCore 互补：少量正常样本时用 AnomalyCLIP，样本充足时切换到 PatchCore

### 输入格式

| 项目 | 规格 |
| --- | --- |
| 图像尺寸 | 自动 resize 到 224x224 |
| 色彩空间 | BGR（内部自动转 RGB） |
| 归一化 | CLIP 标准均值/方差 |
| 支持格式 | JPG, PNG, BMP, TIFF, WEBP |
| 最小尺寸 | 32x32 |
| 最大尺寸 | 4096x4096 |
| 推荐尺寸 | 224x224 |
| 最大文件大小 | 50 MB |

### 输出格式

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `anomalyScore` | `double` | 异常分数 [0,1]，>0.5 倾向异常 |
| `category` | `QString` | 分类结果（"normal" / "anomaly"） |
| `confidence` | `double` | 分类置信度 |
| `anomalyMap` | `cv::Mat` | 像素级异常分数图（热力图） |
| `metrics` | `InferenceMetrics` | 性能指标 |

### 模型文件名与路径要求

- `loadModel` 的 `modelPath` 参数指向模型所在**目录**（不是文件本身）。
- 目录下应包含以下文件：

| 文件名 | 用途 | 必需 |
| --- | --- | --- |
| `clip_vision_vit_b32.onnx` | CLIP 视觉编码器 ONNX 模型 | 是 |
| `clip_text_embeddings.txt` | 预计算的文本嵌入 | 是 |
| `clip_vision_vit_b32_int8.onnx` | 量化版本（可选） | 否 |

示例路径：

```
D:/models/anomaly_clip/
├── clip_vision_vit_b32.onnx
├── clip_text_embeddings.txt
└── clip_vision_vit_b32_int8.onnx   (可选)
```

### 获取方式

<!-- TODO: 补充开源仓库链接 -->

- CLIP 官方仓库：<https://github.com/openai/CLIP>
- AnomalyCLIP 论文与仓库：待补充
- ONNX 转换脚本：待补充

### 注意事项

参考 `resources/models/model_notes.json` 中 `anomaly_clip` 配置：

1. **提示词前缀**：提示词必须以 `normal:` 或 `anomaly:` 开头，否则模型无法正确区分正常/异常。
2. **二分类**：模型输出为二分类（normal/anomaly），不支持多类别分类。如需多类别，请改用 GroundingDINO。
3. **分数范围**：异常分数范围 [0,1]，>0.5 表示更可能是异常。
4. **文本嵌入**：文本嵌入需预计算并存储在 `clip_text_embeddings.txt` 中，运行时不再计算。
5. **阈值调整**：异常阈值建议设为 0.5，可根据实际效果调整。
6. **提示词描述**：提示词描述越具体，检测越准确。

### 局限性

- 只能做正常/异常二分类，无法输出具体类别名。
- 文本提示词质量直接影响检测效果。
- 不支持中文路径直接读取（ZeroShotKit 内部已用 `QFile` + `imdecode` 规避）。
- 对细小缺陷的检测能力有限，建议配合 GroundingDINO 使用。

---

## GroundingDINO（开集目标检测）

### 模型定位与适用场景

GroundingDINO 是开集目标检测模型，支持通过文本提示词检测任意类别目标，输出边界框与置信度。与 AnomalyCLIP 的二分类不同，GroundingDINO 可以同时检测多个具体类别（如划痕、凹痕、污渍、药片、拉锁等）。

适用场景：

- 多类别缺陷检测（同时检测划痕、凹痕、污渍等多种缺陷）
- 产品部件检测（识别产品上的特定部件）
- 与 MobileSAM 配合：先检测目标位置，再分割精确掩码
- 与 AnomalyCLIP 互补：AnomalyCLIP 判断是否有异常，GroundingDINO 定位异常类别

### 输入格式

| 项目 | 规格 |
| --- | --- |
| 图像尺寸 | 自动 resize 到 224x224 |
| 色彩空间 | BGR（内部自动转 RGB） |
| 归一化 | ImageNet 均值/方差 |
| 支持格式 | JPG, PNG, BMP, TIFF, WEBP |

### 输出格式

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `detections` | `std::vector<Detection>` | 检测框列表 |
| `Detection::cx, cy, w, h` | `float` | 边界框（归一化坐标 [0,1]） |
| `Detection::confidence` | `float` | 检测置信度 |
| `Detection::classId` | `int` | 类别 ID |
| `Detection::className` | `QString` | 类别名称 |
| `metrics` | `InferenceMetrics` | 性能指标 |

### 模型文件名与路径要求

- `loadModel` 的 `modelPath` 参数指向模型文件**目录**。
- 目录下应包含以下文件：

| 文件名 | 用途 | 必需 |
| --- | --- | --- |
| `grounding_dino_tiny.onnx` | Grounding DINO Tiny ONNX 模型 | 是（真实模型） |
| `grounding_dino_tiny_int8.onnx` | 量化版本（可选） | 否 |

> 注意：当前 ZeroShotKit 内置 mock 模型实现，即使没有真实 ONNX 文件也能返回模拟检测框。生产环境需替换为真实模型。

### 获取方式

<!-- TODO: 补充开源仓库链接 -->

- GroundingDINO 官方仓库：<https://github.com/IDEA-Research/GroundingDINO>
- ONNX 转换脚本：待补充
- Tiny 模型权重：待补充

### 注意事项

参考 `resources/models/model_notes.json` 中 `grounding_dino` 配置：

1. **提示词格式**：提示词格式为英文类别名，用点号分隔（如 `scratch . dent . stain`）。
2. **检测阈值**：检测阈值默认 0.3，低于此值的检测框会被过滤。可通过 `setDetectionThreshold` 调整。
3. **NMS 去重**：NMS 去重默认 IoU 阈值 0.45，可配置。
4. **多检测框**：每张图可能输出多个检测框，经 NMS 去重后保留稳定结果。
5. **英文提示词**：提示词用英文效果更佳。
6. **稳定性推理**：开启多次推理取稳定值可提升结果一致性（通过 `StabilityConfig`）。

### 局限性

- 当前使用 mock 模型，检测框坐标与置信度为模拟值。真实模型需要 `grounding_dino_tiny.onnx` 文件。
- 类别标签按提示词顺序循环分配，实际模型应根据 scores 输出。
- 对小目标检测能力有限。
- 提示词语义需与目标视觉特征匹配，否则检测失败。

---

## MobileSAM（轻量分割）

### 模型定位与适用场景

MobileSAM 是轻量级分割模型，输出二值掩码，适用于目标分割场景。它是 SAM（Segment Anything Model）的轻量化版本，推理速度更快，适合边缘端部署。

适用场景：

- 目标分割（配合 GroundingDINO 使用：先检测再分割）
- 缺陷区域精确定位
- 异常区域面积计算（通过掩码前景像素比例）
- 与 AnomalyCLIP 热力图互补：AnomalyCLIP 提供像素级异常分数，MobileSAM 提供精确二值掩码

### 输入格式

| 项目 | 规格 |
| --- | --- |
| 图像尺寸 | 自动 resize 到 256x256 |
| 色彩空间 | BGR（内部自动转 RGB） |
| 归一化 | ImageNet 均值/方差 |
| 支持格式 | JPG, PNG, BMP, TIFF, WEBP |

### 输出格式

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `mask` | `cv::Mat` | 二值掩码（后处理 resize 回原图尺寸） |
| `anomalyScore` | `double` | 前景像素比例作为异常分数参考 |
| `metrics` | `InferenceMetrics` | 性能指标 |

### 模型文件名与路径要求

- `loadModel` 的 `modelPath` 参数指向模型文件**目录**。
- 目录下应包含以下文件：

| 文件名 | 用途 | 必需 |
| --- | --- | --- |
| `mobile_sam.onnx` | MobileSAM ONNX 模型 | 是（真实模型） |
| `mobile_sam_int8.onnx` | 量化版本（可选） | 否 |

> 注意：当前 ZeroShotKit 内置 mock 模型实现，即使没有真实 ONNX 文件也能返回模拟掩码。生产环境需替换为真实模型。

### 获取方式

<!-- TODO: 补充开源仓库链接 -->

- MobileSAM 官方仓库：<https://github.com/ChaoningZhang/MobileSAM>
- ONNX 转换脚本：待补充

### 注意事项

参考 `resources/models/model_notes.json` 中 `mobile_sam` 配置：

1. **无需提示词**：MobileSAM 不需要文本提示词。
2. **掩码尺寸**：输出为 256x256 的二值掩码，后处理 resize 回原图尺寸。
3. **异常分数**：掩码中前景像素比例作为异常分数参考。
4. **掩码阈值**：掩码阈值默认 0.5，可根据效果调整。
5. **配合使用**：适合与 GroundingDINO 配合使用，先检测再分割。

### 局限性

- 当前使用 mock 模型，掩码为模拟值。
- 仅输出二值掩码，不提供类别信息。
- 需要配合其他模型使用才能获得完整检测结果。
- 对透明/半透明目标分割效果有限。

---

## PatchCore（正常样本建模）

### 模型定位与适用场景

PatchCore 是基于正常样本特征建模的异常检测方法，需要先添加正常样本到 memory bank。它复用 CLIP 视觉编码器提取特征，通过比较测试样本与 memory bank 中最相似样本的差异判定异常。

适用场景：

- 有少量正常样本（10-20 张以上）的工业检测场景
- 与 AnomalyCLIP 渐进式切换：样本少时用 AnomalyCLIP，样本充足时切换到 PatchCore
- 需要精确异常分数的场景（PatchCore 的分数比 AnomalyCLIP 更稳定）
- 需要像素级异常定位的场景（`anomalyMap`）

### 输入格式

| 项目 | 规格 |
| --- | --- |
| 图像尺寸 | 自动 resize 到 224x224 |
| 色彩空间 | BGR（内部自动转 RGB） |
| 归一化 | CLIP 标准均值/方差 |
| 支持格式 | JPG, PNG, BMP, TIFF, WEBP |

### 输出格式

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `anomalyScore` | `double` | 异常分数 [0,1]，= 1 - 与 memory bank 最相似样本的归一化相似度 |
| `anomalyMap` | `cv::Mat` | 像素级异常分数图 |
| `metrics` | `InferenceMetrics` | 性能指标 |

### 模型文件名与路径要求

PatchCore 复用 AnomalyCLIP 的 CLIP 视觉编码器，因此模型路径与 AnomalyCLIP 一致：

- `loadModel` 的 `modelPath` 参数指向模型所在**目录**。
- 目录下应包含以下文件：

| 文件名 | 用途 | 必需 |
| --- | --- | --- |
| `clip_vision_vit_b32.onnx` | CLIP 视觉编码器 ONNX 模型（与 AnomalyCLIP 共用） | 是 |
| `clip_vision_vit_b32_int8.onnx` | 量化版本（可选） | 否 |

### 获取方式

与 AnomalyCLIP 相同，复用 CLIP ViT-B/32 视觉编码器。

### 注意事项

参考 `resources/models/model_notes.json` 中 `patch_core` 配置：

1. **样本数量要求**：使用前必须添加正常样本，至少达到渐进式切换阈值（默认 10 个）。建议添加 10-20 个样本。
2. **特征复用**：复用 CLIP 视觉编码器提取特征，无需额外模型文件。
3. **异常分数定义**：异常分数 = 1 - 与 memory bank 中最相似样本的归一化相似度。分数越高表示越异常。
4. **无需提示词**：PatchCore 无需文本提示词，完全依赖正常样本特征。
5. **样本多样性**：样本应覆盖正常产品的各种姿态与光照条件，以提高检测鲁棒性。
6. **渐进式切换阈值**：通过 `setProgressiveSwitchThreshold` 可调整，默认 10。当 memory bank 样本数超过此值时，PatchCore 切换为就绪状态。

### 局限性

- 需要足够多的正常样本才能获得稳定结果（至少 10 个）。
- Memory bank 越大，推理越慢（每次推理需与所有样本比较）。
- 只能做异常检测，无法输出具体类别。
- 对训练时未见过的异常类型检测能力取决于样本代表性。

---

## OpenCLIP（零样本分类，预留）

### 模型定位与适用场景

OpenCLIP 是零样本分类模型，通过文本提示词与图像特征的相似度进行分类。与 AnomalyCLIP 不同，OpenCLIP 支持多类别分类（不仅限于正常/异常二分类）。

> 注意：当前版本中 OpenCLIP 为预留接口，未完全实现。`ZeroShotEngine::inferOpenCLIP` 方法已声明但实现为占位。

适用场景（未来版本）：

- 多类别零样本分类（如将产品分类为不同型号）
- 图像检索（通过文本查询相似图像）
- 与 AnomalyCLIP 互补：AnomalyCLIP 做异常检测，OpenCLIP 做具体类别分类

### 输入格式（规划）

| 项目 | 规格 |
| --- | --- |
| 图像尺寸 | 自动 resize 到 224x224 |
| 色彩空间 | BGR（内部自动转 RGB） |
| 归一化 | CLIP 标准均值/方差 |
| 支持格式 | JPG, PNG, BMP, TIFF, WEBP |

### 输出格式（规划）

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `category` | `QString` | 分类结果（预测的类别名称） |
| `confidence` | `double` | 分类置信度 |
| `metrics` | `InferenceMetrics` | 性能指标 |

### 模型文件名与路径要求（规划）

| 文件名 | 用途 | 必需 |
| --- | --- | --- |
| `open_clip_vit_b32.onnx` | OpenCLIP 视觉编码器 ONNX 模型 | 是 |

### 获取方式

<!-- TODO: 补充开源仓库链接 -->

- OpenCLIP 官方仓库：<https://github.com/mlfoundations/open_clip>
- ONNX 转换脚本：待补充

### 注意事项

- 当前版本未实现，调用 `ZeroShotEngine::infer` 加载 `OpenCLIP` 模型会返回未实现错误。
- 文本提示词格式与 AnomalyCLIP 类似，但不强制 `normal:` / `anomaly:` 前缀。

### 局限性

- 未实现，无法使用。
- 实现后仅支持分类，不支持检测与分割。

---

## 模型文件目录结构示例

推荐将所有模型文件统一放置在 `D:/models/` 目录下，按模型类型分子目录：

```
D:/models/
├── anomaly_clip/                          # AnomalyCLIP 模型目录
│   ├── clip_vision_vit_b32.onnx           # CLIP 视觉编码器
│   ├── clip_text_embeddings.txt           # 预计算文本嵌入
│   └── clip_vision_vit_b32_int8.onnx      # 量化版本（可选）
│
├── grounding_dino/                        # GroundingDINO 模型目录
│   ├── grounding_dino_tiny.onnx           # Grounding DINO Tiny
│   └── grounding_dino_tiny_int8.onnx      # 量化版本（可选）
│
├── mobile_sam/                            # MobileSAM 模型目录
│   ├── mobile_sam.onnx                    # MobileSAM
│   └── mobile_sam_int8.onnx               # 量化版本（可选）
│
└── patch_core/                            # PatchCore 模型目录
    └── clip_vision_vit_b32.onnx           # 复用 AnomalyCLIP 的 CLIP 视觉编码器
```

### 路径使用说明

- `Kit::loadModel` 与 `ZeroShotEngine::loadModel` 的 `modelPath` 参数指向**目录**，不是文件本身。
- 加载 AnomalyCLIP 示例：

  ```cpp
  kit.loadModel(zsu::ZeroShotModelType::AnomalyCLIP, "D:/models/anomaly_clip");
  ```

- 加载 PatchCore 示例（复用 AnomalyCLIP 目录）：

  ```cpp
  kit.loadModel(zsu::ZeroShotModelType::PatchCore, "D:/models/anomaly_clip");
  ```

- 路径分隔符在 Windows 下推荐使用 `/`（Qt 与 ORT 均支持），避免 `\` 转义问题。

### 部署注意事项

- 模型文件较大（CLIP ViT-B/32 约 350MB，GroundingDINO Tiny 约 700MB），建议放在非系统盘（D 盘）以减少 C 盘占用。
- 运行时需确保模型文件可读权限。
- 中文路径可能导致 ORT 加载失败，建议模型路径不包含中文。
- 模型文件更新后需重新调用 `loadModel`，不会自动热加载。

---

## 量化模型支持

ZeroShotKit 支持 INT8 量化模型，通过文件名后缀约定自动识别。

### 命名约定

量化模型文件名在原始模型名后添加 `_int8` 后缀，保留 `.onnx` 扩展名：

| 原始模型 | 量化模型 |
| --- | --- |
| `clip_vision_vit_b32.onnx` | `clip_vision_vit_b32_int8.onnx` |
| `grounding_dino_tiny.onnx` | `grounding_dino_tiny_int8.onnx` |
| `mobile_sam.onnx` | `mobile_sam_int8.onnx` |

### 启用方式

通过 `ZeroShotEngine::setUseQuantizedModel` 启用：

```cpp
// 加载模型前启用量化
kit.engine()->setUseQuantizedModel(true);

// 加载模型（自动优先加载 _int8.onnx 版本）
kit.loadModel(zsu::ZeroShotModelType::AnomalyCLIP, "D:/models/anomaly_clip");

// 检查是否实际使用了量化版本
if (kit.engine()->lastLoadUsedQuantized()) {
    std::cout << "已加载量化模型" << std::endl;
} else {
    std::cout << "量化模型未找到，回退到原始模型" << std::endl;
}
```

### 路径推导规则

`ZeroShotEngine::resolveQuantizedPath` 方法按以下规则推导量化模型路径：

1. 原始路径：`xxx/clip_vision_vit_b32.onnx`
2. 量化路径：`xxx/clip_vision_vit_b32_int8.onnx`（在文件名主体与 `.onnx` 之间插入 `_int8`）

### 回退机制

`ZeroShotEngine::resolveModelPath` 方法按以下逻辑处理：

1. 若 `setUseQuantizedModel(true)` 且量化模型文件存在 -> 加载量化模型，`lastLoadUsedQuantized` 返回 `true`。
2. 若 `setUseQuantizedModel(true)` 但量化模型文件不存在 -> 回退到原始模型，`lastLoadUsedQuantized` 返回 `false`。
3. 若 `setUseQuantizedModel(false)` -> 直接加载原始模型，`lastLoadUsedQuantized` 返回 `false`。

### 显式配置

也可通过 `ORTSessionConfig` 显式指定量化模型路径：

```cpp
zsu::ORTSessionConfig cfg = kit.engine()->getORTEngine().ortConfig();
cfg.enableInt8Quantization = true;
cfg.quantizationModelPath = "D:/models/anomaly_clip/clip_vision_vit_b32_int8.onnx";
kit.engine()->getORTEngine().setORTConfig(cfg);
```

### 量化模型优缺点

| 维度 | 量化模型 | 原始模型 |
| --- | --- | --- |
| 文件大小 | 约 1/4 | 基准 |
| 内存占用 | 约 1/4 | 基准 |
| 推理速度 | 提升 20-40% | 基准 |
| 精度 | 略有下降（通常 <1%） | 基准 |
| 适用场景 | 边缘端、低延迟、高吞吐 | 高精度要求 |

### 量化模型生成

量化模型需通过 ONNX Runtime 量化工具生成，不在 ZeroShotKit 范围内。参考 ONNX Runtime 量化文档：

```python
# Python 量化脚本示例（需自行实现）
import onnxruntime.quantization as quant

quant.quantize_dynamic(
    model_input="clip_vision_vit_b32.onnx",
    model_output="clip_vision_vit_b32_int8.onnx",
    weight_type=quant.QuantType.QInt8,
)
```

---

## 文本提示词编写指南

不同模型对文本提示词的格式要求不同，编写质量直接影响检测效果。

### AnomalyCLIP 提示词

#### 格式要求

- 提示词必须以 `normal:` 或 `anomaly:` 前缀开头。
- 正常与异常提示词应成对出现，数量建议相等。
- 描述应具体，避免泛泛而谈。

#### 示例

```cpp
kit.setTextPrompts({
    "normal:a photo of a normal product",
    "anomaly:a photo of a damaged product"
});
```

#### 针对特定产品的提示词

```cpp
// 药片检测
kit.setTextPrompts({
    "normal:a photo of a normal pill",
    "anomaly:a photo of a damaged pill with cracks"
});

// 电路板检测
kit.setTextPrompts({
    "normal:a photo of a normal circuit board",
    "anomaly:a photo of a circuit board with solder defects"
});

// 织物检测
kit.setTextPrompts({
    "normal:a photo of normal fabric surface",
    "anomaly:a photo of fabric with stains or tears"
});
```

#### 编写技巧

1. **使用英文**：CLIP 模型对英文支持更好。
2. **具体描述**：描述越具体，检测越准确。如 "damaged pill with cracks" 优于 "damaged product"。
3. **包含场景**：添加场景描述（如 "a photo of"）有助于模型理解。
4. **避免歧义**：正常与异常描述应清晰对比，避免重叠。
5. **多个提示词**：可添加多组正常/异常提示词增强鲁棒性：

   ```cpp
   kit.setTextPrompts({
       "normal:a photo of a normal pill",
       "normal:a photo of an intact pill",
       "anomaly:a photo of a damaged pill",
       "anomaly:a photo of a pill with cracks"
   });
   ```

### GroundingDINO 提示词

#### 格式要求

- 提示词格式为英文类别名，用 ` . ` 分隔（点号前后有空格）。
- 单个字符串包含所有类别。
- 类别按顺序对应检测结果的 `classId`（从 0 开始）。

#### 示例

```cpp
// 单类别检测
kit.setTextPrompts({"scratch"});

// 多类别检测
kit.setTextPrompts({"scratch . dent . stain . pill . zipper"});

// 缺陷检测
kit.setTextPrompts({"crack . hole . rust . wear . deformation"});
```

#### 编写技巧

1. **英文类别名**：使用英文单词或短语。
2. **点号分隔**：类别之间用 ` . ` 分隔（点号前后各一个空格）。
3. **具体名词**：使用具体名词（如 `scratch`）而非抽象词（如 `defect`）。
4. **避免同义词**：避免同时使用同义词（如 `scratch` 与 `scrape`），以免混淆。
5. **类别顺序**：类别顺序决定 `classId`，建议按检测优先级排列。

### MobileSAM 提示词

MobileSAM 不需要文本提示词，调用 `setTextPrompts` 不会影响结果。

```cpp
// 无需设置提示词
// kit.setTextPrompts({});  // 可选，留空即可
```

### PatchCore 提示词

PatchCore 不需要文本提示词，完全依赖正常样本特征。

```cpp
// 无需设置提示词
// 添加正常样本即可
kit.addNormalSample(normalImage);
```

### OpenCLIP 提示词（预留）

OpenCLIP 的提示词格式与 AnomalyCLIP 类似，但不强制 `normal:` / `anomaly:` 前缀。规划中的格式：

```cpp
// 多类别分类（规划中）
kit.setTextPrompts({
    "a photo of a cat",
    "a photo of a dog",
    "a photo of a bird"
});
```
