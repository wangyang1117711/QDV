# O1a · 算子库 MVP 预打包导入 — 设计文档

| 字段 | 值 |
|---|---|
| 项目 | QDV (QDetectVision) |
| 子项目 | O1：算子库 Controller 实现与导入 UI |
| 切片 | O1a：MVP 预打包导入 |
| 文档版本 | 1.0 |
| 创建日期 | 2026-07-15 |
| 作者 | 协作自检优化器 |
| 状态 | 待用户审阅 |
| 配套示意图 | [2026-07-15-O1a-operator-import-mockup.html](./2026-07-15-O1a-operator-import-mockup.html) |

---

## 0. 背景与范围

### 0.1 背景

[include/OperatorLibrary/](file:///e:/anchor/Trae/QDV/include/OperatorLibrary/) 下已存在 11 个头文件，定义了完整的算子库架构（Controller/Importer/Validator/RegistryStore/VersionManager/Tester 等），但 [src/OperatorLibrary/](file:///e:/anchor/Trae/QDV/src/OperatorLibrary/) 目录下**仅有 `OperatorDefinition.cpp`** 一个实现文件（纯数据序列化层）。其余 10 个组件均未落地。

**关键事实**：[根 CMakeLists.txt](file:///e:/anchor/Trae/QDV/CMakeLists.txt) L78-92 中**没有** `add_subdirectory(src/OperatorLibrary)`，且 `src/OperatorLibrary/CMakeLists.txt` 不存在。这意味着 `OperatorDefinition.cpp` 当前**完全未被编译**，`OperatorDef/InputPort/OutputPort/ParamDef` 等类型当前未被任何运行时代码引用。本切片实施时必须先建立 `src/OperatorLibrary/CMakeLists.txt` 并在根 CMakeLists.txt 中添加 `add_subdirectory(src/OperatorLibrary)`，才能让整个模块进入构建。

同时，[src/UI/OperatorDescriptors.cpp](file:///e:/anchor/Trae/QDV/src/UI/OperatorDescriptors.cpp) 已预留 `s_externalRegistry` 机制（L110/L962-970），含 `registerExternalOperator(meta)` API，注释明确写"Phase 2: 外部动态算子，由 OperatorPluginLoader 调用"，但实际仅由插件加载器使用，未对接 OperatorLibrary 模块。

### 0.2 本切片范围（O1a）

**包含**：
- 实现 4 个核心后端组件：`OperatorRegistryStore` / `OperatorValidator` / `OperatorImporter` / `OperatorLibraryController`
- 实现 1 个 QML 桥接层：`OperatorLibraryBridge`
- 实现 1 个 QML 列表模型：`OperatorListModel`（仅服务于"已导入算子面板"）
- 实现 2 个 QML 对话框：`OperatorImportDialog.qml` / `ImportedOperatorsPanel.qml`
- 在 [qml/EditView/Main.qml](file:///e:/anchor/Trae/QDV/qml/EditView/Main.qml) 算子库面板底部新增入口按钮
- `.qdvop` JSON 文件格式定义与文档化
- 启动加载、导入、删除、刷新完整流程

**不包含**（推迟到后续切片）：
- `OperatorMetadataInferrer`（O1b，C++ 插件 manifest 识别）
- `OperatorTester`（O1b/O1c）
- `OperatorVersionManager`（O1e）
- `OperatorDocGenerator` / `OperatorPermissionManager` / `OperatorDependencyGraph`（O2）
- C++ 源码导入 + 应用内编译（O1d）
- Python 脚本算子导入（O1c）

### 0.3 成功标准（三层，对齐 AGENTS.md 规则 II 阶段1）

**技术成功（必须）**：
- 所有单元测试通过（≥36 用例，见 §6.2）
- 集成测试 5 项全过
- 应用启动无崩溃，导入对话框可正常打开/关闭

**业务成功（必须）**：
- 用户能成功导入一个合法 `.qdvop` 文件
- 导入的算子立即出现在 EditView 左侧算子面板
- 拖入画布后可通过 OperatorEditorDialog 配置参数
- 能从"已导入算子"面板删除算子（无画布依赖时）

**用户满意度（目标）**：
- 导入流程不超过 4 次点击（选文件→预览→提交→关闭）
- 错误提示清晰，用户能根据提示自行修正
- 与现有 UI 风格无明显割裂感

---

## 1. 架构与组件清单

### 1.1 数据流分层

```
┌──────────────────────────────────────────────────────────┐
│ QML 层：OperatorImportDialog.qml / ImportedOperatorsPanel │
└────────────────────────┬─────────────────────────────────┘
                         │ Q_INVOKABLE 调用
┌────────────────────────▼─────────────────────────────────┐
│ Bridge 层：OperatorLibraryBridge                          │
│  - openImportDialog() / requestPreview(path)              │
│  - commitImport(preview) / removeImported(type)           │
│  - listImported() → OperatorListModel                     │
│  - signal: importedChanged(type, action)                  │
│           importFailed(msg)                               │
└────────────────────────┬─────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────┐
│ 控制器层：OperatorLibraryController（单例）                │
│  - init(operatorsPath)                                    │
│  - importFile(path) → ImportPreview                       │
│  - commitImport(preview) → bool                           │
│  - removeImported(type) → {ok, error}                     │
│  - query(category, keyword) → QVariantList                │
└──────┬──────────────┬──────────────┬─────────────────────┘
       │              │              │
┌──────▼─────┐ ┌──────▼──────┐ ┌─────▼─────────────────────┐
│ Importer   │ │ Validator   │ │ RegistryStore             │
│ 解析+查重  │ │ 字段校验    │ │ 文件 I/O                   │
└────────────┘ └─────────────┘ └───────────────────────────┘
                                        │
                                        ▼
                          config/operators_imported/*.qdvop
                                        │
                                        ▼ (启动时 + 导入后)
              OperatorDescriptors::registerExternalOperator(meta)
                                        │
                                        ▼
                  EditViewBridge::searchOperators() 自动可见
```

### 1.2 新建/补齐文件清单

| 文件 | 类型 | 职责 | 行数估算 |
|---|---|---|---|
| `src/OperatorLibrary/OperatorRegistryStore.cpp` | 新建 | 持久化层：扫描目录、加载/保存/删除单个 `.qdvop` | ~180 |
| `src/OperatorLibrary/OperatorValidator.cpp` | 新建 | 字段校验、type 命名规范、ParamDef 合法性 | ~140 |
| `src/OperatorLibrary/OperatorImporter.cpp` | 新建 | 解析 JSON → OperatorDef → 校验 → 查重 → 产 ImportPreview | ~160 |
| `src/OperatorLibrary/OperatorLibraryController.cpp` | 新建 | 门面：聚合上述三者 + 桥接 OperatorDescriptors | ~220 |
| `src/OperatorLibrary/OperatorListModel.cpp` + `.h` | 新建 | QAbstractListModel，已导入算子列表 | ~150 |
| `include/UI/OperatorLibraryBridge.h` + `src/UI/OperatorLibraryBridge.cpp` | 新建 | QML bridge：Q_INVOKABLE 方法 + 信号 | ~180 |
| `qml/EditView/OperatorImportDialog.qml` | 新建 | 导入对话框：选文件→预览→提交 | ~280 |
| `qml/EditView/ImportedOperatorsPanel.qml` | 新建 | 已导入算子列表面板 | ~180 |

**CMake 改动**（关键：当前 OperatorLibrary 模块完全未进入构建）：
- 新建 [src/OperatorLibrary/CMakeLists.txt](file:///e:/anchor/Trae/QDV/src/OperatorLibrary/CMakeLists.txt)：定义 `QDV::OperatorLibrary` 静态库，包含 `OperatorDefinition.cpp` + 上述 4 个新建 .cpp + `OperatorListModel.cpp`
- 修改 [根 CMakeLists.txt](file:///e:/anchor/Trae/QDV/CMakeLists.txt) L86 后新增 `add_subdirectory(src/OperatorLibrary)`
- 修改 [src/UI/CMakeLists.txt](file:///e:/anchor/Trae/QDV/src/UI/CMakeLists.txt)：`target_link_libraries(... PRIVATE QDV::OperatorLibrary)` + 加入 `OperatorLibraryBridge.cpp`
- 修改 [apps/SmartVision/CMakeLists.txt](file:///e:/anchor/Trae/QDV/apps/SmartVision/CMakeLists.txt)：`target_link_libraries(... PRIVATE QDV::OperatorLibrary)`
- 修改 [qml/EditView.qrc](file:///e:/anchor/Trae/QDV/qml/EditView.qrc)：注册 2 个新 QML 文件
- 修改 [tests/CMakeLists.txt](file:///e:/anchor/Trae/QDV/tests/CMakeLists.txt)：新建 `operator_library_test.cpp` 测试目标，链接 `QDV::OperatorLibrary`

### 1.3 与现有代码的集成点（仅 3 处）

1. **读集成**：[src/UI/OperatorDescriptors.cpp](file:///e:/anchor/Trae/QDV/src/UI/OperatorDescriptors.cpp) L962 `registerExternalOperator()` — 已存在的 Phase 2 钩子，直接复用，零修改
2. **UI 集成**：[qml/EditView/Main.qml](file:///e:/anchor/Trae/QDV/qml/EditView/Main.qml) L1383-1384 `operatorList.model = null; operatorList.model = operatorList.buildModel()` — 复用现有刷新模式
3. **Bridge 集成**：[include/UI/EditViewBridge.h](file:///e:/anchor/Trae/QDV/include/UI/EditViewBridge.h) — 新增 `operatorLibraryBridge` 属性指向 `OperatorLibraryBridge` 实例

### 1.4 关键设计决策

| 决策 | 选项 | 理由 |
|---|---|---|
| UI 技术栈 | QML（非 Widgets） | 与 EditView 主体一致，复用 DesignTokens 与 ParamForm |
| 文件格式 | 纯 JSON（非 ZIP） | MVP 阶段无需携带资源；OperatorDef::fromJson 直接可用 |
| 冲突处理 | 拒绝重复 type | 最安全；用户须改 type 重试 |
| 持久化位置 | `config/operators_imported/` 一算子一文件 | 避免污染 operators.json；单文件损坏不影响其他 |
| EditView 集成 | 导入即发布 | 符合"导入即用"心理模型；无 Draft/Published 二态 |
| 单例模式 | Controller 单例 | 与 ModelManager::instance() 一致 |
| OperatorDef 为唯一中间表达 | 是 | 契约清晰，符合 AGENTS.md 规则 I.1 |
| OperatorMeta 转换位置 | 仅在 Controller 边界 | OperatorLibrary 模块内部不感知 OperatorMeta |

---

## 2. 数据流详解

### 2.1 启动加载流程

```
main.cpp 启动
   │
   ▼
OperatorLibraryController::instance().init("config/operators_imported")
   │
   ├─▶ RegistryStore::scanAll()
   │     遍历 config/operators_imported/*.qdvop（按文件名字典序）
   │     逐文件 → OperatorDef::fromJson() → 缓存到 m_loaded[type] = def
   │     损坏文件：记日志 + 跳过（不阻断启动）
   │
   ├─▶ 对每个 def：
   │     OperatorDescriptors::registerExternalOperator(def.toOperatorMeta())
   │     （registerExternalOperator 内部去重，已注册返回 false 但不报错）
   │
   └─▶ 发信号 storeInitialized(count)
```

**关键约束**：
- 启动失败不阻断应用（算子库是增量功能）
- 文件扫描顺序按文件名字典序，保证多次启动行为一致

### 2.2 导入流程（用户主动触发）

```
[QML] 用户点算子库面板底部"导入算子"按钮
   │
   ▼
OperatorLibraryBridge::openImportDialog()
   │  弹出 OperatorImportDialog.qml
   │
[QML] 用户选 .qdvop 文件 → 自动触发预览
   │
   ▼
Bridge::requestPreview(filePath) → Controller::importFile(filePath) → ImportPreview
   │
   ├─▶ Importer::importFile(path)
   │     ① 读文件 → QJsonDocument → OperatorDef::fromJson(err)
   │     ② 失败 → 填充 ImportPreview.def 为空 + conflicts=[Error: 解析失败]
   │     ③ 成功 → 调 Validator::validate(def) → issues 转为 conflicts/warnings
   │     ④ 调 RegistryStore::contains(def.type) → 若重复，追加 conflict: Error: type 已存在
   │     ⑤ 调 OperatorDescriptors::has(def.type) → 内置冲突同样阻断
   │     ⑥ 产 ImportPreview{def, conflicts, warnings}
   │
   ▼
Bridge 收到 ImportPreview → 通过 previewReady(preview) 信号推给 QML
   │
[QML] 预览面板显示：
   │     - 算子名称/类型/分类/版本/作者
   │     - 输入端口、输出端口、参数列表（只读）
   │     - 冲突区（红色，阻断提交） / 警告区（橙色，可忽略）
   │     - "提交导入"按钮仅在 conflicts 为空时可用
   │
[QML] 用户点"提交导入"
   │
   ▼
Bridge::commitImport(preview) → Controller::commitImport(preview) → bool
   │
   ├─▶ 二次校验：再次确认 conflicts 为空（防止 TOCTOU）
   │
   ├─▶ RegistryStore::save(def)
   │     目标路径：config/operators_imported/<type>.qdvop
   │     写入：先写临时文件 <type>.qdvop.tmp → rename 为 <type>.qdvop（原子操作）
   │     若目标文件已存在（理论不应发生，因 importFile 已查重）：
   │        - 先复制为 <type>.qdvop.bak（备份）
   │        - 再覆盖写入
   │        - 写入成功后保留 .bak；写入失败从 .bak 恢复
   │
   ├─▶ OperatorDescriptors::registerExternalOperator(def.toOperatorMeta())
   │     失败（已注册）→ 回滚删除刚写入的文件，返回错误
   │
   ├─▶ m_loaded[type] = def  （更新内存缓存）
   │
   └─▶ 发信号 importedChanged(type, "added")
         → Bridge 转发 → QML 触发 operatorList.model = null; buildModel()
```

### 2.3 卸载流程（删除已导入算子）

```
[QML] 在 ImportedOperatorsPanel 中选中算子 → 点"删除"
   │
   ▼
Bridge::removeImported(type)
   │
   ▼
Controller::removeImported(type) → QVariantMap{ok, error}
   │
   ├─▶ 查 m_loaded 是否存在；不存在返回 {ok:false, error:"未找到"}
   │
   ├─▶ ⚠️ 安全检查：当前 EditView 画布是否有节点使用该 type？
   │     通过 EditViewBridge::currentNodes 检查（在 Bridge 层预检）
   │     若有 → 返回 {ok:false, error:"画布上存在 N 个使用该算子的节点，请先删除"}
   │
   ├─▶ RegistryStore::remove(type)
   │     删除 config/operators_imported/<type>.qdvop 文件
   │     失败（权限/只读）→ 返回 {ok:false, error:"文件删除失败: ..."}
   │
   ├─▶ 内存清理：m_loaded.remove(type)
   │     ⚠️ OperatorDescriptors::s_externalRegistry 无反注册 API
   │     MVP 策略：UI 上对"已删除但本次仍可见"的算子添加置灰标识
   │     下次启动彻底生效
   │
   └─▶ 发信号 importedChanged(type, "removed")
         → Bridge 转发 → QML 刷新
```

**卸载降级说明**：由于 `OperatorDescriptors` 不支持单算子反注册（[src/UI/OperatorDescriptors.cpp](file:///e:/anchor/Trae/QDV/src/UI/OperatorDescriptors.cpp) L957-958 `clear()` 是全清），MVP 切片采取"文件删除 + 内存标记 + 下次启动彻底生效"策略。这是已知技术债，O2 阶段补 `OperatorDescriptors::unregisterExternalOperator()` 后修复。

### 2.4 刷新流程（QML 端）

```qml
Connections {
    target: operatorLibraryBridge
    function onImportedChanged(type, action) {
        // 复用 Main.qml L1383-1384 的刷新模式
        operatorList.model = null
        operatorList.model = operatorList.buildModel()
    }
}
```

---

## 3. 文件格式与存储

### 3.1 `.qdvop` 文件格式

**本质**：UTF-8 编码的 JSON 文件，内容为 `OperatorDef::toJson()` 输出。

**最小合法示例**：

```json
{
  "type": "MyCustomThreshold",
  "kind": "config",
  "cnName": "我的自定义阈值",
  "category": "图像分割",
  "subGroup": "阈值分割",
  "description": "基于双峰直方图自动选择阈值的二值化算子",
  "version": "1.0.0",
  "status": "published",
  "author": "user",
  "tags": ["自定义", "阈值"],
  "inputs": [
    {"name": "image", "cnName": "输入图像", "type": "Image", "required": true}
  ],
  "outputs": [
    {"name": "region", "cnName": "分割区域", "type": "Region", "defaultEnabled": true}
  ],
  "params": [
    {
      "name": "mode",
      "cnName": "阈值模式",
      "type": 2,
      "defaultValue": "auto",
      "options": ["自动", "手动"],
      "optionKeys": ["auto", "manual"]
    }
  ],
  "implementation": {
    "recipe": ["Threshold"]
  }
}
```

### 3.2 校验规则（OperatorValidator）

| 字段 | 规则 | 失败级别 |
|---|---|---|
| `type` | 非空；`^[A-Za-z][A-Za-z0-9_]{2,63}$`；不以下划线开头 | Error |
| `cnName` | 非空；≤32 字符 | Error |
| `category` | 非空；建议命中现有分类（参见 [DesignTokens.qml](file:///e:/anchor/Trae/QDV/qml/EditView/DesignTokens.qml) `categoryColor()` 中的分类列表），未命中仅 Warning | Warning |
| `version` | 语义化版本（`^\d+\.\d+\.\d+$`） | Error |
| `inputs` | 至少 1 个；每个 `name` 唯一 | Error |
| `outputs` | 至少 1 个；每个 `name` 唯一 | Error |
| `params` | 可为空；每个 `name` 唯一；Enum 类型必须有 `options` | Error |
| `implementation` | `recipe` 与 `library` 至少一项非空 | Error |
| `recipe` 中每个 type | 必须存在于 `OperatorDescriptors::allTypes()` 或本批次导入 | Warning |

### 3.3 存储约定

- 目录：`config/operators_imported/`（应用启动时自动创建）
- 创建失败降级：`QStandardPaths::AppDataLocation/operators_imported/`
- 命名：`<type>.qdvop`（如 `MyCustomThreshold.qdvop`）
- 编码：UTF-8 with BOM（与 [config/operators.json](file:///e:/anchor/Trae/QDV/config/operators.json) 一致）
- 格式化：`QJsonDocument::Indented`，2 空格缩进
- 原子写入：先写 `<type>.qdvop.tmp` → `QFile::rename` 为 `<type>.qdvop`
- 备份：保存前若文件已存在，先复制为 `<type>.qdvop.bak`（仅保留最近一份）

### 3.4 命名冲突保护

- `importFile()` 阶段：`RegistryStore::contains(type)` 检查内存缓存
- `importFile()` 阶段：`OperatorDescriptors::has(type)` 检查内置算子
- `commitImport()` 阶段：二次检查文件系统（防 TOCTOU）
- 大小写敏感（与 `OperatorDescriptors::has` 一致）

---

## 4. UI 设计

### 4.1 入口点（算子库面板底部按钮）

在 [qml/EditView/Main.qml](file:///e:/anchor/Trae/QDV/qml/EditView/Main.qml) 左侧算子库面板最下方添加 RowLayout：

```qml
RowLayout {
    Layout.fillWidth: true
    spacing: Tok.DesignTokens.space2

    Button {
        text: "导入算子"
        Layout.fillWidth: true
        onClicked: operatorImportDialogLoader.item.open()
    }
    Button {
        text: "已导入 (N)"
        Layout.fillWidth: true
        onClicked: importedOperatorsPanelLoader.item.open()
    }
}
```

### 4.2 导入对话框（OperatorImportDialog.qml）

**布局**：580×680 Popup，4 个分区：
1. 选择文件（文件输入框 + 浏览按钮，文件过滤器 `*.qdvop`）
2. 预览（算子元信息 + 输入/输出端口列表 + 参数列表，只读）
3. 校验结果（成功项绿色、警告项橙色、冲突项红色）
4. 操作按钮（取消 / 提交导入，提交按钮在有冲突时禁用）

**交互**：
- 选择文件后自动触发预览解析
- 冲突存在时"提交导入"按钮禁用并提示"请先解决冲突"
- 提交成功 → Toast"算子 [name] 导入成功" + 对话框关闭
- 提交失败 → Toast 错误信息，对话框保持开启

### 4.3 已导入算子面板（ImportedOperatorsPanel.qml）

**布局**：420×560 Popup，从右侧抽屉滑入：
- 顶部搜索框
- 列表项：算子名 + 版本号 + 中文名 + 分类 + 删除按钮
- 已删除但本次仍可见的算子置灰显示"已删除·重启生效"
- 底部说明文字解释置灰原因

**交互**：
- 删除按钮触发二次确认对话框
- 删除前预检画布节点使用情况（在 Bridge 层）
- 删除成功 → Toast"算子 X 已删除"

### 4.4 反馈机制

| 场景 | 反馈形式 | 颜色 |
|---|---|---|
| 解析成功 | 预览面板填充内容 | `textSecondary` |
| 解析失败 | 警告框 + 错误详情 | `accentError` |
| 校验通过 | "✓ 通过: N 项检查" | `accentSuccess` |
| 校验警告 | "⚠ 警告: ..." | `accentWarning` |
| 冲突 | "✗ 错误: ..." | `accentError` |
| 提交成功 | Toast "算子 X 导入成功" | `accentSuccess` |
| 提交失败 | Toast 错误信息 | `accentError` |
| 删除成功 | Toast "算子 X 已删除" | `accentSuccess` |
| 删除失败 | Toast 错误信息 | `accentError` |

### 4.5 与现有 UI 规范对齐

| Token | 值 | 用途 |
|---|---|---|
| `fontFamilyCJK` | "Microsoft YaHei UI", ... | 全部文字 |
| `fontSizeXl` | 16 | 对话框标题 |
| `fontSizeBase` | 13 | 正文 |
| `fontSizeSm` | 12 | 辅助文字 |
| `space2/3/4` | 8/12/16 | 间距 |
| `radiusMd` | 4 | 圆角 |
| `controlHeight` | 28 | 按钮高度 |
| `accentPrimary` | #7C4DFF | 主强调 |
| `accentSuccess/Warning/Error` | #69F0AE / #FFD740 / #FF5252 | 语义色 |

完整配色与样式见配套 HTML 示意图。

---

## 5. 错误处理与边界

### 5.1 错误分类与处理矩阵

| 错误场景 | 检测点 | 用户可见反馈 | 系统行为 |
|---|---|---|---|
| 文件不存在 | Importer 读取 | "文件不存在: ..." | 不写入，不缓存 |
| 文件读权限失败 | Importer 读取 | "无法读取文件: ..." | 同上 |
| JSON 解析失败 | `QJsonDocument::fromJson` | "JSON 格式错误（行 N）: ..." | 同上 |
| `OperatorDef::fromJson` 返回 err | OperatorDef | "字段缺失/类型错误: ..." | 同上 |
| 必填字段缺失 | Validator | 预览面板冲突区列出 | 阻断提交 |
| type 重复（已导入） | Importer 查重 | "type 'X' 已存在（已导入算子）" | 阻断提交 |
| type 重复（内置） | `OperatorDescriptors::has` | "type 'X' 已存在（内置算子）" | 阻断提交 |
| Enum 参数无 options | Validator | "参数 'X' 为 Enum 但未定义 options" | 阻断提交 |
| recipe 引用不存在的算子 | Validator | 警告"recipe 引用未知算子 'X'" | 不阻断，仅警告 |
| 保存文件失败（磁盘满/权限） | RegistryStore | "保存失败: ..." | 不更新内存，回滚 |
| registerExternalOperator 失败 | Controller | "注册到算子库失败" | 删除已写文件，回滚 |
| 启动时目录不存在 | RegistryStore | 无（自动创建） | 创建目录，日志记录 |
| 启动时单文件损坏 | RegistryStore | 无（静默跳过） | 日志警告，继续加载其他 |
| 删除时画布有节点使用 | Bridge 预检 | "画布上存在 N 个使用该算子的节点" | 阻断删除 |
| 删除文件失败 | RegistryStore | "文件删除失败: ..." | 不更新内存 |

### 5.2 关键不变量（Invariant）

1. **磁盘与内存一致性**：`m_loaded[type]` 存在 ⟺ `config/operators_imported/<type>.qdvop` 文件存在（启动后成立，运行期通过事务保证）
2. **OperatorDescriptors 一致性**：`m_loaded[type]` 存在 ⟹ `OperatorDescriptors::has(type)` 为真
3. **type 唯一性**：`m_loaded` 的 keys 与内置算子 type 集合不相交
4. **事务性**：`commitImport` 要么全部成功（文件+内存+注册），要么全部回滚

### 5.3 并发与重入

- 本切片为单线程 UI 操作，不考虑并发导入
- 重入保护：`commitImport` 期间禁用导入按钮，防止用户连续点击
- 文件写入采用"先写临时文件 + rename"原子操作，避免中途崩溃产生半文件

### 5.4 边界场景

| 场景 | 处理 |
|---|---|
| `.qdvop` 文件 0 字节 | JSON 解析失败，提示"空文件" |
| `.qdvop` 文件超大（>1MB） | 不限制，但日志警告"异常大的算子定义" |
| type 含特殊字符（如空格、中文） | Validator 拒绝，提示"仅允许字母数字下划线" |
| type 大小写敏感（`Threshold` 与 `threshold` 视为不同） | 与 `OperatorDescriptors::has` 一致，大小写敏感 |
| 同一文件重复导入 | 第二次因 type 重复被拒绝 |
| 用户在文件对话框中选了非 .qdvop | 文件过滤器限定 `.qdvop`，但允许用户强行选其他 → 解析失败提示 |
| `config/operators_imported/` 目录被用户删除 | 启动时自动重建 |
| 应用启动时正在写入的临时文件 `.tmp` 残留 | 启动扫描时跳过 `.tmp` 后缀 |

---

## 6. 测试策略

### 6.1 测试金字塔

```
单元测试（70%）：OperatorDef/Validator/Importer/RegistryStore
集成测试（20%）：Controller + OperatorDescriptors 联动
UI 验证（10%）：手动操作 + 截图验收
```

### 6.2 单元测试清单

| 模块 | 用例数 | 关键用例 |
|---|---|---|
| OperatorValidator | 12 | 全字段合法、type 缺失、type 含非法字符、Enum 无 options、recipe 引用未知算子、version 非语义化、cnName 超长、inputs 名重复 |
| OperatorImporter | 10 | 合法文件、空文件、JSON 损坏、type 重复（已导入）、type 重复（内置）、字段类型错误、嵌套对象错误、Implementation 全空 |
| OperatorRegistryStore | 8 | 扫描空目录、扫描含损坏文件、保存覆盖、删除不存在、目录创建失败降级、原子写入验证、tmp 文件跳过、备份生成 |
| OperatorLibraryController | 6 | init 流程、commitImport 事务回滚、removeImported 内存清理、二次校验防 TOCTOU、registerExternalOperator 失败回滚 |

**合计 ≥36 用例**

### 6.3 集成测试清单

| 用例 | 验证点 |
|---|---|
| 启动加载 3 个 .qdvop → `OperatorDescriptors::allTypes()` 包含 3 个新 type | 注册集成 |
| 导入 1 个算子 → `searchOperators("MyCustom")` 返回该算子 | 搜索集成 |
| 导入后删除 → 重启 → `allTypes()` 不再包含 | 持久化一致性 |
| 导入 type="Threshold"（与内置冲突）→ 被拒绝 | 冲突保护 |
| 导入合法算子 → 在 EditView 拖入画布 → 可配置参数 | 端到端 |

### 6.4 5 条对抗性测试用例（AGENTS.md 规则 II 阶段1 红队要求）

1. **TOCTOU 攻击**：用户在预览阶段看到 type 不冲突，但在提交前另一进程在 `config/operators_imported/` 创建了同名文件。验证：`commitImport` 二次检查文件系统，拒绝并提示，不覆盖。

2. **事务中途崩溃**：`commitImport` 在写入文件后、`registerExternalOperator` 前应用崩溃。重启后 `init` 扫描到该文件并尝试注册，但因内置已有同 type 而被 `registerExternalOperator` 拒绝（返回 false）。验证：日志记录"注册失败，跳过"，应用正常启动，文件保留待用户处理。

3. **恶意 JSON 嵌套**：构造一个 `.qdvop`，`params` 数组中某个元素的 `defaultValue` 是一个 100 层嵌套的对象。验证：`OperatorDef::fromJson` 不崩溃，要么解析成功（Qt JSON 默认无深度限制但耗时长）要么超时；不阻断其他算子加载。

4. **删除时画布依赖**：用户在 EditView 画布上有 1 个 `MyCustomThreshold` 节点，尝试从 ImportedOperatorsPanel 删除该算子。验证：弹出"画布上存在 1 个使用该算子的节点，请先删除"，删除被阻断，文件保留。

5. **重命名绕过冲突**：用户导入 `type=Threshold` 被拒（冲突），手动改 JSON 为 `type=threshold2` 后再次导入。验证：通过（type 大小写敏感，`threshold2` 不冲突），但用户在画布上能区分两个算子。同时验证：若用户改回 `type=_Threshold`（下划线开头），Validator 拒绝。

---

## 7. 实施顺序建议

按依赖关系，建议如下顺序（每步可独立编译验证）：

0. **CMake 接入**（前置必须）：新建 `src/OperatorLibrary/CMakeLists.txt`，根 CMakeLists.txt 加 `add_subdirectory`，先编译 `OperatorDefinition.cpp` 通过验证基础类型链路
1. **OperatorRegistryStore** + 单元测试（无外部依赖）
2. **OperatorValidator** + 单元测试（仅依赖 OperatorDef）
3. **OperatorImporter** + 单元测试（依赖 1+2）
4. **OperatorLibraryController** + 集成测试（依赖 1-3 + OperatorDescriptors）
5. **OperatorListModel**（依赖 4）
6. **OperatorLibraryBridge**（依赖 4+5）
7. **OperatorImportDialog.qml**（依赖 6）
8. **ImportedOperatorsPanel.qml**（依赖 6）
9. **Main.qml 入口按钮接入**（依赖 7+8）
10. **端到端验收**（依据 §0.3 成功标准）

---

## 8. 已知技术债与后续切片

| 技术债 | 当前处理 | 修复切片 |
|---|---|---|
| `OperatorDescriptors` 不支持单算子反注册 | 删除后置灰，重启生效 | O2：补 `unregisterExternalOperator()` |
| 模型编辑器无法识别"已删除"算子的节点 | 用户须自行删除画布节点 | O2 |
| 无版本控制 | 导入即覆盖式保存（.bak 兜底） | O1e |
| 无导入日志 | 仅 Logger 日志 | O1e |
| 不支持 C++ 源码导入 | 仅支持预打包 .qdvop | O1d |
| 不支持 Python 脚本算子 | 同上 | O1c |
| 不支持插件 .dll 导入 | 同上 | O1b |

---

## 9. 决策记录

| 时间 | 决策 | 决策者 | 理由 |
|---|---|---|---|
| 2026-07-15 | 子项目分解为 O1a-O1e + M1-M4 | 用户+AI | 单次 spec 范围过大违反 AGENTS.md 阶段1规则 |
| 2026-07-15 | O1a 优先于 M1 | 用户 | 用户选择 |
| 2026-07-15 | UI 采用 QML | 用户 | 与 EditView 一致 |
| 2026-07-15 | 入口在算子库面板底部 | 用户 | 一眼可见 |
| 2026-07-15 | .qdvop 为纯 JSON | 用户 | MVP 简化 |
| 2026-07-15 | 冲突拒绝重复 type | 用户 | 最安全 |
| 2026-07-15 | 持久化到 config/operators_imported/ | 用户 | 隔离内置与导入 |
| 2026-07-15 | 导入即发布 | 用户 | 符合直觉 |
| 2026-07-15 | 架构采用方案 A（复用 s_externalRegistry） | 用户+AI | 顺着原设计走，零侵入 |
| 2026-07-15 | 规划完成后先生成 HTML 示意图再实施 | 用户 | 视觉验收优先 |
