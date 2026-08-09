# zeroshot_with_ui - 带 UI 集成示例

本示例演示如何将 ZeroShotKit 的 `ZeroShotPanel` 和 `ZeroShotResultPanel` 嵌入自定义 `QMainWindow`，通过信号槽连接完成端到端推理流程。

## 功能

- 左侧 `ZeroShotPanel`：模型类型选择、路径浏览、阈值调整、提示词输入、PatchCore 样本管理
- 右侧 `ZeroShotResultPanel`：异常分数仪表、分类结果、检测框列表、掩码/热力图预览、性能指标
- 菜单栏：文件（打开图像 / 打开目录 / 退出）
- 状态栏：显示当前模型、推理耗时、进度
- 信号槽连接：
  - `ZeroShotPanel::modelLoadRequested` → `Kit::loadModel`
  - `ZeroShotPanel::inferenceRequested` / `inferenceAllRequested` → `Kit::inferAsync` / `inferBatchAsync`
  - `Kit::inferenceCompleted` → `ZeroShotResultPanel::setResult`
  - `Kit::batchCompleted` → `ZeroShotResultPanel::setResults`
  - `Kit::progressUpdated` → `ZeroShotResultPanel::setProgress` + 状态栏
  - `Kit::errorOccurred` → `QMessageBox` 弹窗
  - `ZeroShotPanel::settingsChanged` → 同步配置到 `Kit`

## 界面布局

```
+--------------------------------------------------------------+
| 文件(&F)                                                      |
+------------------+-------------------------------------------+
|                  |                                           |
|  ZeroShotPanel   |        ZeroShotResultPanel                |
|  (左侧配置面板)   |        (右侧结果展示面板)                  |
|                  |                                           |
|  - 模型类型       |  - 异常分数仪表                            |
|  - 模型路径       |  - 分类结果 / 置信度                       |
|  - 提示词         |  - 检测框列表                              |
|  - 异常阈值       |  - 掩码预览                                |
|  - 检测阈值       |  - 热力图预览                              |
|  - 量化开关       |  - 性能指标                                |
|  - 加载/推理按钮  |  - 上一张/下一张导航                       |
|  - PatchCore 管理 |                                           |
|  - 准确性保障     |                                           |
|                  |                                           |
+------------------+-------------------------------------------+
| 模型: AnomalyCLIP  |  耗时: 28 ms  |          进度: 5 / 10    |
+--------------------------------------------------------------+
```

## 编译步骤

```bash
# 进入示例目录
cd third_party/ZeroShotKit/examples/with_ui

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
# Windows 下双击 zeroshot_with_ui.exe，或在命令行运行：
./zeroshot_with_ui.exe
```

启动后：
1. 在左侧面板选择模型类型（如 AnomalyCLIP）
2. 点击"浏览"选择模型路径
3. 点击"加载模型"
4. 通过菜单"文件 > 打开图像"选择一张图像（或"打开目录"选择批量目录）
5. 点击左侧"推理当前"或"推理全部"
6. 右侧结果面板查看推理结果

## 截图

> 截图占位（请运行程序后自行截图）
