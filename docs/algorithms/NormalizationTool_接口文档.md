# NormalizationTool（均一化算子）接口文档

**版本**: 1.0 · 适用于 Q-DetectVision v2.6.0+  
**头文件**: `include/Vision/NormalizationTool.h`  
**源文件**: `src/Vision/NormalizationTool.cpp`

---

## 1. 功能概述

`NormalizationTool` 是深度学习类别中的预处理算子，用于对输入图像执行数值标准化/归一化，使数据分布与下游模型训练时的分布保持一致。支持单通道灰度图与多通道彩色图，兼容 `CV_8U` / `CV_16U` / `CV_32F` / `CV_64F` 等多种位深。

---

## 2. 支持的标准化模式

| 模式 | 内部 key | 数学公式 | 适用场景 |
|------|----------|----------|----------|
| **最小最大归一化** | `minMax` | `dst = (src - min) / (max - min) * (tMax - tMin) + tMin` | 将像素值线性映射到 `[0,1]` 或 `[-1,1]` |
| **Z-Score 标准化** | `zScore` | `dst = (src - mean) / (std + eps)` | 按自定义或自动统计量标准化 |
| **层均一化** | `layerNorm` | 逐通道或全特征维度计算 `mean/std` 后标准化 | Transformer、LLM 风格预处理 |
| **实例均一化** | `instanceNorm` | 每个样本的每个通道独立计算 `mean/std` | 风格迁移、生成模型 |
| **批均一化** | `batchNorm` | 单图场景下按整张图像所有像素计算统计量 | 无真实 batch 时的近似批标准化 |
| **ImageNet 预训练标准化** | `imageNet` | `dst = (src / 255.0 - mean) / std` | 使用 ImageNet 预训练权重前的标准预处理 |

---

## 3. 参数说明

| 参数名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `mode` | Enum | `minMax` | 标准化模式，见上表 |
| `targetRange` | Enum | `0_1` | 仅 `minMax` 模式有效：`0_1` 或 `minus1_1` |
| `perChannel` | Bool | `true` | 是否逐通道独立计算统计量（`minMax` / `layerNorm` 生效） |
| `mean` | Vector | `""` | 逗号分隔的自定义均值，如 `0.485,0.456,0.406` |
| `std` | Vector | `""` | 逗号分隔的自定义标准差，如 `0.229,0.224,0.225` |
| `epsilon` | Float | `1e-5` | 防止除零的小常数，必须大于 0 |

### 3.1 mean/std 自动规则

- `zScore`：留空时自动从输入图像计算。
- `imageNet`：留空时使用 ImageNet 默认值 `mean=[0.485,0.456,0.406]`, `std=[0.229,0.224,0.225]`。
- 用户只提供一个值时，自动广播到所有通道；通道数不足时，剩余通道使用最后一个值填充。

---

## 4. C++ 接口

```cpp
#include "Vision/NormalizationTool.h"
#include <QJsonObject>

// 创建算子
NormalizationTool tool;

// 配置参数
QJsonObject params;
params["mode"] = "imageNet";
params["mean"] = "0.485,0.456,0.406";
params["std"] = "0.229,0.224,0.225";
tool.configure(params);

// 执行标准化
cv::Mat input = cv::imread("image.jpg");  // BGR
ToolResult result;
bool ok = tool.execute(input, result);

// 结果
if (ok) {
    cv::Mat display = result.overlayImage;        // uint8 显示图（已映射到 [0,255]）
    QString mode = result.data["mode"].toString(); // 实际使用的模式
    double outMin = result.data["outputMin"].toDouble();
    double outMax = result.data["outputMax"].toDouble();
}
```

---

## 5. 使用示例

### 5.1 图像分类预处理（ImageNet 模式）

```cpp
NormalizationTool norm;
QJsonObject p;
p["mode"] = "imageNet";  // 自动除以 255 并使用 ImageNet mean/std
norm.configure(p);

cv::Mat src = cv::imread("cat.jpg");
ToolResult r;
norm.execute(src, r);
cv::Mat modelInput = ...;  // 注意：result.overlayImage 是显示图，不是模型输入
```

> **重要**: `result.overlayImage` 是为了在 QDV 预览窗口中显示而映射到 `[0,255]` 的图像。若要将标准化后的 float 结果直接送入模型，请从 `result.data` 中的统计量或自行保存 `normalize()` 的输出。后续版本将提供独立的 `floatOutput` 输出端口。

### 5.2 目标检测前的 Min-Max 归一化

```cpp
NormalizationTool norm;
QJsonObject p;
p["mode"] = "minMax";
p["targetRange"] = "0_1";
p["perChannel"] = false;  // 全局统一缩放
norm.configure(p);
```

### 5.3 自定义数据集的 Z-Score 标准化

```cpp
NormalizationTool norm;
QJsonObject p;
p["mode"] = "zScore";
p["mean"] = "128.0";  // 单值自动广播到所有通道
p["std"] = "64.0";
norm.configure(p);
```

---

## 6. 输出字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `mode` | string | 实际使用的标准化模式 |
| `targetRange` | string | `minMax` 模式的目标区间 |
| `perChannel` | bool | 是否逐通道计算 |
| `epsilon` | double | 实际使用的 epsilon |
| `outputMin` | double | 标准化后 float 结果的全局最小值 |
| `outputMax` | double | 标准化后 float 结果的全局最大值 |
| `mean` | array[double] | 实际使用的各通道均值 |
| `std` | array[double] | 实际使用的各通道标准差 |
| `inputChannels` | int | 输入图像通道数 |
| `inputDepth` | int | 输入图像 CV 深度 |
| `outputDepth` | int | 输出图像 CV 深度（固定 `CV_32F`） |

---

## 7. 边界条件与异常处理

| 场景 | 行为 |
|------|------|
| 输入为空 | `execute` 返回 `false`，`result.data["error"] = "Empty input"` |
| `epsilon <= 0` | 自动重置为 `1e-5` 并输出 warn 日志 |
| 常量图（std=0） | 通过 `epsilon` 保护，输出接近 0 的矩阵，不会崩溃 |
| `mean/std` 数量不足通道数 | 自动广播/填充最后一个值 |
| 不支持的 `mode` 字符串 | 回退到 `minMax` 模式 |
| 1x1 最小尺寸图像 | 正常执行，结果维度保持 1x1 |

---

## 8. 集成到深度学习流程

在 QDV 编辑模块中，将 `均一化` 算子置于 `ReadImage` 或 `GrabImage` 之后、`AiClassify` / `YoloDetect` / `DetectObjectsDl` / `SegmentDl` 之前，即可形成标准的深度学习预处理链：

```
ReadImage → Normalization → AiClassify
```

在 `config/operators.json` 中，均一化算子已注册在 **深度学习** 分类下，参数编辑器会自动渲染模式选择、目标区间、逐通道开关、均值/标准差向量和 epsilon 输入框。

---

## 9. 单元测试

测试文件: `tests/Vision/test_normalization.cpp`

覆盖内容：
- 工厂创建与空输入
- MinMax `[0,1]` / `[-1,1]` 两种区间
- 常量图除零保护
- ZScore 自定义 mean/std
- ImageNet 默认常量校验
- LayerNorm 全特征模式
- BatchNorm 通道共享统计量
- 多 bit-depth 输入（8U/16U/32F/64F）
- 序列化/反序列化一致性
- epsilon 非法值重置
- 1x1 最小尺寸图像

运行方式：

```powershell
# 在已构建的 build 目录中
ctest -R QDV_Tests_All -V
# 或单独运行测试可执行文件
.\build\tests\QDV_tests "[Vision][Normalization]"
```
