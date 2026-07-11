# 🔬 调试会话: edit-blank-screen-bug

**Session ID:** `edit-blank-screen-bug`  
**Start Time:** 2026-06-09  
**Status:** [OPEN]  
**Component:** 编辑模块（EditView / EditView.qml）

## 📋 问题描述

- **现象:** 点击顶部"编辑"标签页后，整个页面完全空白，无任何 UI 组件渲染
- **期望:** 应显示左侧算子库、中间画布、右侧算子属性面板、底部操作栏
- **影响范围:** 核心编辑模块，阻断所有方案编辑流程

## 🧪 假设列表（3-5个可证伪假设）

| # | 假设 | 可验证观测点 |
|---|------|-------------|
| H1 | **QML 资源路径解析失败** — `EditView.qrc` 资源注册时的 prefix/path 不匹配，导致 `qrc:/qml/EditView/Main.qml` 无法加载 | 检查 `QQuickWidget::setSource()` 返回值；看 QML 控制台错误 |
| H2 | **DesignTokens 单例注册失败** — `qmldir` 中 `singleton DesignTokens` 未正确注册，导致所有组件引用 `Tok.DesignTokens` 时崩溃 | 检查 `DesignTokens` 是否在任何组件中被引用但未注册 |
| H3 | **Main.qml 根组件布局问题** — `Rectangle` 未指定 `width/height`，或 `anchors.fill` 未生效，导致根组件尺寸为 0x0 | 检查根组件尺寸；查看是否有 `Component.onCompleted` 打印尺寸 |
| H4 | **EditViewBridge C++ 侧初始化异常** — 桥接对象未正确暴露到 QML 上下文，导致数据绑定失败 | 检查 `rootContext()->setContextProperty()` 调用顺序与时机 |
| H5 | **Qt Quick Scene Graph 渲染初始化失败** — OpenGL/场景图初始化失败，窗口创建但无渲染管线 | 检查 `QQuickWidget` 的 `clearColor`、`format` 设置 |

## 🔍 调查记录

### 阶段 1: 静态代码分析（预插桩）
*待补充...*

### 阶段 2: 运行时证据收集
*待补充...*

### 阶段 3: 根因确认
*待补充...*

## 🔧 修复记录
*待补充...*

## ✅ 验证结果
*待补充...*
