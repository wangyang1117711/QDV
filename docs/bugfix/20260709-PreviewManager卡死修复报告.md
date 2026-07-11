# QDetectVision 卡死 Bug 修复报告

- **报告日期**：2026-07-09
- **模块**：EditView 编辑模块 / PreviewManager 预览管理器
- **故障等级**：P0（用户可感知的功能性卡死）
- **状态**：已修复并通过构建验证

---

## 一、背景

用户在编辑模块执行算子（含 FFT 等耗时算子）过程中，程序出现卡死现象，界面无响应，日志停止写入。需排查根因并修复。

## 二、根因分析

通过分析 `build/bin/logs/20260709.log` 与源码，定位为 **三重叠加问题**：

### 问题 1：主线程同步阻塞（卡死主因）

`PreviewManager::doPreview()` 在主线程同步调用 `m_bridge->runSingleOperator(...)`，而 `runSingleOperator` 内部会调用 `ToolChainExecutor::execute()`，其中执行 FFT、边缘检测等 OpenCV 耗时算子直接占用主线程。

日志证据：
```
[14:11:56] [WARN] ToolChainExecutor::execute() called from main thread, prefer executeAsync() to avoid UI blocking
```
此后日志停止写入，表明主线程已被算子占用，事件循环无法处理日志队列刷新，界面完全冻结。

### 问题 2：图像变量被 0x0 覆盖（数据损坏）

`runSingleOperator` 内部已用 **6 参数版本** 正确更新 ImageVariableManager（写入 `800x800`），但 `doPreview` 完成后又用 **3 参数版本**（width/height/channels 默认为 0）重复调用 `updateImageVariable`，将有效图像信息覆盖为 `0x0`。

日志证据：
```
[14:11:55] ImageVariableManager: 更新 FFT变换 (node_xxx) 800x800
[14:11:56] ImageVariableManager: 更新 FFT变换 (node_xxx) 0x0
```

### 问题 3：重入循环（持续抖动）

当 `m_isRunning=true` 期间收到新的预览请求时，原实现会 `m_debounceTimer.start()` 重启防抖定时器，导致算子执行完成后立即又触发一次执行，形成"执行→信号→请求→执行"的持续循环，加剧卡顿。

## 三、修复方案

### 修复 1：异步执行算子链

将 `doPreview()` 中的同步调用改为 `QtConcurrent::run` 异步执行，通过 `QFutureWatcher<QVariantMap>` 在子线程完成后将结果回传主线程：

- **[PreviewManager.h](file:///e:/anchor/Trae/QDV/include/UI/PreviewManager.h)**：新增 `QFutureWatcher<QVariantMap> m_watcher;`、`QString m_runningNodeId;`、`onPreviewFinished()` 槽。
- **[PreviewManager.cpp](file:///e:/anchor/Trae/QDV/src/UI/PreviewManager.cpp#L120-L145)**：`doPreview()` 改为 `QtConcurrent::run` 异步执行 `runSingleOperator`，主线程立即返回。

### 修复 2：删除重复更新

`onPreviewFinished()` 中删除对 `updateImageVariable` 的重复调用，仅保留 `runSingleOperator` 内部的 6 参数版本调用，避免 0x0 覆盖。

### 修复 3：重入保护改为直接 return

`doPreview()` 中 `m_isRunning=true` 时直接 return，不再 restart timer。在 `onPreviewFinished()` 完成后再判断 `m_pendingNodeId` 是否变化，如有变化才重新触发防抖，避免持续循环。

### 修复 4：ImageVariableManager 线程安全

由于算子执行移至子线程，子线程会调用 `ImageVariableManager::updateImageVariable` 写入，主线程同时读取列表，必须加互斥锁保护 `m_imageVariables`，否则 `QHash` 并发读写会导致内部结构损坏崩溃。

- **[ImageVariableManager.h](file:///e:/anchor/Trae/QDV/include/Core/ImageVariableManager.h#L84-L90)**：新增 `mutable QMutex m_mutex;` 成员。
  - 使用 `mutable`：const 成员函数（`imageVariable()` / `imageVariables()` / `imagePath()` / `count()` / `exists()`）也需要在读取时加锁。
- **[ImageVariableManager.cpp](file:///e:/anchor/Trae/QDV/src/Core/ImageVariableManager.cpp)**：所有读写方法均添加 `QMutexLocker locker(&m_mutex);` 保护，并将信号 emit 移到锁外避免死锁。

## 四、关键代码位置

| 文件 | 修改点 |
|------|--------|
| [PreviewManager.h](file:///e:/anchor/Trae/QDV/include/UI/PreviewManager.h) | 新增 m_watcher / m_runningNodeId / onPreviewFinished() |
| [PreviewManager.cpp#L120-L145](file:///e:/anchor/Trae/QDV/src/UI/PreviewManager.cpp#L120-L145) | doPreview() 改为 QtConcurrent::run 异步执行 |
| [PreviewManager.cpp#L147-L180](file:///e:/anchor/Trae/QDV/src/UI/PreviewManager.cpp#L147-L180) | onPreviewFinished() 接收子线程结果，删除重复更新 |
| [ImageVariableManager.h#L84-L90](file:///e:/anchor/Trae/QDV/include/Core/ImageVariableManager.h#L84-L90) | 新增 mutable QMutex m_mutex |
| [ImageVariableManager.cpp](file:///e:/anchor/Trae/QDV/src/Core/ImageVariableManager.cpp) | 所有读写方法加 QMutexLocker |

## 五、验证结果

### 构建验证

```
cmake --build e:\anchor\Trae\QDV\build --config Debug --target QDetectVision
```

- **构建结果**：成功
- **退出码**：0
- **输出末尾**：`[100%] Built target QDetectVision`
- **产物**：`build/bin/QDetectVision.exe`

### 修复点核对

| 修复项 | 状态 |
|--------|------|
| doPreview 改为 QtConcurrent::run 异步执行 | ✓ |
| onPreviewFinished 接收子线程结果回主线程 | ✓ |
| 删除 onPreviewFinished 中重复的 updateImageVariable 调用 | ✓ |
| 重入保护改为直接 return，避免持续循环 | ✓ |
| ImageVariableManager 所有读写方法加 QMutexLocker | ✓ |
| QMutex 声明为 mutable，支持 const 函数加锁 | ✓ |
| 信号 emit 移到锁外，避免死锁 | ✓ |

### 预期运行表现

1. **不再卡死**：FFT 等耗时算子在子线程执行，主线程事件循环保持响应，界面可正常拖拽、缩放、切换节点。
2. **图像变量正确**：执行后图像变量尺寸为有效值（如 800x800），不再被 0x0 覆盖。
3. **无重入循环**：连续切换节点不会形成持续执行循环。
4. **跨线程安全**：子线程写 ImageVariableManager 与主线程读列表互不冲突。

## 六、影响范围

- **正向影响**：编辑模块预览体验显著改善，所有耗时算子（FFT、边缘检测、阈值等）均不再阻塞 UI。
- **潜在影响**：
  - 算子执行在子线程，若算子内部有访问 UI 控件的代码需排查（当前 VisionTool 体系仅处理 cv::Mat，不涉及 UI，安全）。
  - 信号触发频率可能略增（异步完成事件），但已通过防抖定时器（300ms）控制。

## 七、后续建议

1. **运行时验证**：建议运行 QDetectVision.exe，在编辑模块中拖入 FFT 算子并执行，验证界面无卡顿、图像预览正确显示。
2. **回归测试**：连续快速切换多个节点，观察是否仍存在重入问题。
3. **日志监控**：观察新日志中是否还有 `called from main thread` 警告（应消失）。
4. **长期改进**：考虑为 `ToolChainExecutor::execute()` 增加 `Q_ASSERT` 断言，禁止在主线程直接调用，强制使用 `executeAsync()`。

## 八、参考资料

- 项目宪法 AGENTS.md：「语义高于语法」「全绿≠完成」原则
- Qt 官方文档：[QtConcurrent::run](https://doc.qt.io/qt-6/qtconcurrent.html)、[QFutureWatcher](https://doc.qt.io/qt-6/qfuturewatcher.html)、[QMutex](https://doc.qt.io/qt-6/qmutex.html)
- 项目 memory：`ImageVariableManager collects image variables indexed by node ID`、`PreviewManager implements 300ms debounced real-time preview`
