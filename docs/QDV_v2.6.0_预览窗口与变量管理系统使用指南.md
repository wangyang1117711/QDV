# QDV v2.6.0 预览窗口与变量管理系统使用指南

## 一、版本概述

v2.6.0 实现了**Halcon 风格的预览窗口与变量管理系统**，主要包含：

1. **底部工作区**：在主窗口底部新增可调节高度的工作区，包含"预览"和"变量管理"两个 Tab
2. **预览窗口**：实时显示算子节点输出图像，支持 Halcon 风格缩放/平移/适应窗口
3. **变量管理系统**：
   - 图像变量：自动收集算子输出图像
   - 控制变量：数值/字符串/布尔型变量，支持 `${变量名}` 绑定到算子参数
4. **实时预览**：参数/变量变更后自动执行预览（300ms 防抖）
5. **变量持久化**：变量随方案文件保存/加载

---

## 二、底部工作区操作

### 2.1 显示/隐藏底部工作区

- **顶部工具栏按钮**：点击 ▼/▲ 按钮切换底部工作区显示
- **底部标题栏按钮**：在工作区标题栏右侧点击 ▼ 折叠 / ▲ 展开

### 2.2 调节底部工作区高度

- **拖拽分隔条**：将鼠标移到工作区顶部边缘，光标变为 ↕ 时按住左键上下拖拽
- **高度范围**：120px ~ 600px

### 2.3 切换 Tab

- 点击标题栏的"预览"或"变量管理"标签切换 Tab

---

## 三、预览窗口（PreviewPanel）

### 3.1 基本操作

| 操作 | 方法 |
|------|------|
| 切换预览节点 | 顶部下拉框选择算子节点 |
| 立即执行 | 点击 ▶ 按钮 |
| 适应窗口 | 点击 ⤢ 按钮或双击图像 |
| 缩放 | 鼠标滚轮（10% ~ 1000%） |
| 平移 | 左键按住拖拽 |
| 实时预览开关 | 切换"实时"开关 |
| 固定预览节点 | 点击 📌/🔒 按钮 |

### 3.2 实时预览

- **开启实时预览**：参数变更后自动执行预览（300ms 防抖）
- **固定预览节点**：固定后不会跟随选中节点切换，便于对比调试
- **节点切换**：切换预览节点时立即执行（跳过防抖）

### 3.3 图像信息

预览窗口右下角显示图像信息：
- 算子名称（如 Threshold）
- 图像尺寸（如 640×480）
- 通道数（如 3ch）

---

## 四、变量管理系统

### 4.1 图像变量（自动管理）

**特性**：
- 算子执行后自动收集输出图像
- 按节点 ID 索引，节点删除时自动清理
- 列表显示缩略图、算子名、节点 ID、尺寸/通道

**操作**：
- 点击列表项的 ▶ 按钮 → 切换为预览节点（自动跳转到预览 Tab）
- 双击列表项 → 同上

### 4.2 控制变量（手动管理）

#### 4.2.1 创建变量

1. 切换到"变量管理" Tab
2. 点击 + 按钮 → 弹出新建对话框
3. 填写：
   - **变量名**：字母/数字/下划线，首字符必须为字母或下划线
   - **类型**：Int / Double / String / Bool
   - **值**：根据类型输入
   - **描述**（可选）：变量用途说明
4. 点击"创建"

#### 4.2.2 编辑变量

- **修改值**：直接在列表中点击值字段，编辑后按回车
- **修改详情**：点击 ✎ 按钮，弹出编辑对话框
- **删除变量**：点击 ✕ 按钮

#### 4.2.3 变量绑定算子参数

**语法**：`${变量名}`

**示例**：
1. 创建变量 `thresh_val`（Int 类型，值 128）
2. 添加 Threshold 算子
3. 在算子参数中，将 `threshold` 参数值设为 `${thresh_val}`
4. 修改变量 `thresh_val` 的值（如改为 200）
5. 实时预览自动执行 → Threshold 算子使用新值 200

**支持场景**：
- 单变量：`${thresh_val}`
- 前后缀：`prefix_${var}_suffix`
- 多变量：`${var1}_${var2}`
- 字符串中嵌入：`color_${mode}`

### 4.3 变量持久化

变量随方案文件自动保存/加载：
- **保存方案**：所有控制变量自动写入方案 JSON 的 `variables` 字段
- **加载方案**：自动从 `variables` 字段恢复变量
- **新建方案**：清空所有变量

---

## 五、技术架构

### 5.1 后端（C++）

| 类 | 命名空间 | 职责 |
|----|----------|------|
| `VariableManager` | QDV | 控制变量 CRUD + ${var} 绑定解析 + 序列化 |
| `ImageVariableManager` | QDV | 图像变量收集（按节点 ID 索引） |
| `PreviewManager` | QDV | 预览管理（300ms 防抖 + 自动跟随选中节点） |
| `EditViewBridge` | 全局 | 暴露管理器到 QML + 信号连接 |
| `SchemeSerializer` | QDV::UI | 方案 JSON 增加 variables 字段 |

### 5.2 前端（QML）

| 组件 | 职责 |
|------|------|
| `PreviewPanel.qml` | 预览窗口（缩放/平移/节点切换/固定） |
| `VariableManagerPanel.qml` | 变量管理（图像 Tab + 控制 Tab） |
| `VarEditDialog.qml` | 变量新建/编辑对话框 |
| `Main.qml` | 底部工作区集成（Tab 切换 + 高度调节） |

### 5.3 数据流

```
用户操作（参数变更/变量修改/节点选中）
    ↓
EditViewBridge 发出信号
    ↓
PreviewManager 触发防抖（300ms）
    ↓
doPreview() → runSingleOperator()
    ↓
ToolChainExecutor 执行上游链
    ↓
ImageVariableManager 收集输出图像
    ↓
QML 端 PreviewPanel 刷新显示
```

---

## 六、测试覆盖

### 6.1 单元测试（300/300 通过）

v2.6.0 新增 15 个测试用例：

| # | 测试名 | 覆盖范围 |
|---|--------|----------|
| 1 | VariableManager createVariable basic | CRUD 基本 |
| 2 | VariableManager rejects invalid name | 非法变量名拒绝 |
| 3 | VariableManager setValue type match | 值修改 + 类型匹配 |
| 4 | VariableManager removeVariable | 删除 |
| 5 | VariableManager resolveBinding | ${var} 绑定解析 |
| 6 | VariableManager resolveVariant map | 递归解析 Map |
| 7 | VariableManager serialize/deserialize | 序列化 |
| 8 | ImageVariableManager basic CRUD | 图像变量 CRUD |
| 9 | EditViewBridge exposes variable managers | 管理器暴露 |
| 10 | EditViewBridge variablesToJson roundtrip | JSON 端到端 |
| 11 | EditViewBridge loadVariablesFromJson invalid json | 无效 JSON 拒绝 |
| 12 | EditViewBridge variable binding in operator params | 变量绑定算子参数 |
| 13 | PreviewManager follows node selection | 跟随选中节点 |
| 14 | PreviewManager autoPreview toggle | 自动预览开关 |
| 15 | ImageVariableManager cleanup on node delete | 节点删除清理 |

### 6.2 冒烟测试

- 应用启动正常
- 底部工作区显示
- Tab 切换工作
- 不影响现有编辑功能

---

## 七、常见问题

### Q1：实时预览太频繁导致卡顿？
A：可关闭"实时"开关，改为手动点击 ▶ 执行预览。

### Q2：变量绑定不生效？
A：检查变量名是否符合规范（字母/数字/下划线，首字符为字母或下划线）。在算子参数中输入 `${变量名}`，注意大小写敏感。

### Q3：图像变量列表为空？
A：图像变量在算子执行后自动收集。请先运行算子（点击 ▶ 或部署）。

### Q4：预览节点固定后如何取消？
A：点击预览窗口工具栏的 🔒 按钮取消固定，预览节点将重新跟随选中节点。

### Q5：变量保存到方案文件后能否手动编辑？
A：可以。方案 JSON 的 `variables` 字段是一个数组，每个元素包含 `name/type/value/description`，可直接编辑 JSON 文件。
