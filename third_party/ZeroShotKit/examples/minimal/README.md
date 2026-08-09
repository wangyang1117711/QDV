# zeroshot_minimal - 最简推理示例

本示例演示如何使用 `zsu::Kit` 门面类加载 AnomalyCLIP 模型，并对单张图像进行零样本异常检测推理。

## 功能

- 加载 AnomalyCLIP 模型
- 设置文本提示词（正常 / 异常）
- 读取单张图像并执行同步推理
- 输出异常分数、分类结果与耗时统计

## 编译步骤

```bash
# 进入示例目录
cd third_party/ZeroShotKit/examples/minimal

# 创建构建目录
mkdir build && cd build

# 配置（请根据实际环境调整 Qt6 / OpenCV / ONNX Runtime 路径）
cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_PREFIX_PATH="D:/Qt/6.6.0/msvc2019_64;D:/opencv/build" ^
    -DONNXRUNTIME_ROOT="D:/onnxruntime"

# 编译
cmake --build . --config Release
```

## 运行示例

```bash
# 基本用法
./zeroshot_minimal <model_path> <image_path>

# 指定自定义文本提示词（用 '|' 分隔）
./zeroshot_minimal D:/models/anomalyclip C:/images/sample.png "normal:product|anomaly:damaged product"
```

## 参数说明

| 参数 | 说明 |
|------|------|
| `model_path` | AnomalyCLIP 模型所在目录或文件路径 |
| `image_path` | 待推理图像路径（支持 jpg/png/bmp） |
| `text_prompt` | 可选，自定义文本提示词，用 `\|` 分隔多个提示词 |

## 输出示例

```
[信息] 正在加载 AnomalyCLIP 模型: D:/models/anomalyclip
[信息] 模型加载成功
[信息] 文本提示词: normal:product | anomaly:damaged product
[信息] 图像尺寸: 512x512
---------------- 推理结果 ----------------
分类结果:     anomaly
置信度:       0.8723
异常分数:     0.8723 (阈值 0.5)
是否异常:     是
检测框数量:   0
---------------- 耗时统计 ----------------
预处理:       5 ms
推理:         23 ms
后处理:       1 ms
引擎总耗时:   29 ms
墙钟耗时:     31 ms
后端:         ONNXRuntime
```
