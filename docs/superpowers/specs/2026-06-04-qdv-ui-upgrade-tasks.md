# QDV 界面升级 详细任务清单

> **配套设计稿**：`docs/superpowers/specs/2026-06-04-qdv-ui-upgrade-design.md`  
> **生成日期**：2026-06-04  
> **约定**：任务 ID 格式 `<M>.<NN>`（如 M1.01）；每个任务有「代码」「测试」「验收」三栏；依赖关系用箭头 `→` 表示。

---

## 总览

| 阶段 | 任务数 | 工作量 | 累计 |
|------|--------|--------|------|
| M1 主窗口增强 | 12 | 2–3 天 | 2–3 天 |
| M2 QML EditView 画布 | 15 | 5–7 天 | 7–10 天 |
| M3 算子参数面板 | 13 | 3–4 天 | 10–14 天 |
| M4 JSON I/O + 50 步 Undo | 10 | 2 天 | 12–16 天 |
| M5 UX 反馈 | 9 | 1–2 天 | 13–18 天 |
| **合计** | **59** | **13–18 天** | — |

---

## M1：MainWindow 增强（12 任务，2–3 天）

### M1.01 — 创建标题栏布局基类
- **依赖**：无
- **代码**：`include/UI/TitleBar.h` + `src/UI/TitleBar.cpp`
  - `QWidget` 子类，含左侧 `QLabel`（项目名/版本号）、中间 `Spacer`、右侧 3 个 `QToolButton`（min/max/close）
  - 信号：`minimizeClicked() maximizeClicked() closeClicked()`
- **测试**：手动验证 3 按钮可见
- **验收**：编译通过，类可被 QSS 美化

### M1.02 — 集成 TitleBar 到 MainWindow
- **依赖**：M1.01
- **代码**：修改 `include/UI/MainWindow.h` `src/UI/MainWindow.cpp`
  - 用 `TitleBar` 替换现有 `CentralWindow` 上方（如果有）；或作为 `MainWindow` 的子部件
  - 高度 32px
- **测试**：手动验证 32px 高度
- **验收**：UI 显示 32px 标题栏

### M1.03 — 标题栏项目名/版本号动态绑定
- **依赖**：M1.02
- **代码**：修改 `src/UI/MainWindow.cpp`
  - `QString title = QString("QDV · %1 v%2").arg(schemeName, appVersion)`
  - 订阅 `m_schemeView::currentSchemeChanged` 信号
- **测试**：`tests/UI/test_title_bar.cpp`（Catch2）
- **验收**：切换方案时标题栏文本实时更新

### M1.04 — 标题栏 12–14pt 字体 + QSS
- **依赖**：M1.03
- **代码**：`resources/qss/main.qss` 新增 `.QTitleBar` 选择器
  ```css
  .QTitleBar { background: #1E1E1E; height: 32px; }
  .QTitleBar QLabel { font: 14pt "Segoe UI"; color: #FFFFFF; }
  .QTitleBar QLabel#accent { color: #9C27B0; }
  ```
- **测试**：手动检查字体
- **验收**：标题栏样式符合设计稿

### M1.05 — 最小化/最大化/还原按钮
- **依赖**：M1.04
- **代码**：`src/UI/TitleBar.cpp`
  - 槽 `onMinClicked → showMinimized()`
  - 槽 `onMaxClicked → isMaximized() ? showNormal() : showMaximized()`
  - 槽 `onCloseClicked → close()`
  - 按钮图标：min/max/close SVG（24×24）
- **测试**：`tests/UI/test_title_bar_buttons.cpp`（QTest::mouseClick）
- **验收**：3 按钮功能正常，< 100ms

### M1.06 — 8 方向边缘拖拽（WM_NCHITTEST 拦截）
- **依赖**：M1.05
- **代码**：`src/UI/MainWindow.cpp`
  - 重写 `nativeEvent(const QByteArray& eventType, void* message, qintptr* result)`
  - 仅 Windows：`#ifdef Q_OS_WIN`
  - 8px 边界检测；返回 `HTLEFT/HTRIGHT/HTTOP/HTBOTTOM/HTCORNER*`
- **测试**：手动拖拽 8 个方向
- **验收**：8 方向都能拖拽改变窗口大小

### M1.07 — 边界限制（最小 800×600）
- **依赖**：M1.06
- **代码**：`src/UI/MainWindow.cpp`
  - 在 `resizeEvent` 中 `if (width() < 800) setFixedWidth(800);`
  - 或 `setMinimumSize(800, 600)`
- **测试**：手动拖小窗口到极小
- **验收**：最小 800×600，不再继续缩小

### M1.08 — 平滑重绘优化
- **依赖**：M1.07
- **代码**：`src/UI/MainWindow.cpp`
  - `resizeEvent` 中 `setUpdatesEnabled(false)` 包住，结束 `setUpdatesEnabled(true); update();`
- **测试**：`tests/UI/test_resize_perf.cpp`（QElapsedTimer）
- **验收**：resize 期间无闪烁，< 100ms 响应

### M1.09 — 标题栏双击切换最大化
- **依赖**：M1.04
- **代码**：`src/UI/TitleBar.cpp`
  - `mouseDoubleClickEvent` → 发出 `maximizeClicked()`
- **测试**：手动双击标题栏
- **验收**：双击标题栏 = 最大化/还原

### M1.10 — MainWindow QSS 主题
- **依赖**：M1.08
- **代码**：`resources/qss/main.qss`
  - 完整 QSS：背景 #1E1E1E、按钮 hover #2D2D2D、按钮 pressed #3D3D3D
- **测试**：手动视觉检查
- **验收**：与设计稿一致

### M1.11 — MainWindow 单元测试
- **依赖**：M1.10
- **代码**：`tests/UI/test_main_window.cpp`（Catch2）
  - 测试标题栏 32px
  - 测试最小尺寸限制
  - 测试按钮槽函数
- **测试**：Catch2 单元测试
- **验收**：≥ 5 个测试用例通过

### M1.12 — M1 集成测试 + 截图
- **依赖**：M1.11
- **代码**：`tests/Integration/test_main_window_ui.cpp`
  - `QTest::qWaitForWindowExposed`
  - `QQuickItem::grabToImage` 截图
  - 保存到 `build/test_screenshots/M1_main_window.png`
- **验收**：截图通过人工 review

---

## M2：QML EditView 画布（15 任务，5–7 天）

### M2.01 — Qt6 Quick/QuickWidgets 依赖验证
- **依赖**：无
- **代码**：修改 `CMakeLists.txt`
  - `find_package(Qt6 COMPONENTS Quick QuickWidgets Qml REQUIRED)`
  - 链接 `Qt6::Quick` `Qt6::QuickWidgets` `Qt6::Qml`
- **测试**：`cmake -B build` 成功
- **验收**：构建无错误

### M2.02 — 创建 qml/ 目录结构
- **依赖**：M2.01
- **代码**：创建 `qml/EditView/` 目录与 11 个空 `.qml` 文件（含 `qmldir`）
- **测试**：Qt 资源系统加载测试
- **验收**：`qmlRegisterType` 成功

### M2.03 — 注册 QML 模块
- **依赖**：M2.02
- **代码**：`src/UI/EditViewBridge.cpp`
  - `qmlRegisterType<EditViewBridge>("QDV.Edit", 1, 0, "Bridge");`
- **测试**：`tests/UI/test_qml_registration.cpp`
- **验收**：QML 端 `import QDV.Edit 1.0` 成功

### M2.04 — EditViewBridge 类骨架
- **依赖**：M2.03
- **代码**：`include/UI/EditViewBridge.h` + `src/UI/EditViewBridge.cpp`
  - 单例或 app-level 实例
  - 基础 `Q_PROPERTY`（operatorList）
  - 空槽函数
- **测试**：`tests/UI/test_edit_view_bridge.cpp`（骨架）
- **验收**：编译通过，QML 可访问

### M2.05 — EditView QQuickWidget 集成
- **依赖**：M2.04
- **代码**：修改 `src/UI/EditView.cpp`（或新建）
  - 在 `EditView` 的中央区域用 `QQuickWidget` 加载 `qrc:/qml/EditView/Main.qml`
  - `setClearColor(Qt::transparent)` 避免闪烁
- **测试**：手动启动 QDV
- **验收**：QML 在 CentralWindow 中可见

### M2.06 — Main.qml 顶栏 + 三栏布局
- **依赖**：M2.05
- **代码**：`qml/EditView/Main.qml`
  - `ApplicationWindow` + `SplitView`
  - 左 240px / 中央自适应 / 右 320px
  - 顶栏：新建/保存/加载/撤销/重做按钮
- **测试**：手动验证布局
- **验收**：三栏布局生效

### M2.07 — 工具库 ToolLibraryPanel.qml
- **依赖**：M2.06
- **代码**：`qml/EditView/ToolLibraryPanel.qml`
  - 折叠分类（11 类）`ListView` + `Repeater`
  - 点击分类切换 `expanded`
- **测试**：手动点击
- **验收**：折叠展开可用

### M2.08 — 工具库项 ToolItem.qml
- **依赖**：M2.07
- **代码**：`qml/EditView/ToolItem.qml`
  - 32×32 灰度图标 + 白色背景
  - 拖拽源
- **测试**：`tests/qml/EditView/tst_tool_item.qml`（QQuickTest）
- **验收**：可拖拽

### M2.09 — FlowChartView.qml 画布
- **依赖**：M2.08
- **代码**：`qml/EditView/FlowChartView.qml`
  - `Flickable` 容器
  - 中键 `DragHandler` 平移
  - `WheelHandler` 缩放（0.5x–2x）
  - 缩放中心 = 鼠标当前位置
- **测试**：`tests/qml/EditView/tst_flow_chart_view.qml`
- **验收**：缩放/平移流畅

### M2.10 — FlowNode.qml 节点
- **依赖**：M2.09
- **代码**：`qml/EditView/FlowNode.qml`
  - 40×40 彩色图标 + 主题背景
  - 输入/输出端口（圆）
  - 选中高亮
- **测试**：`tests/qml/EditView/tst_flow_node.qml`
- **验收**：节点显示正常

### M2.11 — 拖拽添加节点（FlowChartView DropArea）
- **依赖**：M2.10
- **代码**：`qml/EditView/FlowChartView.qml` 内嵌 `DropArea`
  - 接收 ToolItem 拖拽
  - 调 `bridge.addOperator(type, x, y)`
- **测试**：`tests/qml/EditView/tst_drop_node.qml`
- **验收**：拖入成功

### M2.12 — 节点拖动（DragHandler + savedX/Y）
- **依赖**：M2.11
- **代码**：`qml/EditView/FlowNode.qml`
  - `DragHandler` 拖动
  - `property real savedX; savedY` 用于撤销
  - 拖动结束 `bridge.moveNode(nodeId, x, y)`
- **测试**：`tests/qml/EditView/tst_node_move.qml`
- **验收**：拖动流畅

### M2.13 — FlowConnection.qml 贝塞尔连线
- **依赖**：M2.12
- **代码**：`qml/EditView/FlowConnection.qml`
  - `Canvas { onPaint: ... }` 绘制贝塞尔
  - 选中高亮 + 箭头
  - 端口吸附（按下端口 MouseArea → 拖到目标端口）
- **测试**：`tests/qml/EditView/tst_flow_connection.qml`
- **验收**：连线创建 + 删除

### M2.14 — 右键删除连线
- **依赖**：M2.13
- **代码**：`qml/EditView/FlowConnection.qml`
  - `TapHandler { acceptedButtons: Qt.RightButton; onTapped: contextMenu.popup() }`
  - `Menu { MenuItem { text: "删除"; onTriggered: bridge.disconnectEdge(id) } }`
- **测试**：`tests/qml/EditView/tst_delete_connection.qml`
- **验收**：右键删除

### M2.15 — M2 集成 + 性能测试
- **依赖**：M2.14
- **代码**：`tests/Integration/test_edit_view_canvas.cpp`
  - 60fps 测试（`QQuickWindow::frameSwapped` 计数）
  - 拖拽响应 < 100ms
  - 100 节点压测
- **验收**：性能达标，截图保存

---

## M3：算子参数面板（13 任务，3–4 天）

### M3.01 — OperatorMeta + ParamSpec 数据结构
- **依赖**：M2.04
- **代码**：`include/UI/OperatorDescriptors.h`
  - `struct ParamSpec { ... }`
  - `struct OperatorMeta { ... }`
- **测试**：`tests/UI/test_operator_meta.cpp`（数据结构）
- **验收**：编译通过

### M3.02 — OperatorDescriptors 单例注册
- **依赖**：M3.01
- **代码**：`src/UI/OperatorDescriptors.cpp`
  - 静态注册 14 个算子元数据
  - `static QList<OperatorMeta> all()`
- **测试**：`tests/UI/test_operator_descriptors.cpp`
- **验收**：14 算子元数据齐全

### M3.03 — EditViewBridge::getOperatorMeta
- **依赖**：M3.02
- **代码**：`src/UI/EditViewBridge.cpp`
  - `Q_INVOKABLE QVariantMap getOperatorMeta(const QString& type)`
  - 返回 `QVariantMap`（QML 可读）
- **测试**：`tests/UI/test_bridge_meta.cpp`
- **验收**：QML 可调

### M3.04 — 14 算子元数据实现
- **依赖**：M3.02
- **代码**：`src/UI/OperatorDescriptors.cpp`（详细实现）
  - ReadImage / ImagePreprocess / Threshold / EdgeDetect / BlobDetect
  - ColorDetect / ContourAnalyze / TemplateMatch / GeometryMeasure / LineCircleDetect
  - ImageArithmetic / ImageTransform / ImageMerge / BranchControl / AiClassify
  - 每个含 ParamSpec 完整字段
- **测试**：`tests/UI/test_all_operators_meta.cpp`（14 算子）
- **验收**：14 算子元数据完整

### M3.05 — ParamForm.qml 动态表单
- **依赖**：M3.04
- **代码**：`qml/EditView/ParamForm.qml`
  - `Repeater { model: paramSpecs; delegate: Loader { sourceComponent: ... } }`
  - 7 个组件：`spinBoxComp` `doubleSpinBoxComp` `comboBoxComp` `checkBoxComp` `textFieldComp` `roiSelectorComp`
- **测试**：`tests/qml/EditView/tst_param_form.qml`
- **验收**：7 类型控件都可渲染

### M3.06 — 表单参数双向绑定
- **依赖**：M3.05
- **代码**：`qml/EditView/ParamForm.qml`
  - `property var currentParams: ({})`
  - 控件 `onValueChanged: currentParams[name] = value`
  - 反向：`onCurrentParamsChanged: 控件.value = currentParams[name]`
- **测试**：`tests/qml/EditView/tst_param_binding.qml`
- **验收**：双向同步

### M3.07 — PropertyPreviewPanel.qml 联动预览
- **依赖**：M3.06
- **代码**：`qml/EditView/PropertyPreviewPanel.qml`
  - 选中节点 → 加载 ParamForm
  - 显示 `bridge.getOperatorParams(nodeId)`
  - 修改 → `bridge.updateOperatorParams(nodeId, params)`
- **测试**：`tests/qml/EditView/tst_property_preview.qml`
- **验收**：选中节点实时显示

### M3.08 — OperatorEditorDialog.qml 模态详编
- **依赖**：M3.07
- **代码**：`qml/EditView/OperatorEditorDialog.qml`
  - `Dialog { modal: true; anchors.centerIn: parent }`
  - 复用 ParamForm
  - 应用/取消/重置按钮
- **测试**：`tests/qml/EditView/tst_operator_dialog.qml`
- **验收**：双击节点弹模态

### M3.09 — 算子参数校验
- **依赖**：M3.06
- **代码**：`src/UI/EditViewBridge.cpp`
  - `Q_INVOKABLE QStringList validateParam(QString type, QString name, QVariant value)`
  - 检查 min/max、enum 合法性、required
- **测试**：`tests/UI/test_param_validation.cpp`（边界值）
- **验收**：错误值返回错误列表

### M3.10 — ROI 选择器子控件
- **依赖**：M3.05
- **代码**：`qml/EditView/ROISelector.qml`（新增）
  - 弹出 ROI 编辑器（与图像叠加）
  - 返回 ROI 矩形 `(x, y, w, h)`
- **测试**：`tests/qml/EditView/tst_roi_selector.qml`
- **验收**：ROI 可选

### M3.11 — AiClassify 算子元数据特殊字段
- **依赖**：M3.04
- **代码**：`src/UI/OperatorDescriptors.cpp`
  - 类别标签列表（动态）
  - 模型路径选择（`FileDialog`）
  - 推理时调用 `VisionClassifier`
- **测试**：`tests/UI/test_ai_classify_meta.cpp`
- **验收**：AiClassify 面板可加载模型

### M3.12 — M3 集成 + 视觉对照
- **依赖**：M3.11
- **代码**：`tests/Integration/test_operator_panels.cpp`
  - 14 算子逐一截图
  - 与现有截图（属性 - 边缘检测）视觉对照
- **验收**：与现有风格一致

### M3.13 — 算子面板使用手册增量
- **依赖**：M3.12
- **代码**：`docs/QDetectVision_使用说明书.md` §6.3 编辑模块 v2.2
  - 描述 QML 画布
  - 14 算子参数说明
- **验收**：文档 review

---

## M4：JSON I/O + 50 步 Undo（10 任务，2 天）

### M4.01 — `.qdv` 文件格式设计
- **依赖**：M3.06
- **代码**：`docs/spec/qdv-file-format.md`
  - 完整 JSON schema
  - version 字段
- **验收**：文档 review

### M4.02 — EditViewBridge::saveToFile（异步）
- **依赖**：M4.01
- **代码**：`src/UI/EditViewBridge.cpp`
  - `Q_INVOKABLE void saveToFile(QUrl url)`
  - `QThreadPool::globalInstance()->start(runnable)`
  - 工作线程内：序列化 + 写盘
  - emit `savedSuccessfully(path)` / `saveFailed(reason)`
- **测试**：`tests/UI/test_save_load.cpp`
- **验收**：保存成功 + 失败信号

### M4.04 — EditViewBridge::loadFromFile
- **依赖**：M4.02
- **代码**：`src/UI/EditViewBridge.cpp`
  - 异步读 + 解析 JSON
  - 校验版本
  - emit `loadedSuccessfully()` / `loadFailed(reason)`
  - 重建 scheme + 节点 + 连线
- **测试**：`tests/UI/test_load.cpp`
- **验收**：加载还原

### M4.05 — Toast 通知「正在保存…」
- **依赖**：M4.02, M5.01
- **代码**：`qml/EditView/Main.qml` + `qml/EditView/Toast.qml`
  - 保存触发 toast (info)
  - 完成后转为 success/error
- **测试**：`tests/qml/EditView/tst_toast_save.qml`
- **验收**：toast 显示

### M4.06 — 6 类 UndoCommand
- **依赖**：无
- **代码**：`include/Core/UndoManager.h` `src/Core/UndoManager.cpp`
  - `NodeAddCommand` `NodeRemoveCommand` `NodeMoveCommand`
  - `NodeConnectCommand` `NodeDisconnectCommand` `NodePropertyChangeCommand`
- **测试**：`tests/Core/test_undo_commands.cpp`
- **验收**：6 类 command 撤销/重做正常

### M4.07 — 50 步 Undo 上限
- **依赖**：M4.06
- **代码**：`src/Core/UndoManager.cpp`
  - `m_undoStack->setUndoLimit(50)`
- **测试**：`tests/Core/test_undo_limit.cpp`（51 步测试）
- **验收**：栈底自动丢弃

### M4.08 — Ctrl+Z / Ctrl+Y 快捷键
- **依赖**：M4.06
- **代码**：`qml/EditView/Main.qml`
  - `Shortcut { sequence: "Ctrl+Z"; onActivated: bridge.undo() }`
  - `Shortcut { sequence: "Ctrl+Y"; onActivated: bridge.redo() }`
- **测试**：`tests/qml/EditView/tst_undo_shortcut.qml`
- **验收**：快捷键可用

### M4.09 — 撤销/重做按钮状态
- **依赖**：M4.06
- **代码**：`qml/EditView/Main.qml`
  - 工具栏按钮 `enabled: bridge.canUndo`
  - 订阅 `bridge.canUndoChanged`
- **测试**：`tests/qml/EditView/tst_undo_buttons.qml`
- **验收**：按钮 disabled 状态正确

### M4.10 — M4 集成测试
- **依赖**：M4.09
- **代码**：`tests/Integration/test_save_undo.cpp`
  - 创建方案 → 保存 → 加载 → 修改 → 撤销 51 次
  - JSON round-trip 一致
- **验收**：集成测试通过

---

## M5：UX 反馈（9 任务，1–2 天）

### M5.01 — Toast.qml 通知组件
- **依赖**：M2.04
- **代码**：`qml/EditView/Toast.qml`
  - 右上角堆叠 3 个
  - 4 类级别（info/warn/error/success）
  - 自动 4s 消失 + 淡入淡出动画
- **测试**：`tests/qml/EditView/tst_toast.qml`
- **验收**：toast 显示

### M5.02 — 全局 Toast 错误处理
- **依赖**：M5.01
- **代码**：`src/UI/EditViewBridge.cpp` + `qml/EditView/Main.qml`
  - 所有 `*Failed` 信号 → 调 `bridge.toast("error", ...)`
  - 错误信息格式：`[位置] 类型：描述 → 建议：xxx`
- **测试**：`tests/UI/test_bridge_toast.cpp`
- **验收**：错误显示规范

### M5.03 — 拖拽视觉反馈
- **依赖**：M2.11
- **代码**：`qml/EditView/FlowChartView.qml`
  - 拖拽中：`DropArea { onEntered: highlightColor = "#2196F3" }`（合法）
  - 非法位置：红色 `#F44336`
  - 放置成功：节点 `NumberAnimation` 弹性曲线（`Easing.OutBounce`，300ms）
  - 放置失败：`SequentialAnimation` 抖动
- **测试**：`tests/qml/EditView/tst_drag_feedback.qml`
- **验收**：视觉反馈完整

### M5.04 — 上下文帮助 HoverHelp.qml
- **依赖**：M3.05
- **代码**：`qml/EditView/HoverHelp.qml`
  - `Popup` 延迟 1500ms
  - 引用 `paramSpec.help`
- **测试**：`tests/qml/EditView/tst_hover_help.qml`
- **验收**：悬停 1.5s 弹出

### M5.05 — 图标差异化（灰度 vs 彩色 + 尺寸）
- **依赖**：M2.08, M2.10
- **代码**：`qml/EditView/ToolItem.qml` + `qml/EditView/FlowNode.qml`
  - 工具库：`sourceSize: Qt.size(32, 32)` + `ColorOverlay { color: "#888" }`
  - 节点：`sourceSize: Qt.size(40, 40)`（保留原色）
- **测试**：`tests/qml/EditView/tst_icon_diff.qml`
- **验收**：尺寸差 25%

### M5.06 — 操作成功/失败状态动画
- **依赖**：M5.03
- **代码**：`qml/EditView/FlowNode.qml`
  - `SequentialAnimation` 抖动（左右各 5px，3 次，100ms）
  - `NumberAnimation` 弹入（`Easing.OutBounce`，300ms）
- **测试**：`tests/qml/EditView/tst_state_anim.qml`
- **验收**：动画播放

### M5.07 — 全局错误日志（详细）
- **依赖**：M5.02
- **代码**：`src/Core/Logger.cpp`
  - 错误信息格式：`[时间戳] [模块] [级别] 位置 类型 描述 建议`
  - 写 `build/bin/logs/YYYYMMDD.log`
- **测试**：`tests/Core/test_logger_format.cpp`
- **验收**：日志格式规范

### M5.08 — M5 集成测试
- **依赖**：M5.07
- **代码**：`tests/Integration/test_ux_feedback.cpp`
  - 拖拽视觉反馈
  - 悬停帮助
  - toast 显示
  - 状态动画
- **验收**：集成通过

### M5.09 — 整体 UI 回归
- **依赖**：M5.08
- **代码**：`tests/Integration/test_full_ui.cpp`
  - 启动 QDV → 截图
  - 编辑模块 → 训练推理模块 → 截图
  - AI 推理回归测试
  - AI 模块 git diff = 0
- **验收**：AI 零侵入 + 全功能截图

---

## 任务依赖图（简化）

```
M1.01 → M1.02 → M1.03 → M1.04 → M1.05 → M1.06 → M1.07 → M1.08 → M1.10 → M1.11 → M1.12
                                                              ↘ M1.09

M2.01 → M2.02 → M2.03 → M2.04 → M2.05 → M2.06 → M2.07 → M2.08 → M2.09 → M2.10 → M2.11
                                                                                                 ↘ M2.12 → M2.13 → M2.14 → M2.15
                                                                                                  ↗ M5.01 (toast 提前做)

M3.01 → M3.02 → M3.04 → M3.05 → M3.06 → M3.07 → M3.08 → M3.12 → M3.13
              ↘ M3.03
                                                                       ↗ M3.09
                                                                       ↗ M3.10
                                                                       ↗ M3.11

M4.01 → M4.02 → M4.03 → M4.05 → M4.10
              ↘ M4.06 → M4.07 → M4.08 → M4.09
```

---

## 关键里程碑

| 里程碑 | 完成条件 | 验收物 |
|-------|---------|--------|
| M1 完成 | M1.12 通过 | MainWindow 截图 + Catch2 单元测试 |
| M2 完成 | M2.15 通过 | EditView QML 截图 + 60fps 性能报告 |
| M3 完成 | M3.13 通过 | 14 算子面板截图 + 使用手册 v2.2 |
| M4 完成 | M4.10 通过 | `.qdv` 示例文件 + Undo/Redo 测试报告 |
| M5 完成 | M5.09 通过 | 全功能截图 + AI 零侵入验证报告 |

---

## 任务统计

| 维度 | 数量 |
|------|------|
| 任务总数 | 59 |
| M1 | 12 |
| M2 | 15 |
| M3 | 13 |
| M4 | 10 |
| M5 | 9 |
| 预计总工期 | 13–18 工作日 |

---

> **下一步**：用户确认后，按 M1 → M2 → M3 → M4 → M5 顺序逐阶段实现，每阶段独立 commit + review。
