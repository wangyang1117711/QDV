# zeroshot_batch - 批量推理示例

本示例演示如何使用 `zsu::Kit` 门面类对目录下多张图像进行异步批量推理，并将结果导出为 JSON 报告。

## 功能

- 支持多种模型类型（anomalyclip / groundingdino / mobilesam / openclip / patchcore）
- 递归遍历输入目录下的所有图像文件（jpg/jpeg/png/bmp）
- 调用 `Kit::inferBatchAsync` 异步批量推理
- 实时显示进度条（连接 `progressUpdated` 信号）
- 将所有结果导出为结构化 JSON 报告
- 汇总统计：成功/失败数、异常/正常样本数、平均耗时

## 编译步骤

```bash
# 进入示例目录
cd third_party/ZeroShotKit/examples/batch

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
# 基本用法（使用默认文本提示词）
./zeroshot_batch <model_path> <model_type> <input_dir> <output_json>

# 完整用法（自定义文本提示词）
./zeroshot_batch D:/models/anomalyclip anomalyclip C:/images ./report.json \
    "normal:product|anomaly:damaged product"
```

## 参数说明

| 参数 | 说明 |
|------|------|
| `model_path` | 模型文件路径或所在目录 |
| `model_type` | 模型类型：`anomalyclip` / `groundingdino` / `mobilesam` / `openclip` / `patchcore` |
| `input_dir` | 输入图像目录（递归扫描子目录） |
| `output_json` | 输出 JSON 报告路径 |
| `text_prompts` | 可选，自定义文本提示词，用 `\|` 分隔 |

## 输出示例

```
[信息] 正在加载模型 [anomalyclip]: D:/models/anomalyclip
[信息] 模型加载成功
[信息] 共发现 24 张图像
[########################################] 24/24 (100%)
[信息] 批量推理完成，共 24 条结果
---------------- 批量推理汇总 ----------------
总图像数:       24
成功数:         24
失败数:         0
异常样本:       9
正常样本:       15
平均推理耗时:   27.3 ms
总墙钟耗时:     712 ms
报告已写入:     ./report.json
```

## JSON 报告结构

```json
{
    "model_path": "D:/models/anomalyclip",
    "model_type": "anomalyclip",
    "input_dir": "C:/images",
    "total_images": 24,
    "total_results": 24,
    "wall_time_ms": 712,
    "text_prompts": ["normal:product", "anomaly:damaged product"],
    "results": [
        {
            "image_path": "C:/images/001.jpg",
            "index": 0,
            "status": "success",
            "category": "anomaly",
            "confidence": 0.8723,
            "anomaly_score": 0.8723,
            "latency_ms": 28,
            "detections": [],
            "num_detections": 0
        }
    ],
    "success_count": 24,
    "failure_count": 0,
    "anomaly_count": 9,
    "normal_count": 15,
    "anomaly_threshold": 0.5,
    "avg_inference_ms": 27.3
}
```
