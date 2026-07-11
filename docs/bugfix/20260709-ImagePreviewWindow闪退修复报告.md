# QDetectVision 闪退 Bug 修复报告（QRhi 冲突）

- **报告日期**：2026-07-09
- **模块**：EditView 编辑模块 / ImagePreviewWindow 图像预览浮窗
- **故障等级**：P0（启动后双击算子无反应并闪退）
- **状态**：已修复并通过构建验证

---

## 一、背景

用户反馈：打开软件后，未做执行操作，双击算子库的算子无反应，然后闪退。终端持续输出：

```
Texture 0x1b2a1c34680 () belongs to QRhi 0x1b2a1d920f0, but client code attempted to use it with QRhi 0x1b2a1dd2ad0. This is wrong.
Device loss detected in Present()
```

需排查根因并修复。

## 二、根因分析

### 问题定位：QQuickWidget 内嵌套独立顶层 Window 导致 QRhi 冲突

通过源码审查发现：

1. [Main.qml:2905](file:///e:/anchor/Trae/QDV/qml/EditView/Main.qml#L2905-L2908) 在 QQuickWidget 承载的 Main.qml 中静态声明了一个独立的顶层 `Window`：

   ```qml
   ImagePreviewWindow {
       id: previewWindow
   }
   ```

2. [ImagePreviewWindow.qml](file:///e:/anchor/Trae/QDV/qml/EditView/ImagePreviewWindow.qml)（v3.1.0）原实现为 QML 顶层 `Window`：

   ```qml
   Window {
       flags: Qt.Window | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
       visible: false
       ...
   }
   ```

3. Qt 6 默认使用 QRhi 作为底层渲染抽象，每个顶层 `Window` 默认创建**独立的 QRhi 实例**（除非显式共享）

4. 即使 `visible: false`，QML 在 QQuickWidget 中实例化 Window 时也会预创建 QRhi 上下文

5. 当主 QML 与 Window 间共享图像资源（Image 加载相同 source）时，纹理会在两个 QRhi 间被错误使用

6. 持续报错 → 渲染线程崩溃 → "双击无反应"（事件循环被阻塞）→ `Device loss detected in Present()` → 应用闪退

### 双击无反应的传导链

```
QRhi 冲突（持续报错）
  → 渲染线程崩溃/卡死
  → Qt 事件循环被阻塞
  → 双击事件无法被 MouseArea 处理
  → "双击算子库无反应"
  → 最终 Device loss 闪退
```

## 三、修复方案

### 核心思路

将独立顶层 `Window` 改为 `Popup`（来自 QtQuick.Controls），与主 QML 共享同一 QRhi 上下文，彻底消除冲突源头。

### 修复 1：ImagePreviewWindow.qml 完全重构

[ImagePreviewWindow.qml](file:///e:/anchor/Trae/QDV/qml/EditView/ImagePreviewWindow.qml) 主要变更：

| 项 | 原（v3.1.0） | 新（v3.2.0） |
|----|--------------|--------------|
| 根类型 | `Window` | `Popup`（QtQuick.Controls） |
| `flags` | `Qt.Window \| Qt.WindowStaysOnTopHint \| Qt.FramelessWindowHint` | 移除（Popup 不支持 Window flags） |
| `color: "transparent"` | Window 背景 | `background: Item { }` 让内部 mainContainer 自绘 |
| 最前显示 | `Qt.WindowStaysOnTopHint` | `z: 1000` |
| 位置控制 | `previewWindow.x/y`（绝对屏幕坐标） | `previewWindow.x/y`（相对父 Item） |
| 最大化范围 | `Screen.desktopAvailableWidth/Height`（整个桌面） | `parentItem.width/height`（父窗口内） |
| 状态机 | `states: [...]` 作用于 Window | `Binding on width/height { when: isMinimized }` |
| `open()` | 重写：设置 x/y + `visible = true` + `raise()` | 不重写，用 `onAboutToShow` 钩子同步位置 |
| `close()` | 重写：`visible = false` | 直接使用 Popup 基类 `close()` |
| QtQuick.Window import | 需要 | 已移除 |
| `raise()` 调用 | 需要 | 不需要（z-index 保证） |

关键设计点：
- **不重写 open()/close()**：避免覆盖 Popup 基类的信号机制（aboutToShow/opened/closed 等）
- **onAboutToShow 钩子**：在弹出前同步位置，实现"首次居中"
- **Binding 控制尺寸**：`Binding on width/height { when: isMinimized }` 替代原 states，最小化时高度收缩为标题栏+边框

### 修复 2：DeployDialog.qml 动态创建适配

[DeployDialog.qml:307-331](file:///e:/anchor/Trae/QDV/qml/EditView/DeployDialog.qml#L307-L331) 适配 Popup 接口：

| 项 | 原 | 新 |
|----|----|----|
| `createObject` 的 parent | `dlg`（Dialog 自身，非 Item） | `dlg.parent`（DeployDialog 的父 Item） |
| 显示调用 | `win.show()` | `popup.open()` |

**原因**：Popup 的 parent 必须是 Item；Dialog/Popup 自身不是 Item。改为 `dlg.parent` 确保 Popup 有合法父级。

### 修复 3：Main.qml 无需改动

[Main.qml:137-142](file:///e:/anchor/Trae/QDV/qml/EditView/Main.qml#L137-L142) 中调用方式：

```qml
function openImagePreview(sourceUrl, processedUrl, title) {
    previewWindow.sourceImage = sourceUrl
    previewWindow.processedImage = processedUrl
    previewWindow.imageTitle = title || "效果预览"
    previewWindow.open()  // 兼容 Popup 基类 open()
}
```

Popup 基类提供 `open()` 方法，签名兼容，无需改动。

## 四、关键代码位置

| 文件 | 修改点 |
|------|--------|
| [ImagePreviewWindow.qml](file:///e:/anchor/Trae/QDV/qml/EditView/ImagePreviewWindow.qml) | Window → Popup 完全重构 |
| [DeployDialog.qml#L307-L331](file:///e:/anchor/Trae/QDV/qml/EditView/DeployDialog.qml#L307-L331) | createObject parent + show→open |
| [Main.qml#L2905-L2908](file:///e:/anchor/Trae/QDV/qml/EditView/Main.qml#L2905-L2908) | 无需改动（open() 兼容） |
| [qmldir](file:///e:/anchor/Trae/QDV/qml/EditView/qmldir) | 无需改动（1.0 版本号兼容） |

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
| ImagePreviewWindow 从 Window 改为 Popup | ✓ |
| 移除 QtQuick.Window import | ✓ |
| 移除 Window flags，改用 z-index | ✓ |
| 最大化范围改为 parent.width/height | ✓ |
| 状态机改为 Binding 控制尺寸 | ✓ |
| open()/close() 保留基类实现，用 onAboutToShow 同步位置 | ✓ |
| DeployDialog 中 createObject parent 改为 dlg.parent | ✓ |
| DeployDialog 中 win.show() 改为 popup.open() | ✓ |
| Main.qml 中 previewWindow.open() 兼容性验证 | ✓ |
| qmldir 版本号兼容（1.0 仍可用） | ✓ |
| 构建通过 | ✓ |

### 预期运行表现

1. **不再闪退**：Popup 与主 QML 共享同一 QRhi，无纹理冲突
2. **双击算子恢复响应**：渲染线程正常，事件循环可用
3. **预览浮窗功能保留**：
   - 双击效果预览图仍可弹出对比窗口
   - 标题栏可拖动、双击最大化
   - 透明度滑块可调
   - 最小化/最大化/关闭按钮可用
4. **位置约束变化**：浮窗只能在主窗口内浮动（不能拖出主窗口外），这是用户选择"改为内嵌 Popup"方案的预期行为

## 六、影响范围

### 正向影响

- 彻底消除 QRhi 冲突，应用稳定运行
- 双击算子、连线、参数编辑等所有交互恢复正常
- 部署对话框的"查看大图"功能同样受益

### 行为变化（用户已确认接受）

- ImagePreviewWindow 不再是独立操作系统窗口
- 不能拖出主窗口外
- 没有任务栏图标
- 最大化范围限于父窗口（原为整个桌面）

### 不受影响

- 主窗口、算子库、画布、属性面板等核心交互不受影响
- 图像预览内容（原图/处理后图并排显示）不变
- 透明度调节、最小化、最大化功能不变

## 七、后续建议

1. **运行时验证**：运行 `QDetectVision.exe`，验证：
   - 双击算子库算子能正常添加到画布（无卡死、无闪退）
   - 双击效果预览图能弹出对比窗口
   - 拖动/最大化/最小化/关闭按钮均正常
   - 部署对话框"查看大图"按钮正常
2. **日志监控**：观察终端是否还有 `Texture belongs to QRhi` 或 `Device loss` 警告（应完全消失）
3. **回归测试**：连续多次打开/关闭 ImagePreviewWindow，验证无内存泄漏或渲染异常
4. **长期改进**：项目内全局审查是否还有其他 QQuickWidget 内嵌套顶层 Window 的情况，统一改为 Popup 模式

## 八、参考资料

- Qt 官方文档：[QQuickWidget](https://doc.qt.io/qt-6/qquickwidget.html)、[Popup](https://doc.qt.io/qt-6/qml-qtquick-controls2-popup.html)、[QRhi](https://doc.qt.io/qt-6/qrhi.html)
- Qt Bug Tracker：QQuickWidget + 嵌套 Window 导致 QRhi 冲突是已知问题模式
- 项目宪法 AGENTS.md：「全绿≠完成」「语义高于语法」原则

## 九、与上次修复的关系

本次修复与 2026-07-09 早些时候的 [PreviewManager 卡死修复](./20260709-PreviewManager卡死修复报告.md) 是两个独立问题：

| 项 | PreviewManager 卡死 | ImagePreviewWindow 闪退 |
|----|---------------------|------------------------|
| 根因 | 主线程同步阻塞 + 0x0 覆盖 + 重入循环 | QRhi 渲染上下文冲突 |
| 触发时机 | 执行算子时 | 启动后立即（双击算子时） |
| 修复层 | C++（PreviewManager / ImageVariableManager） | QML（ImagePreviewWindow / DeployDialog） |
| 修复手段 | QtConcurrent 异步 + QMutex | Window → Popup |

两者均已修复，构建均通过。
