# Spec: EditView 编辑区域空白永久性修复

**版本**: 2.0（实施后修订版——记录实际根因与修复）
**日期**: 2026-07-07
**作者**: brainstorming 流程
**状态**: ✅ 已实施验证通过

---

## 1. 背景与根因分析

### 1.1 问题现象

用户反馈：点击编辑模块后，编辑区域内容显示为**完全空白（无任何 UI 元素）**。此问题已反复出现 5-10 次，每次"修复"后短期内复发。

### 1.2 排查方法

通过 `QT_LOGGING_RULES="*.debug=true;qt.*=true"` + `QT_FORCE_STDERR_LOGGING=1` 启动程序，重定向 stderr 到文件，捕获完整 QML 加载日志。

### 1.3 根因（双重失效，实施后修订）

#### 直接根因：Main.qml ScrollBar.vertical 语法错误

**原诊断（v1.0）**：`ScrollBar.vertical.policy` 挂在 Rectangle 上（只对 Flickable 有效）。
**实际根因（v2.0 修订）**：`ScrollBar.vertical.policy: ScrollBar.AsNeeded` 语法**在任何位置**（Rectangle 或 Flickable）都是错误的。

`ScrollBar.vertical` 是 QtQuick.Controls 的附加属性，返回一个 ScrollBar 对象引用。**在未显式赋值前，该引用为 null**，因此直接设 `.policy` 属性会触发 `Cannot set properties on vertical as it is null` 致命错误。

```qml
// 错误写法（v1.0 spec 误以为移入 Flickable 即可修复）
ScrollBar.vertical.policy: ScrollBar.AsNeeded   // ← vertical 是 null，致命错误

// 正确写法（参考 Main.qml:1490 已有用法）
ScrollBar.vertical: ScrollBar {
    policy: ScrollBar.AsNeeded
}
```

日志铁证（首次运行 stderr.log）：
```
[EditView] QML load FAILED. status= QQuickWidget::Error
   "qrc:/qml/EditView/Main.qml:2860:31: Cannot set properties on vertical as it is null"
```

二次验证（v1.0 修复后仍失败，错误覆盖层显示）：
```
⚠️ EditView QML 加载失败
[1] qrc:/qml/EditView/Main.qml:2841:35: Cannot set properties on vertical as it is null
```

**关键发现**：错误覆盖层机制在第二次运行时**成功捕获并显示了错误**，证明了根本根因修复的有效性。如果没有错误覆盖层，第二次失败又会是"空白无提示"。

#### 根本根因：QML 加载失败被静默吞掉

```cpp
// EditView.cpp 第 158-166 行（当前错误处理）
if (m_qmlCanvas->status() == QQuickWidget::Error) {
    const auto errors = m_qmlCanvas->errors();
    qWarning() << "[EditView] QML load FAILED. status=" << m_qmlCanvas->status();
    for (const auto& err : errors) {
        qWarning() << "  " << err.toString();
    }
}   // ← 只打印 qWarning，不阻断、不通知 UI、不重试、不弹窗
```

任何一处 QML 错误（语法/类型/缺失组件）都会导致整个界面空白，而错误信息只在 stderr 日志中，用户看到的是"空白"无任何提示。

### 1.4 历史修复模式验证

经 `git log --grep` 检索，历史 5-10 次修复均为"补 qmldir/qrc/qmlRegisterType 注册"：

| Commit | 修复内容 | 是否触及根本根因 |
|--------|---------|----------------|
| `2aafe7b9` | fix(editview): harden FilePathField injection - register in qmldir | ❌ |
| `4e0a7fec` | feat(editview): v3.2.0 算子参数编辑器增强（含 M8 修复空白） | ❌ |
| `1bb2afe2` | feat(qml): QML 资源 - EditView 模块化组件 | ❌ |

**结论**：历史修复从未触及错误处理缺陷，本次**不动这些注册**（它们已正确），只修复 2 处：ScrollBar 语法 + 错误可见化机制。

---

## 2. 修复方案

### 2.1 方案选择

采用**方案 B：错误可见化 + 修复语法**。

| 方案 | 改动范围 | 是否根除复发 | 是否过度 |
|------|---------|-------------|---------|
| A: 仅修 ScrollBar | 1 处 | ❌ 会复发 | 否 |
| **B: 错误可见化 + 修复语法** | 4 文件 | ✅ 永久根除 | 否 |
| C: 全面重构架构 | 数百文件 | ✅ | ❌ 违背 YAGNI |

### 2.2 架构改动

```
EditView (QWidget)
├── m_bridge (EditViewBridge)            [已有，不变]
├── m_qmlCanvas (QQuickWidget)           [已有，加载 Main.qml]
└── m_errorOverlay (QLabel)              [新增：QML加载失败时显示]
    ├── 绑定 m_qmlCanvas->statusChanged
    ├── status==Error → 显示错误详情 + 阻断空白
    ├── status==Ready → 隐藏
    └── 错误信息同步转发到 Logger + editViewBridge.errorRaised
```

---

## 3. 实施清单

### 3.1 修复 Main.qml ScrollBar.vertical 语法错误

**文件**: `qml/EditView/Main.qml` 第 2841 行

**原错误代码**：
```qml
Rectangle {
    Flickable { ... }
    ScrollBar.vertical.policy: ScrollBar.AsNeeded   // ← 错误：vertical 为 null
}
```

**v1.0 尝试（仍失败）**：移入 Flickable 内部——`ScrollBar.vertical.policy` 语法本身错误，位置无关。

**v2.0 最终修复**：使用显式 ScrollBar 对象赋值（与 Main.qml:1490 已有正确用法一致）：
```qml
Rectangle {
    Flickable {
        anchors.fill: parent
        clip: true
        contentWidth: width
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
        ColumnLayout { ... }
    }
}
```

### 3.2 增加 QML 加载失败可见化机制

**文件**: `include/UI/EditView.h`

新增成员与槽：
```cpp
class EditView : public QWidget {
    // ...
private slots:
    void onQmlStatusChanged(QQuickWidget::Status status);
private:
    QQuickWidget* m_qmlCanvas = nullptr;
    EditViewBridge* m_bridge = nullptr;
    QLabel* m_errorOverlay = nullptr;   // ← 新增：错误覆盖层
    void setupErrorOverlay();           // ← 新增：初始化覆盖层
    void showErrorOverlay(const QString& errorText);
    void hideErrorOverlay();
};
```

**文件**: `src/UI/EditView.cpp`

`setupQmlCanvas` 改造（仅增加错误处理，不改加载逻辑）：
```cpp
void EditView::setupQmlCanvas(QWidget* parent) {
    // ...现有代码不变...
    m_qmlCanvas = new QQuickWidget();
    // ...setContextProperty / Q_INIT_RESOURCE / registerQmlTypesForEditView...

    setupErrorOverlay();   // ← 新增：创建错误覆盖层

    // 连接 statusChanged（替代当前的 if 判断）
    connect(m_qmlCanvas, &QQuickWidget::statusChanged,
            this, &EditView::onQmlStatusChanged);

    m_qmlCanvas->setSource(mainQmlUrl);
    // 不再在此处 if 判断 status——交给 statusChanged 槽统一处理
}
```

新增错误覆盖层实现：
```cpp
void EditView::setupErrorOverlay() {
    m_errorOverlay = new QLabel(this);
    m_errorOverlay->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_errorOverlay->setStyleSheet(
        "QLabel { background-color: #2a1a1a; color: #ff6b6b; "
        "font-family: Consolas, 'Microsoft YaHei UI'; font-size: 12px; "
        "padding: 12px; }");
    m_errorOverlay->setWordWrap(true);
    m_errorOverlay->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_errorOverlay->hide();
}

void EditView::onQmlStatusChanged(QQuickWidget::Status status) {
    if (status == QQuickWidget::Ready) {
        hideErrorOverlay();
        qDebug() << "[EditView] QML loaded successfully";
    } else if (status == QQuickWidget::Error) {
        const auto errors = m_qmlCanvas->errors();
        QString errorText = QStringLiteral("⚠️ EditView QML 加载失败\n\n");
        for (int i = 0; i < errors.size() && i < 5; ++i) {
            errorText += QStringLiteral("[%1] %2\n").arg(i + 1).arg(errors[i].toString());
        }
        if (errors.size() > 5) {
            errorText += QStringLiteral("\n... 共 %1 条错误，选中可复制\n").arg(errors.size());
        }
        showErrorOverlay(errorText);

        // 转发到 Logger
        QDV::Logger::error(QStringLiteral("[EditView] QML load FAILED:"));
        for (const auto& err : errors) {
            QDV::Logger::error(QStringLiteral("  ") + err.toString());
        }

        // 转发到 editViewBridge.errorRaised（QML 端可显示 Toast）
        if (m_bridge) {
            emit m_bridge->errorRaised("QML_LOAD", errorText);
        }
    }
}

void EditView::showErrorOverlay(const QString& errorText) {
    m_errorOverlay->setText(errorText);
    m_errorOverlay->setGeometry(this->rect());
    m_errorOverlay->raise();
    m_errorOverlay->show();
}

void EditView::hideErrorOverlay() {
    m_errorOverlay->hide();
}
```

### 3.3 测试防回归

**文件**: `tests/UI/test_ui_smoke.cpp`（或新增 `test_editview_qml_load.cpp`）

新增用例：
```cpp
TEST_CASE("EditView QML loads without error", "[ui][editview]") {
    EditView editView;
    // 等待 QML 加载完成（statusChanged 是异步信号，但 setSource 同步触发首次）
    QQuickWidget* qml = editView.findChild<QQuickWidget*>();
    REQUIRE(qml != nullptr);
    REQUIRE(qml->status() == QQuickWidget::Ready);   // ← 若加载失败，测试失败并打印错误
}
```

### 3.4 改动文件清单

| 文件 | 改动类型 | 行数估计 |
|------|---------|---------|
| `qml/EditView/Main.qml` | 修复 ScrollBar 语法（1 行移位） | ~3 |
| `include/UI/EditView.h` | 新增成员 + 槽声明 | ~5 |
| `src/UI/EditView.cpp` | 新增 setupErrorOverlay + onQmlStatusChanged + 替换原 if 判断 | ~40 |
| `tests/UI/test_ui_smoke.cpp` | 新增 QML 加载状态验证用例 | ~10 |

**总改动**: 约 60 行，4 个文件。不动 qmldir/qrc/qmlRegisterType（已正确）。

---

## 4. 验收标准

### 4.1 技术层
1. ✅ 编译通过
2. ✅ 全部单元测试通过（含新增 QML 加载状态用例）
3. ✅ 启动程序，编辑模块显示完整 UI（三栏布局 + 顶栏 + 画布）

### 4.2 业务层
1. ✅ 算子库显示全部算子（mock + 内置）
2. ✅ 任意 mock 算子可拖拽执行
3. ✅ 未来若 QML 出现任何错误，显示明确错误覆盖层（而非空白）

### 4.3 用户层（永久性根除）
1. ✅ "空白无提示"复发模式永久根除——任何 QML 错误都会显示错误覆盖层
2. ✅ 错误信息可复制、可观测（Logger + Toast）

---

## 5. 风险与缓解

| 风险 | 缓解 |
|------|------|
| m_errorOverlay 覆盖 QML 导致交互阻断 | status==Ready 时立即 hide()；错误时仍允许切换其他视图 |
| statusChanged 信号时序 | setSource 同步触发首次 statusChanged，无需额外等待 |
| 测试中 QML 异步加载 | setSource 后立即检查 status（Qt 同步加载 qrc 资源） |

---

## 6. 实施顺序

1. 修复 Main.qml ScrollBar.vertical 语法（立即消除当前空白）
2. 修改 EditView.h/cpp 增加错误覆盖层（根本根因）
3. 新增测试用例（防回归）
4. 编译 + 运行测试 + 启动程序验证
5. 验收：编辑模块显示完整 UI + 错误覆盖层机制生效

---

## 7. 实施验证结果（v2.0 新增）

### 7.1 编译

```
[100%] Built target QDetectVision
[100%] Built target QDV_tests
```
编译 100% 通过，无警告无错误。

### 7.2 单元测试

```
[PASS] EditView QML loads without error
Total: 351 | Passed: 351 | Failed: 0
```
新增的 `EditView QML loads without error` 测试通过，351/351 全绿。

### 7.3 运行时验证（错误覆盖层机制）

**第一次运行**（v1.0 修复，ScrollBar.vertical.policy 语法仍错误）：
错误覆盖层成功显示：
```
⚠️ EditView QML 加载失败
[1] qrc:/qml/EditView/Main.qml:2841:35: Cannot set properties on vertical as it is null
```
→ 证明了错误覆盖层机制的有效性——如果没有它，这又会是"空白无提示"。

**第二次运行**（v2.0 最终修复，ScrollBar.vertical: ScrollBar {} 显式赋值）：
```
NO ERROR OVERLAY - QML loaded successfully!
```
错误覆盖层未出现，QML 加载成功。

### 7.4 UI 元素验证（UI Automation 树）

EditView 显示完整 UI，包含以下元素：
- **算子库**：几何变换、图像变换、图像合并、仿射变换、极坐标变换、几何测量、线到线角度、点到点距离
- **流程图编辑区**：画布提示"双击左侧算子库中的算子添加到画布"
- **图片预览**："等待图像输入"
- **变量管理**：图像变量、控制变量
- **日志/调试**："运行方案后将在此显示日志和调试信息"

### 7.5 截图色彩分析

- 修复前（空白）：1 种颜色（全黑 0,0,0）
- v1.0 修复后（错误覆盖层）：34 种颜色（错误文本 + 背景）
- v2.0 最终修复后：**84 种颜色**（丰富 UI 内容，多层级深色主题面板）

### 7.6 改动文件清单（实际）

| 文件 | 改动 | 行数 |
|------|------|------|
| `qml/EditView/Main.qml` | ScrollBar.vertical 显式赋值 | ~5 |
| `include/UI/EditView.h` | m_errorOverlay + onQmlStatusChanged 槽 | ~10 |
| `src/UI/EditView.cpp` | setupErrorOverlay + onQmlStatusChanged + 3 辅助方法 | ~55 |
| `tests/UI/test_ui_smoke.cpp` | EditView QML 加载状态验证用例 | ~12 |
| **总计** | **4 文件** | **~82 行** |
