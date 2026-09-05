# 零样本检测"界面未打开"排查与修复

## 结论先行
经**隔离探针实证**，零样本检测视图（`ZeroShotDetectView`）的**构造、显示、`switchView` 滑动动画、三栏 `QSplitter` 渲染均正常**——打开/显示逻辑本身没有缺陷，`CentralWindow` 的导航接线（QStackedWidget 索引 8 / `switchView(8)`）也正确。

因此"界面未打开"不是代码打开逻辑的 bug，而是**运行环境特有的构造期失败**（例如用户机器上模型源 / LM Studio 单例未就绪，`ZeroShotDetectView` 构造抛异常；原代码无 `try/catch`，异常会拖垮整个 `CentralWindow` 构造，主界面看似"打不开"）。

## 怎么验证的（沙箱限制下的关键手段）
- GUI 子系统 exe 在本沙箱无法无头运行（仅部署了 `qwindows.dll`，缺 `qoffscreen`/`qminimal`）。从 Qt 安装目录复制 `qoffscreen.dll` 到 `build/bin/platforms/` 后，可无头运行。
- 新增独立探针 `tests/zero_shot_view_probe.cpp` + `tests/CMakeLists.txt` 的 `zero_shot_view_probe` target：独立链接 `UI` 等库（避开 `QDV_tests` 全量在 `EditViewBridge/ROI` 上 OOM 段错误），构造 `ZeroShotDetectView` 并把进度 `fprintf+fflush` 写入 `%TEMP%/zsv_probe.log`（子进程崩溃也不丢证据）。

探针输出（决定性）：
```
CONSTRUCTED_OK
A_DIRECT_SHOW viewGeometry=0,0,1200,821 visible=1
B_BEFORE_SWITCH targetGeometry=0,0,1200,821
B_AFTER_ANIM_FINALIZE viewGeometry=0,0,1200,821 visible=1
SPLITTER_SIZES=360 552 280 / SPLITTER_VISIBLE
PROBE_DONE
```

## 已落地的修复（防御性）
1. `src/UI/CentralWindow.cpp`
   - 用 `try/catch` 包裹 `new ZeroShotDetectView()`；构造失败则 `m_zeroShotView = nullptr`，并在索引 8 放**占位页**（橙字提示"零样本检测模块未能加载"，引导查看 `logs/`），不再静默致死。
2. `include/UI/ZeroShotDetectView.h` + `src/UI/ZeroShotDetectView.cpp`
   - `refreshSplitterSizes()` 在 `width<=0`（首次布局未就绪）时**有上限重试（≤5 次）**，杜绝极端情况下界面空白。

## 已重新构建
- `build/bin/QDetectVision.exe`（无错误，含上述修复）。

## 需要你确认（定位真实根因）
请用重建后的 `QDetectVision.exe` 在其机器上运行：
- 若窗口仍"打不开"——现在会显示占位页文案，且 `logs/` 目录会记录 `[CentralWindow] 零样本检测视图构造失败: <原因>`。
- 把占位页文案或 `logs/` 中的错误信息发我，即可精确定位是模型源路径、LM Studio 连接，还是某个单例未就绪。

## 涉及文件
- `src/UI/CentralWindow.cpp`（try/catch + 占位页）
- `include/UI/ZeroShotDetectView.h` / `src/UI/ZeroShotDetectView.cpp`（splitter 重试）
- `tests/zero_shot_view_probe.cpp` + `tests/CMakeLists.txt`（隔离冒烟探针，可复现）
- `build/bin/QDetectVision.exe`（已重建）
