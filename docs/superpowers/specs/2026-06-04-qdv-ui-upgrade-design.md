# QDV 界面升级设计文档

> **版本**：v2.1.0（设计稿）  
> **设计日期**：2026-06-04  
> **设计范围**：MainWindow 增强 + EditView 重构（QML） + 14 算子参数面板 + UX 反馈  
> **不涉及**：`include/AI/` `src/AI/` `training/` `models/`（AI 模型推理模块零侵入）

---

## 1. 目标与边界

### 1.1 设计目标
1. **窗口控制专业级**：min/max/close + 8 方向拖拽 + 32px 标题栏（项目名/版本号）
2. **EditView 重构**：QGraphicsView 风格的流程图编辑器（QML 实现）+ 14 算子真实参数面板
3. **交互现代化**：toast 通知、上下文帮助气泡、拖拽视觉反馈、60fps 缩放/平移
4. **撤销/重做 50 步**：支持 Ctrl+Z / Ctrl+Y
5. **文件 I/O**：标准 JSON 格式 `.qdv` 保存/加载

### 1.2 边界
- ✅ 可修改：`include/UI/` `src/UI/` `qml/` `include/Core/UndoManager.h` `src/Core/UndoManager.cpp`
- ❌ **不可修改**：`include/AI/` `src/AI/` `training/` `models/` `include/Vision/` `src/Vision/`（AI/Vision 算子已稳定）
- ✅ 可新增：`src/UI/EditViewBridge.*` `src/UI/OperatorDescriptors.cpp` `tests/UI/*` `tests/qml/*`

### 1.3 不在本期
- CameraView / IOView / CommView / MonitorView / TrainingInferenceView 仍为 QWidget
- CentralWindow 框架不改
- LoginView 不改
- AI 模型推理相关代码全部不动

---

## 2. 关键决策（已与用户确认）

| 决策项 | 选择 |
|--------|------|
| 算子参数规范来源 | 按 QDV 现有 14 个真实算子（src/Vision/*Tool.cpp）反推 |
| 交付策略 | 分 5 阶段（M1 窗口 → M2 EditView → M3 算子面板 → M4 I/O+Undo → M5 UX） |
| 参数面板模式 | 双模式（选中→右侧预览 + 双击→模态详编） |
| 视觉风格 | 沿用现有暗色主题（#1E1E1E 背景 + #9C27B0 紫色强调） |
| EditView 技术栈 | Qt Quick (QML) + QQuickWidget 嵌入 CentralWindow |
| QML 集成边界 | 仅 EditView 改 QML；其他视图保持 QWidget |

---

## 3. 总体架构

### 3.1 文件结构（新增/修改）
```
e:\anchor\Trae\QDV\
├── qml/                                    ← 新增
│   └── EditView/
│       ├── Main.qml                        根视图（顶栏 + 三栏布局）
│       ├── ToolLibraryPanel.qml            左侧工具库（可折叠分类）
│       ├── ToolItem.qml                    工具库项
│       ├── FlowChartView.qml               中央画布
│       ├── FlowNode.qml                    节点
│       ├── FlowConnection.qml              连线
│       ├── PropertyPreviewPanel.qml        右侧联动预览
│       ├── OperatorEditorDialog.qml        模态详编
│       ├── ParamForm.qml                   动态表单（共用）
│       ├── HoverHelp.qml                   上下文帮助气泡
│       └── Toast.qml                       通知
├── src/UI/                                 ← 修改
│   ├── MainWindow.{h,cpp}                  增量：8 方向拖拽 + 标题栏项目名版本号
│   ├── EditViewBridge.{h,cpp}              ← 新增：C++ ↔ QML 桥
│   └── OperatorDescriptors.cpp             ← 新增：14 算子元数据
├── include/Core/                           ← 修改
│   └── UndoManager.h                       增量：6 类新 Command
├── src/Core/                               ← 修改
│   └── UndoManager.cpp
├── tests/UI/                               ← 新增
│   └── test_edit_view_bridge.cpp
└── tests/qml/EditView/                     ← 新增
    ├── tst_flow_node.qml
    ├── tst_flow_connection.qml
    └── tst_undo_redo.qml
```

### 3.2 模块依赖图
```
┌─────────────────────────────────────────────────────┐
│  EditViewBridge (C++ 单点入口)                      │
│  ┌──────────────────────────────────────────────┐  │
│  │ Q_PROPERTY 暴露：                            │  │
│  │   operatorList / currentSchemeNodes / ...  │  │
│  │ Q_INVOKABLE 槽：                             │  │
│  │   addOperator / moveNode / connectNodes /  │  │
│  │   updateOperatorParams / saveToFile / ...   │  │
│  └──────────────────────────────────────────────┘  │
└─────┬──────────┬──────────┬──────────┬─────────────┘
      │          │          │          │
      ▼          ▼          ▼          ▼
┌─────────┐ ┌─────────┐ ┌─────────┐ ┌────────────┐
│ Scheme  │ │ Tool    │ │ Undo    │ │ Operator   │
│ Manager │ │ Factory │ │ Manager │ │ Meta       │
└─────────┘ └─────────┘ └─────────┘ └────────────┘
   已存在      已存在      已存在      新增
```

### 3.3 AI 模块零侵入验证
```bash
# 实施完成后必须执行的检查
git diff --name-only HEAD~1 HEAD | grep -E '^(include/AI|src/AI|training|models)/' | wc -l
# 期望输出：0

# 推理回归测试
./build/bin/QDV_tests.exe --gtest_filter="AI.*"
./build/bin/QDV_tests.exe --test-case="AIInference*"
```

---

## 4. M1：MainWindow 增强（2–3 天）

### 4.1 标题栏规范
| 元素 | 规格 |
|------|------|
| 高度 | 32px |
| 字体 | 14pt Segoe UI / 微软雅黑 无衬线 |
| 内容 | 左：`QDV · <项目名> v<版本号>`；右：最小化/最大化/关闭 |
| 颜色 | 背景 #1E1E1E；文字 #FFFFFF；强调色 #9C27B0（仅项目名前缀） |
| 拖拽 | 左键双击 = 最大化/还原；左键单击（图标区）= 同上 |

### 4.2 8 方向拖拽实现
```cpp
// src/UI/MainWindow.cpp
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
    if (eventType == "windows_generic_MSG" || eventType == "WM_NCHITTEST") {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST) {
            const int BORDER = 8;  // 8px 边缘检测
            QPoint pos = QPoint(LOWORD(msg->lParam), HIWORD(msg->lParam)) 
                        - geometry().topLeft();
            bool left = pos.x() < BORDER;
            bool right = pos.x() > width() - BORDER;
            bool top = pos.y() < BORDER;
            bool bottom = pos.y() > height() - BORDER;
            
            if (top && left)        { *result = HTTOPLEFT; return true; }
            if (top && right)       { *result = HTTOPRIGHT; return true; }
            if (bottom && left)     { *result = HTBOTTOMLEFT; return true; }
            if (bottom && right)    { *result = HTBOTTOMRIGHT; return true; }
            if (left)               { *result = HTLEFT; return true; }
            if (right)              { *result = HTRIGHT; return true; }
            if (top)                { *result = HTTOP; return true; }
            if (bottom)             { *result = HTBOTTOM; return true; }
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
```

### 4.3 边界限制
| 限制 | 值 |
|------|------|
| 最小宽度 | 800 |
| 最小高度 | 600 |
| 屏幕边缘吸附 | enable（`QRect::adjusted(20, 20, -20, -20)` 避免完全遮盖任务栏） |
| 平滑重绘 | `setUpdatesEnabled(false)` 包住 resize，结束 `update()` |

### 4.4 响应时间 < 100ms
- 拖拽状态：`QTimer::singleShot(0, this, [this](){ update(); })` 异步
- 重绘用 `QWidget::update()`（合并多个 paint 事件）

---

## 5. M2：QML EditView（5–7 天）

### 5.1 Main.qml 布局
```qml
ApplicationWindow {
    width: 1280; height: 800
    visible: true
    
    menuBar: ToolBar {
        RowLayout {
            ToolButton { text: "新建" }
            ToolButton { text: "保存"; onClicked: bridge.saveToFile(...) }
            ToolButton { text: "加载" }
            ToolSeparator {}
            ToolButton { text: "撤销"; enabled: bridge.canUndo; onClicked: bridge.undo() }
            ToolButton { text: "重做"; enabled: bridge.canRedo; onClicked: bridge.redo() }
        }
    }
    
    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal
        
        ToolLibraryPanel { width: 240 }    // 左侧
        
        FlowChartView { }                   // 中央（自适应）
        
        PropertyPreviewPanel { width: 320 } // 右侧
    }
    
    OperatorEditorDialog { id: editorDialog }  // 模态
    Toast { id: toast }
    
    Connections {
        target: bridge
        function onToast(level, title, msg) { toast.show(level, title, msg) }
    }
}
```

### 5.2 FlowChartView.qml 关键能力
| 能力 | 实现 |
|------|------|
| 缩放 0.5x–2x | `PinchHandler` + 鼠标滚轮 `WheelHandler { onWheel: ... }`；缩放中心 = 鼠标当前位置 |
| 平移 | 中键 `DragHandler`；也可按住 Space + 左键 |
| 节点拖拽 | `DragHandler` + `property real savedX/Y`（用于撤销） |
| 连线创建 | 从节点输出端口 `MouseArea` 按下→拖→释放到目标输入端口；路径用 `QPainterPath` 贝塞尔 |
| 连线选中/删除 | 单击选中（高亮）；双击弹属性；右键菜单 `Menu { MenuItem { text: "删除" } }` |
| 选中节点 | 单击节点（`TapHandler`）→ 通知 PropertyPreviewPanel |
| 双击节点 | `TapHandler { onDoubleTapped: editorDialog.open(nodeId) }` |
| 60fps | 节点数 < 100 时开启 `smooth: true`；> 100 时关闭 |

### 5.3 FlowNode.qml 模板
```qml
Rectangle {
    id: node
    property string nodeId
    property string operatorType
    property var params: ({})
    width: 120; height: 60
    color: "#2D2D2D"
    border.color: selected ? "#9C27B0" : "#555"
    border.width: selected ? 2 : 1
    
    Column {
        Image { source: "qrc:/icons/" + operatorType + ".png"; sourceSize: Qt.size(40, 40) }  // 40px 彩色
        Text { text: operatorDisplayName; color: "#FFF" }
    }
    
    // 端口
    Rectangle { x: -6; y: 20; width: 12; height: 12; radius: 6; color: "#9C27B0" }  // 输入
    Rectangle { x: width-6; y: 20; width: 12; height: 12; radius: 6; color: "#4CAF50" } // 输出
}
```

### 5.4 ToolLibraryPanel.qml 折叠分类
```qml
ListView {
    model: bridge.operatorCategories  // ["输入", "预处理", ...]
    delegate: Item {
        Column {
            Row {  // 分类标题（点击折叠）
                Text { text: modelData }
                Image { source: expanded ? "down" : "right" }
            }
            ListView {  // 子项
                visible: expanded
                model: bridge.operatorsInCategory(modelData)
                delegate: ToolItem { operatorType: modelData }
            }
        }
    }
}
```

### 5.5 拖拽从工具库到画布
```qml
// FlowChartView.qml 的 DropArea（接收端）
DropArea {
    id: dropArea
    anchors.fill: parent
    onDropped: {
        var source = drop.source
        if (source && source.operatorType) {
            bridge.addOperator(source.operatorType, drop.x, drop.y)
            drop.accept(Qt.CopyAction)
        }
    }
    onEntered: canvasFrame.border.color = "#2196F3"  // 合法高亮
    onExited:  canvasFrame.border.color = "transparent"
}

// ToolItem.qml（拖拽源端）
Item {
    id: toolItem
    property string operatorType
    Drag.active: dragArea.drag.active
    Drag.hotSpot.x: 16; Drag.hotSpot.y: 16
    
    Image {
        source: "qrc:/icons/" + toolItem.operatorType + ".png"
        sourceSize: Qt.size(32, 32)
        opacity: dragArea.drag.active ? 0.6 : 1.0
        ColorOverlay { color: "#888" }  // 灰度化
    }
    
    MouseArea { id: dragArea; drag.target: toolItem; cursorShape: Qt.OpenHandCursor }
}
```

---

## 6. M3：算子参数面板（3–4 天）

### 6.1 算子元数据结构
```cpp
// src/UI/OperatorDescriptors.h
struct ParamSpec {
    enum Type { Int, Float, Enum, Bool, String, ROI, Vector };
    QString name;        // 内部名（如 "threshold"）
    QString cnName;      // 中文名（如 "阈值"）
    Type type;
    QVariant defaultValue;
    QVariant minValue, maxValue, step;
    QStringList options; // enum 用
    QString help;        // 帮助文本
    QString unit;        // 单位（如 "px"）
};

struct OperatorMeta {
    QString type;        // "ImagePreprocess" 等
    QString cnName;      // "图像预处理"
    QString category;    // "预处理"
    QString iconPath;    // "qrc:/icons/preprocess.png"
    QList<ParamSpec> params;
};

class OperatorDescriptors {
public:
    static QList<OperatorMeta> all();
    static OperatorMeta get(const QString& type);
};
```

### 6.2 14 个算子元数据
| 类型 | 中文名 | 关键参数 |
|------|--------|----------|
| ReadImage | 读图 | filePath, colorMode |
| ImagePreprocess | 图像预处理 | denoise, morphology, kernelSize |
| Threshold | 阈值 | threshold, maxValue, method |
| EdgeDetect | 边缘检测 | lowThreshold, highThreshold, apertureSize |
| BlobDetect | Blob 检测 | minArea, maxArea, minCircularity, maxCircularity |
| ColorDetect | 颜色检测 | hMin, hMax, sMin, sMax, vMin, vMax |
| ContourAnalyze | 轮廓分析 | minArea, maxArea, filterByArea |
| TemplateMatch | 模板匹配 | templatePath, threshold, matchMethod |
| GeometryMeasure | 几何测量 | measureType, minThreshold, maxThreshold, pixelScale |
| LineCircleDetect | 线圆检测 | detectType, rho, theta, threshold, minLineLength, minRadius, maxRadius |
| ImageArithmetic | 图像算术 | operation, scalar, useScalar |
| ImageTransform | 图像变换 | transformType, angle, scaleX, scaleY, targetWidth, targetHeight |
| ImageMerge | 图像合并 | mergeType |
| BranchControl | 分支控制 | condition, trueBranch, falseBranch |
| AiClassify | AI 分类 | modelPath, confidenceThreshold, topK, inputWidth, inputHeight, categoryLabels |

### 6.3 ParamForm.qml 动态生成
```qml
Repeater {
    model: paramSpecs
    delegate: Loader {
        sourceComponent: {
            switch (modelData.type) {
                case ParamSpec.Int: return spinBoxComp
                case ParamSpec.Float: return doubleSpinBoxComp
                case ParamSpec.Enum: return comboBoxComp
                case ParamSpec.Bool: return checkBoxComp
                case ParamSpec.String: return textFieldComp
                case ParamSpec.ROI: return roiSelectorComp
            }
        }
        onLoaded: item.bindSpec(modelData, currentValue)
    }
}
```

### 6.4 模态详编 OperatorEditorDialog.qml
- 复用 `ParamForm.qml`
- 大字号（14pt）
- 帮助气泡（`HoverHelp.qml` 引用 `paramSpec.help`）
- 「默认值」按钮（重置单个参数）
- 「重置全部」按钮
- 「应用」+「取消」

---

## 7. M4：JSON I/O + Undo/Redo（2 天）

### 7.1 `.qdv` 文件格式
```json
{
  "version": "2.1.0",
  "scheme": {
    "name": "零件尺寸检测",
    "createdAt": "2026-06-04T10:00:00"
  },
  "nodes": [
    {
      "id": "n1",
      "type": "ImagePreprocess",
      "x": 100, "y": 100,
      "params": {"denoise": true, "kernelSize": 5}
    }
  ],
  "connections": [
    {"id": "c1", "from": "n1", "to": "n2", "fromPort": 0, "toPort": 0}
  ]
}
```

### 7.2 保存/加载（异步）
```cpp
// EditViewBridge::saveToFile
void EditViewBridge::saveToFile(const QString& url) {
    auto* runnable = new QRunnable();
    runnable->setAutoDelete(true);
    QObject::connect(runnable, &QRunnable::destroyed, this, [this, url](){
        // 工作线程完成后 emit
    });
    QThreadPool::globalInstance()->start(runnable);
}
```
- UI 立即 toast「正在保存...」
- 工作线程完成 → emit `savedSuccessfully(path)` 或 `saveFailed(reason)`
- toast 转为「保存成功 / 失败」

### 7.3 Undo/Redo 50 步
```cpp
// src/UI/EditViewBridge.cpp
EditViewBridge::EditViewBridge() {
    m_undoStack = UndoManager::instance()->stack();
    m_undoStack->setUndoLimit(50);
    connect(m_undoStack, &QUndoStack::canUndoChanged, this, &EditViewBridge::canUndoChanged);
    connect(m_undoStack, &QUndoStack::canRedoChanged, this, &EditViewBridge::canRedoChanged);
}
```

### 7.4 6 类 UndoCommand（增量）
| Command | 撤销 | 重做 |
|---------|------|------|
| NodeAddCommand | 删除节点 | 添加节点 |
| NodeRemoveCommand | 添加节点（含原参数） | 删除节点 |
| NodeMoveCommand | 恢复旧坐标 | 设置新坐标 |
| NodeConnectCommand | 断开连线 | 创建连线 |
| NodeDisconnectCommand | 创建连线 | 断开连线 |
| NodePropertyChangeCommand | 恢复旧参数 | 应用新参数 |

### 7.5 快捷键
```qml
Shortcut { sequence: "Ctrl+Z"; onActivated: bridge.undo() }
Shortcut { sequence: "Ctrl+Y"; onActivated: bridge.redo() }
Shortcut { sequence: "Ctrl+S"; onActivated: bridge.saveToFile(currentFile) }
Shortcut { sequence: "Ctrl+O"; onActivated: bridge.loadFromFileDialog() }
```

---

## 8. M5：UX 反馈（1–2 天）

### 8.1 Toast 通知
```qml
// Toast.qml
Rectangle {
    property string level: "info"  // info/warn/error/success
    property string title: ""
    property string message: ""
    color: {
        "error": "#F44336";
        "warn": "#FF9800";
        "info": "#2196F3";
        "success": "#4CAF50";
    }[level]
    
    width: 320; height: 80
    radius: 4
    anchors.top: parent.top
    anchors.right: parent.right
    anchors.margins: 16
    
    Behavior on opacity { NumberAnimation { duration: 200 } }
    
    Timer { interval: 4000; onTriggered: opacity = 0 }
}
```

错误信息格式：`[位置] 类型：描述 → 建议：xxx`
例：`[EditView / saveToFile] IO 错误：磁盘空间不足 → 建议：清理 build/bin/logs/ 旧日志后重试`

### 8.2 拖拽视觉反馈
| 阶段 | 视觉 |
|------|------|
| 拖拽中 | `Drag.active` + 半透明 `opacity: 0.6` 副本跟随鼠标 |
| 合法放置 | 画布高亮蓝色边框（2px #2196F3） |
| 非法放置 | 画布高亮红色边框（2px #F44336）+ 抖动回弹 |
| 放置成功 | 节点 `NumberAnimation` 弹性曲线落入（`Easing.OutBounce`，300ms） |
| 放置失败 | 抖动 `SequentialAnimation`（左右各 5px，3 次） |

### 8.3 上下文帮助气泡 HoverHelp.qml
```qml
Popup {
    property string helpText: ""
    delay: 1500          // 悬停 1.5s 后弹出
    timeout: 5000
    padding: 8
    contentItem: Text {
        text: helpText
        wrapMode: Text.Wrap
        color: "#FFF"
    }
    background: Rectangle { color: "#424242"; radius: 4 }
}
```

### 8.4 图标差异化
| 位置 | 图标尺寸 | 颜色 |
|------|----------|------|
| 工具库 | 32×32 | 灰度（`ColorOverlay { color: "#888" }`） |
| 画布节点 | 40×40 | 彩色（保留原色） |
| 尺寸差 | 25% | > 需求 20% |
| 背景 | 工具库：白；画布节点：主题色 | — |

---

## 9. 数据流

### 9.1 用户拖拽算子到画布
```
ToolItem (拖拽)
   │ Drag.drop()
   ▼
FlowChartView.onDropped
   │ drop.getDataAsString("operator/type")
   │ x = drop.x, y = drop.y
   ▼
EditViewBridge::addOperator(type, x, y)
   │ 1. create VisionTool via ToolFactory
   │ 2. push NodeAddCommand to UndoStack
   │ 3. m_scheme->addTool(tool)
   │ 4. emit currentSchemeNodesChanged
   ▼
QML 重新渲染
   │ currentSchemeNodes
   ▼
新 FlowNode 出现（弹性动画）
```

### 9.2 修改参数
```
FlowNode 双击
   │ TapHandler.onDoubleTapped
   ▼
OperatorEditorDialog.open(nodeId)
   │ bridge.getOperatorParams(nodeId)
   ▼
用户修改 → ParamForm.onValueChanged
   │ bridge.updateOperatorParams(nodeId, newParams)
   │ 1. push NodePropertyChangeCommand
   │ 2. m_tool->setParams(newParams)
   │ 3. emit currentSchemeNodesChanged
   ▼
FlowNode 显示更新
```

### 9.3 保存
```
Ctrl+S / 「保存」按钮
   │ bridge.saveToFile(url)
   ▼
QThreadPool::start(runnable)  // 工作线程
   │ toast "正在保存..."
   │
   ▼ (工作线程)
QFile + QJsonDocument
   │ emit savedSuccessfully(path) / saveFailed(reason)
   ▼
QML Toast
   │ "保存成功" / "保存失败: ..."
```

---

## 10. 测试策略

### 10.1 单元测试
| 文件 | 覆盖 |
|------|------|
| `tests/UI/test_edit_view_bridge.cpp` | 14 算子元数据；JSON 解析；6 类 Command 撤销/重做；信号触发 |
| `tests/UI/test_operator_descriptors.cpp` | 元数据完整性（所有 ParamSpec 字段非空） |

### 10.2 QML 测试
| 文件 | 覆盖 |
|------|------|
| `tests/qml/EditView/tst_flow_node.qml` | 节点位置/选中/双击 |
| `tests/qml/EditView/tst_flow_connection.qml` | 贝塞尔路径计算；端点吸附 |
| `tests/qml/EditView/tst_undo_redo.qml` | 50 步上限；Ctrl+Z/Y 触发 |

### 10.3 集成测试
| 场景 | 验证 |
|------|------|
| 拖拽 1 个 ImagePreprocess → 修改 denoise → Ctrl+Z | 参数恢复 |
| 拖拽 1 个 ImagePreprocess + 1 个 Threshold → 连线 → 保存 | JSON 含 2 节点 1 连线 |
| 加载 → 拖入新节点 → Ctrl+Z | 新节点消失 |
| 连续 51 次操作 | 第 51 次撤销后栈底被丢弃（仅保留 50 步） |

### 10.4 性能测试
| 指标 | 工具 | 目标 |
|------|------|------|
| 60fps 缩放/平移 | `QQuickWindow::frameSwapped` 计数 | ≥ 60 fps（< 100 节点） |
| 拖拽响应 | `QElapsedTimer` | < 100ms |
| 撤销/重做 | `QElapsedTimer` | < 16ms |
| 加载 100 节点 | `QElapsedTimer` | < 200ms |

### 10.5 AI 模块零侵入
```bash
# 必跑
git diff --name-only HEAD~5 HEAD~0 | grep -E '^(include/AI|src/AI|training|models)/'
# 期望：空

# 推理回归
./build/bin/QDV_tests.exe --test-case="*AI*"
# 期望：全部通过，推理时间差 < 5%
```

### 10.6 覆盖率
- 仅统计新增代码（`src/UI/EditViewBridge.*` `src/UI/OperatorDescriptors.cpp` `qml/EditView/*`）
- 目标 ≥ 80%
- 工具：gcov/lcov（C++）+ qml-coverage（QML）

---

## 11. 风险与缓解

| 风险 | 影响 | 概率 | 缓解 |
|------|------|------|------|
| QML ↔ C++ 类型转换 bug | 高 | 中 | EditViewBridge 单点入口；C++→QML 只用 QVariantList/QVariantMap |
| 撤销 50 步内存膨胀 | 中 | 低 | Command 仅存差量（坐标/参数），不存整图快照 |
| 60fps 性能瓶颈 | 中 | 中 | > 100 节点时关闭 smooth；用 `QQuickItem::setRenderTarget` |
| QML 调试困难 | 中 | 中 | QML console.log + C++ qDebug 双日志；开发期开启 QML 调试器 |
| 暗色主题适配所有图标 | 中 | 中 | 工具库图标统一灰度 overlay；节点图标保留原色 |
| CentralWindow 嵌入 QQuickWidget 闪烁 | 中 | 低 | 启动时 `setClearColor(Qt::transparent)`；`setAttribute(Qt::WA_TranslucentBackground)` |
| Windows 11 Snap 冲突 | 低 | 中 | 关闭 8 方向拖拽的某些方向（与系统 Snap 冲突的） |

---

## 12. 验收标准

### 12.1 M1 窗口
- [ ] 标题栏 32px；项目名/版本号实时显示
- [ ] 8 方向拖拽可用；最小 800×600
- [ ] min/max/close 按钮功能正常，< 100ms
- [ ] 双击标题栏 = 最大化/还原

### 12.2 M2 EditView
- [ ] QML 嵌入 CentralWindow 无闪烁
- [ ] 工具库 14 算子分 11 类（按 OperatorDescriptors::category）
- [ ] 拖拽流畅（60fps 测得 ≥ 55fps）
- [ ] 缩放 0.5x–2x；平移中键
- [ ] 节点拖动 + 连线创建 + 删除均可用
- [ ] 双击节点弹模态

### 12.3 M3 算子面板
- [ ] 14 算子都有元数据
- [ ] 选中 → 右侧预览实时更新
- [ ] 双击 → 模态详编可保存
- [ ] 参数校验生效（min/max）

### 12.4 M4 I/O + Undo
- [ ] Ctrl+S 保存 `.qdv` 成功；加载还原
- [ ] Ctrl+Z 撤销；Ctrl+Y 重做
- [ ] 50 步上限生效
- [ ] 6 类 Command 全部测试通过

### 12.5 M5 UX
- [ ] Toast 显示成功/错误/警告
- [ ] 拖拽视觉反馈完整
- [ ] 悬停 1.5s 弹帮助气泡
- [ ] 工具库灰度 32px；节点彩色 40px

### 12.6 AI 零侵入
- [ ] git diff AI 相关目录 0 改动
- [ ] AI 推理回归测试全部通过
- [ ] 6 个 ONNX 模型推理时间差 < 5%

---

## 13. 时间表（5 阶段）

| 阶段 | 内容 | 工作量 | 累计 |
|------|------|--------|------|
| M1 | MainWindow 增强 | 2–3 天 | 2–3 天 |
| M2 | QML EditView（画布 + 工具库） | 5–7 天 | 7–10 天 |
| M3 | 14 算子参数面板 | 3–4 天 | 10–14 天 |
| M4 | JSON I/O + 50 步 Undo | 2 天 | 12–16 天 |
| M5 | Toast / 帮助 / 拖拽反馈 | 1–2 天 | 13–18 天 |

每阶段独立可验收、可演示。

---

## 14. 交付物清单

- [ ] `docs/superpowers/specs/2026-06-04-qdv-ui-upgrade-design.md`（本文档）
- [ ] `qml/EditView/*.qml`（11 个文件）
- [ ] `src/UI/EditViewBridge.{h,cpp}`
- [ ] `src/UI/OperatorDescriptors.{h,cpp}`
- [ ] `src/UI/OperatorDescriptors.h`
- [ ] `include/UI/MainWindow.h` `src/UI/MainWindow.cpp`（增量）
- [ ] `include/Core/UndoManager.h` `src/Core/UndoManager.cpp`（增量）
- [ ] `tests/UI/test_edit_view_bridge.cpp`
- [ ] `tests/qml/EditView/tst_*.qml`（3 个）
- [ ] `docs/QDetectVision_使用说明书.md` §6.3 编辑模块 v2.2
- [ ] `docs/UIUpgradeTestReport.md`

---

## 15. 更新记录

| 版本 | 日期 | 主要内容 |
|------|------|----------|
| v2.1.0（设计稿） | 2026-06-04 | 初版设计（基于与用户 4 轮澄清） |
