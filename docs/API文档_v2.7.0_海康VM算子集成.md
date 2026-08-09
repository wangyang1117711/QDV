# QDV v2.7.0 海康VM算子集成 API 文档

> 版本：v2.7.0  
> 适用模块：QDV 编辑/执行链（Vision/Core）  
> 文档基线：`include/Core/VariableManager.h`、`include/Vision/*.h`、`include/Core/Scheme.h` 及对应 `.cpp` 实现  
> 命名空间：除特别注明外，类位于 `QDV` 命名空间；`BranchNode`、`Scheme`、`ToolResult`、`BranchControlTool`、`LoopTool`、`VariableTool`、`FlowJoinTool`、`ToolChainExecutor` 位于全局命名空间

---

## 1. 概述

### 1.1 升级背景与目标

v2.7.0 围绕"海康VM 自定义参数与逻辑控制算子手册"完成一轮能力对齐，目标是让 QDV 的算子链具备与海康 VisionMaster 等价的**变量化参数**、**循环遍历**、**条件分支**、**流程合并**与**并行分支同步**能力。具体目标：

1. **变量系统升级**：`VariableManager` 由数值/字符串扩展到 ROI/Region/Points 三类几何变量，并对所有读写加递归互斥锁，支持预览子线程并发访问。
2. **变量运算算子扩展**：`VariableTool` 在原有算术/字符串运算基础上新增 `get/set/define/delete` 四个操作，直接对接全局变量管理器。
3. **循环算子扩展**：`LoopTool` 新增 `count`（计数循环）与 `while`（条件循环）两种模式。
4. **分支算子扩展**：`BranchControlTool` 新增 `and/or/not/xor` 逻辑运算与 `switch` 多分支选择。
5. **新增流程合并算子**：`FlowJoinTool` 作为并行分支同步点。
6. **执行器升级**：`ToolChainExecutor` 升级为编排引擎，支持子链循环、`trueBranch/falseBranch` 路由与并行分支同步。
7. **方案模型扩展**：`Scheme` 新增子链与并行分支的拥有权管理及序列化。

### 1.2 海康VM 对齐说明

| 海康VM 能力 | QDV v2.7.0 对应实现 | 对齐说明 |
| --- | --- | --- |
| 全局变量自定义工具（数值/字符串/ROI） | `VariableManager` + `VariableTool` | 类型枚举扩展至 `Roi/Region/Points`；运算算子支持 CRUD |
| 循环遍历（计数/条件） | `LoopTool` `count/while` 模式 | `count` 固定次数循环；`while` 条件表达式循环 |
| 条件分支（逻辑运算/多分支） | `BranchControlTool` | `and/or/not/xor` 逻辑运算；`switch` 多分支 |
| 流程合并/等待 | `FlowJoinTool` | 同步点算子，由执行器消费 `parallelBranchStatus` |
| 子流程/并行流程 | `ToolChainExecutor` + `Scheme` | 子链循环 + 并行分支 + `FlowJoin` 同步 |

### 1.3 架构总览

```
┌─────────────────────────────────────────────────────────────────┐
│                       Scheme（方案数据模型）                      │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────────────┐  │
│  │  主工具链     │  │  子链映射     │  │  并行分支映射          │  │
│  │  toolChain   │  │  m_subChains │  │  m_parallelBranches   │  │
│  └──────┬───────┘  └──────┬───────┘  └───────────┬───────────┘  │
│         │                 │                       │              │
│         │  拥有权(unique_ptr)│                       │              │
└─────────┼─────────────────┼───────────────────────┼──────────────┘
          │ 裸指针(NON-OWNING)│                       │
          ▼                 ▼                       ▼
┌─────────────────────────────────────────────────────────────────┐
│                  ToolChainExecutor（编排引擎）                   │
│  ┌──────────┐  ┌─────────────────┐  ┌────────────────────────┐  │
│  │ 线性执行  │  │ executeSubChain │  │ executeParallelBranches│  │
│  │ + 路由    │  │ (子链循环)      │  │ (并行分支+FlowJoin)    │  │
│  └────┬─────┘  └────────┬────────┘  └───────────┬────────────┘  │
│       │                 │                       │               │
│       └─────────────────┼───────────────────────┘               │
│                         ▼                                       │
│            ExecutionContext（贯穿执行的上下文）                   │
│  iterationIndex / currentRoi / upstreamResults /                │
│  parallelBranchStatus / depth (MAX_DEPTH=5)                     │
└─────────────────────────────┬───────────────────────────────────┘
                              │ 调用
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    VisionTool 算子族（执行单元）                 │
│  VariableTool │ LoopTool │ BranchControlTool │ FlowJoinTool ... │
└─────────────────────────────┬───────────────────────────────────┘
                              │ 读写
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│            VariableManager（全局变量管理器，线程安全）            │
│        QRecursiveMutex 保护 + ROI/Region/Points 类型扩展         │
└─────────────────────────────────────────────────────────────────┘
```

**所有权关系**：`Scheme` 通过 `unique_ptr` 持有主链/子链/并行分支的所有算子；`ToolChainExecutor` 仅持有裸指针（NON-OWNING），生命周期由 `Scheme` 管理。

---

## 2. VariableManager API

**头文件**：`include/Core/VariableManager.h`  
**命名空间**：`QDV`

### 2.1 功能描述

控制变量管理器，作为方案级全局变量的统一存储与解析中心。v2.7.0 升级要点：

- 类型枚举扩展至 `Roi/Region/Points`（对齐海康VM"全局变量自定义工具"）；
- 所有读写方法由 `QRecursiveMutex` 保护，支持 `PreviewManager` 子线程并发调用；
- 变量绑定解析 `${varName}` 支持算子输出变量名（含 `.` 与 `-`，用于 UUID 形式的 `nodeId.outputName`）。

### 2.2 类型枚举

```cpp
enum class Type {
    Int,        // 整数型
    Double,     // 浮点型
    String,     // 字符串型
    Bool,       // 布尔型
    Roi,        // ROI 区域型 {x,y,w,h}
    Region,     // Region 掩码型（多边形点集 QVariantList）
    Points      // 点集型（QPointF 列表 [{x,y}, ...]）
};
```

**类型字符串别名**（`stringToType` 接受，大小写不敏感）：

| Type | 合法字符串 |
| --- | --- |
| `Int` | `int`、`integer` |
| `Double` | `double`、`float`、`real` |
| `String` | `string`、`str`、`text` |
| `Bool` | `bool`、`boolean` |
| `Roi` | `roi`、`rect` |
| `Region` | `region` |
| `Points` | `points`、`point[]` |

**变量名规范**：`^[A-Za-z_][A-Za-z0-9_]*$`（首字符必须为字母或下划线，后续允许字母/数字/下划线）。重复创建返回 `false`。

> 注：通过 `registerOperatorOutput` 注册的算子输出变量名为 `<nodeId>.<outputName>` 形式（含 `.` 与 UUID 的 `-`），**绕过** `isValidName` 校验直接写入，与用户变量分离存放。

### 2.3 公共方法

#### 2.3.1 CRUD

| 方法签名 | 返回 | 说明 |
| --- | --- | --- |
| `bool createVariable(const QString& name, const QString& typeStr, const QVariant& value, const QString& description = QString())` | bool | 创建变量；名称非法或已存在返回 false。`value` 按类型转换，不匹配时使用类型默认值（int=0, double=0.0, bool=false, roi={0,0,100,100}, region/points=空列表） |
| `bool removeVariable(const QString& name)` | bool | 删除变量；不存在返回 false |
| `bool setValue(const QString& name, const QVariant& value)` | bool | 修改变量值；类型必须可转换，否则返回 false。值未变化时不发信号 |
| `bool setDescription(const QString& name, const QString& description)` | bool | 修改描述 |
| `QVariant value(const QString& name) const` | QVariant | 获取值；不存在返回无效 QVariant |
| `QVariantMap variable(const QString& name) const` | QVariantMap | 获取完整信息 `{name, type, value, description}`；不存在返回空 Map |
| `QVariantList variables() const` | QVariantList | 获取所有变量列表（同上结构） |
| `int count() const` | int | 变量数量 |
| `bool exists(const QString& name) const` | bool | 是否存在指定变量 |
| `void clear()` | void | 清空所有变量 |

#### 2.3.2 变量绑定解析

| 方法签名 | 说明 |
| --- | --- |
| `QString resolveBinding(const QString& input, bool* ok = nullptr) const` | 解析 `${varName}` 引用。正则：`\$\{([A-Za-z_0-9][A-Za-z0-9_.\-]*)\}`（支持算子输出变量名）。未定义变量保留原样，`*ok` 置 false。不含 `${}` 时原样返回 |
| `QVariant resolveVariant(const QVariant& input) const` | 递归解析 QVariant 中的引用（处理 Map/List/String）；含未定义变量返回原值 |
| `QStringList extractReferences(const QString& input) const` | 提取字符串中所有 `${varName}` 的变量名列表 |

#### 2.3.3 序列化

| 方法签名 | 说明 |
| --- | --- |
| `QJsonArray toJson() const` | 序列化为 JSON 数组（保存方案时调用）。每项 `{name, type, description, value}`，ROI 为 Object，Region/Points 为 Array |
| `bool fromJson(const QJsonArray& arr)` | 反序列化（加载方案时调用）；**会清空现有变量**再装载 |

#### 2.3.4 算子输出映射（v5.4 起，v2.7.0 沿用）

| 方法签名 | 说明 |
| --- | --- |
| `void registerOperatorOutput(const QString& nodeId, const QString& outputName, const QString& typeName = "string")` | 注册算子输出为全局变量，变量名为 `<nodeId>.<outputName>`。`typeName` 支持 `int/double/string/string[]/double[]/roi/rect/region/points/point[]`，数组类型归并为 String/Double |
| `void unregisterOperatorOutput(const QString& nodeId, const QString& outputName = QString())` | 反注册；`outputName` 为空时反注册该节点所有输出（前缀匹配 `<nodeId>.`） |
| `void updateOperatorOutputValues(const QString& nodeId, const QJsonObject& outputs)` | 更新已注册变量值（ToolChain 执行后调用）；仅更新已注册变量，不主动创建 |
| `bool isOperatorOutputRegistered(const QString& nodeId, const QString& outputName) const` | 查询是否已注册 |

#### 2.3.5 类型工具（静态）

| 方法签名 | 说明 |
| --- | --- |
| `static QString typeToString(Type t)` | 枚举转字符串（`int`/`double`/`string`/`bool`/`roi`/`region`/`points`） |
| `static Type stringToType(const QString& s, bool* ok = nullptr)` | 字符串转枚举（支持别名，见 2.2）；失败返回 `String` 且 `*ok=false` |
| `static bool isValidName(const QString& name)` | 校验变量名合法性 |

#### 2.3.6 ROI/Region/Points 辅助方法（静态，v2.7.0）

| 方法签名 | 说明 |
| --- | --- |
| `static QRectF toRoi(const QVariant& v)` | ROI 变量值 → `QRectF`；输入需为 `{x,y,w,h}` Map，空 Map 返回默认 QRectF |
| `static QVariant fromRoi(const QRectF& r)` | `QRectF` → ROI 变量值（`QVariantMap {x,y,w,h}`） |
| `static QList<QPointF> toPoints(const QVariant& v)` | 点集变量值 → `QList<QPointF>`；输入为 `[{x,y}, ...]`，每项需含 x/y |
| `static QVariant fromPoints(const QList<QPointF>& pts)` | `QList<QPointF>` → 点集变量值 |

### 2.4 信号

| 信号 | 触发时机 |
| --- | --- |
| `void variableCreated(const QString& name)` | 创建变量后 |
| `void variableRemoved(const QString& name)` | 删除变量后 |
| `void valueChanged(const QString& name, const QVariant& newValue)` | 变量值变化后（触发 `PreviewManager` 重运行） |
| `void descriptionChanged(const QString& name, const QString& newDesc)` | 描述变化后 |
| `void variablesChanged()` | 整体变更（清空/批量加载/创建/删除）后，QML 端刷新列表 |

### 2.5 线程安全说明

- 锁成员：`mutable QRecursiveMutex m_mutex`（递归互斥锁）。
- **选用递归锁的原因**：`resolveVariant` 内部会调用 `resolveBinding`，二者均加锁，同线程二次加锁需递归锁支持，否则会死锁。
- **加锁范围**：所有公共方法（含 `const` 方法）入口处 `QMutexLocker locker(&m_mutex)`。
- **并发场景**：`PreviewManager::doPreview` 通过 `QtConcurrent::run` 在子线程执行算子，算子内部调用 `VariableManager` 时由锁保护，避免 `QHash` 并发崩溃。
- **信号发射在锁内**：信号发射时仍持锁，连接方式若为 `Qt::DirectConnection`，槽函数执行期间也会阻塞其他线程访问；建议槽函数短小或使用 `Qt::QueuedConnection`。

### 2.6 使用示例

#### 示例 1：创建各类变量并读取

```cpp
#include "Core/VariableManager.h"
using namespace QDV;

VariableManager vm;

// 数值/字符串/布尔
vm.createVariable("thresh", "double", 128.0, "二值化阈值");
vm.createVariable("enablePreprocess", "bool", true);
vm.createVariable("reportTitle", "string", QStringLiteral("检测报告"));

// ROI 变量
QVariantMap roiMap;
roiMap["x"] = 100.0;
roiMap["y"] = 100.0;
roiMap["w"] = 640.0;
roiMap["h"] = 480.0;
vm.createVariable("detectRoi", "roi", roiMap, "检测区域");

// 点集变量
QVariantList pts;
QVariantMap p1; p1["x"] = 10.0; p1["y"] = 20.0;
pts.append(p1);
vm.createVariable("refPoints", "points", pts, "标定参考点");

// 读取并转换为 QRectF
const QRectF roi = VariableManager::toRoi(vm.value("detectRoi"));
// 读取并转换为 QList<QPointF>
const QList<QPointF> points = VariableManager::toPoints(vm.value("refPoints"));
```

#### 示例 2：变量绑定解析

```cpp
VariableManager vm;
vm.createVariable("thresh", "double", 128.0);
vm.createVariable("unit", "string", "px");

// 单变量引用
bool ok = false;
const QString s1 = vm.resolveBinding("${thresh}", &ok);  // "128"，ok=true

// 混合文本
const QString s2 = vm.resolveBinding("阈值=${thresh} ${unit}");  // "阈值=128 px"

// 递归解析 QVariantMap
QVariantMap params;
params["threshold"] = QStringLiteral("${thresh}");
params["label"]     = QStringLiteral("ROI_${unit}");
const QVariant resolved = vm.resolveVariant(params);
// → {"threshold": "128", "label": "ROI_px"}

// 提取引用列表
const QStringList refs = vm.extractReferences("a=${thresh}, b=${unit}");
// → ["thresh", "unit"]
```

#### 示例 3：序列化与加载

```cpp
VariableManager vm;
vm.createVariable("thresh", "double", 128.0);
vm.createVariable("detectRoi", "roi", roiMap);

// 保存
const QJsonArray arr = vm.toJson();
// 写入方案 JSON: scheme["variables"] = arr;

// 加载
VariableManager vm2;
vm2.fromJson(arr);  // 会先清空 vm2 再装载
```

#### 示例 4：注册算子输出供下游引用

```cpp
VariableManager vm;
const QString nodeId = QStringLiteral("a1b2c3d4-e5f6-7890-abcd-ef1234567890");

// 注册算子的 className 输出
vm.registerOperatorOutput(nodeId, "className", "string");
vm.registerOperatorOutput(nodeId, "confidence", "double");

// 执行后更新输出值
QJsonObject outputs;
outputs["className"]  = QStringLiteral("defect");
outputs["confidence"] = 0.92;
vm.updateOperatorOutputValues(nodeId, outputs);

// 下游算子通过 ${<nodeId>.className} 引用
const QString ref = QStringLiteral("${%1.className}").arg(nodeId);
const QString val = vm.resolveBinding(ref);  // "defect"
```

### 2.7 注意事项

1. **变量名规范**：用户变量必须匹配 `^[A-Za-z_][A-Za-z0-9_]*$`；算子输出变量名（含 `.`/`-`）通过 `registerOperatorOutput` 绕过校验直接写入，与用户变量分离存放。
2. **类型匹配**：`setValue` 对 Int/Double/Bool 强制类型转换校验，不匹配返回 false 且不修改；ROI 必须传 `QVariantMap`，Region/Points 必须传 `QVariantList`。
3. **值未变化不发信号**：`setValue`/`setDescription` 检测到值相同则直接返回 true，不触发 `valueChanged`，避免预览反复重运行。
4. **`fromJson` 会清空**：加载前会清空现有变量，谨慎使用。
5. **递归锁非重入安全保证**：递归锁解决同线程二次加锁死锁，但不意味着可重入修改；槽函数中再次修改变量需注意信号嵌套。
6. **数组类型归并**：`string[]`/`double[]` 在 `Type` 枚举中无对应项，归并为 String/Double，实际值以 QStringList/QVariantList 持有。

---

## 3. VariableTool API

**头文件**：`include/Vision/VariableTool.h`  
**命名空间**：`QDV`  
**继承**：`QDV::VisionTool`

### 3.1 功能描述

变量运算算子，用于在算子链中做轻量级数值/字符串处理，以及对全局变量进行 CRUD 操作。v2.7.0 新增 `get/set/define/delete` 四个操作，直接对接 `VariableManager`。

**使用前提**：必须通过 `setVariableManager` 注入 `VariableManager` 指针（由 `EditViewBridge` 在构建算子链时调用），否则 `get/set/define/delete` 操作返回失败。

### 3.2 支持的 operation

| operation | 类别 | 说明 |
| --- | --- | --- |
| `add` | 算术 | `operand1 + operand2`（数值加法） |
| `sub` | 算术 | `operand1 - operand2` |
| `mul` | 算术 | `operand1 * operand2` |
| `div` | 算术 | `operand1 / operand2`（除零返回 ok=false） |
| `mod` | 算术 | `fmod(operand1, operand2)`（取模除零返回 ok=false） |
| `concat` | 字符串 | 字符串拼接 `operand1 + operand2` |
| `format` | 字符串 | 按 `format` 模板格式化（printf 风格） |
| `get` | 变量 | 读取全局变量（`operand1`=变量名），结果写入 result |
| `set` | 变量 | 写入全局变量（`operand1`=变量名，`operand2`=值；可解析为 double 时按数值写入，否则按字符串） |
| `define` | 变量 | 创建全局变量（`operand1`=变量名，`operand2`=类型，`format`=初始值） |
| `delete` | 变量 | 删除全局变量（`operand1`=变量名） |

> 说明：任务描述中的 `variableName`/`variableType` 参数在实际代码中复用 `operand1`/`operand2` 字段，未单独开参数。`define` 操作的初始值复用 `format` 字段。

### 3.3 参数说明

| 参数名 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `operation` | QString | `"add"` | 运算类型，非法值回退 `add`。白名单：`add/sub/mul/div/mod/concat/format/get/set/define/delete` |
| `operand1` | QString | `"0"` | 操作数1。`get/set/define/delete` 时为变量名 |
| `operand2` | QString | `"0"` | 操作数2。`set` 时为待写入的值；`define` 时为类型字符串 |
| `format` | QString | `"%.2f"` | 格式化模板（`format` 操作）或 `define` 的初始值 |
| `outputVar` | QString | `"result"` | 输出变量名。结果会同时以 `result` 与 `outputVar` 为 key 写入 `ports` |

### 3.4 define 支持的类型

`define` 操作的 `operand2`（类型字符串）支持以下值（大小写不敏感）：

| 类型字符串 | 默认初始值 |
| --- | --- |
| `int` | `initValue.toInt()`（`initValue` 即 `format` 字段） |
| `double` | `initValue.toDouble()` |
| `bool` | `initValue.toLower()=="true" \|\| initValue=="1"` |
| `roi` | `{x:0, y:0, w:100, h:100}`（QVariantMap） |
| `region` | 空 `QVariantList` |
| `points` | 空 `QVariantList` |
| `string` 或未知 | `initValue` 原字符串 |

### 3.5 输出端口

| 端口名 | 类型 | 方向 | 说明 |
| --- | --- | --- | --- |
| `result` | `Any` | Out | 运算结果（数值或字符串） |
| `image` | `Image` | In | 输入图像（透传到 overlay） |

执行结果同时写入 `result.ports["result"]`、`result.ports[outputVar]`（若 `outputVar != "result"`）以及 `result.data["result"]`。`result.data` 还包含 `operation/operand1/operand2/outputVar` 用于调试。

### 3.6 使用示例

#### 示例 1：算术运算

```cpp
#include "Vision/VariableTool.h"
using namespace QDV;

VariableTool tool;
QJsonObject params;
params["operation"] = "add";
params["operand1"]  = "3.14";
params["operand2"]  = "2.0";
params["outputVar"] = "sum";
tool.configure(params);

ToolResult result;
tool.execute(inputImage, result);
// result.ports["result"] == 5.14
// result.ports["sum"]    == 5.14
```

#### 示例 2：字符串格式化

```cpp
QJsonObject params;
params["operation"] = "format";
params["operand1"]  = "0.956";   // 主参数
params["operand2"]  = "defect";  // 附加参数
params["format"]    = "[%s] score=%.2f";
tool.configure(params);
// → "[defect] score=0.96"
```

#### 示例 3：define + set + get 全局变量

```cpp
// 前提：已通过 setVariableManager 注入 vm
tool.setVariableManager(&vm);

// 1) 定义变量
QJsonObject def;
def["operation"] = "define";
def["operand1"]  = "defectCount";   // 变量名
def["operand2"]  = "int";           // 类型
def["format"]    = "0";             // 初始值
tool.configure(def);
tool.execute(input, result);  // vm 中创建 defectCount=0

// 2) 写入变量
QJsonObject setOp;
setOp["operation"] = "set";
setOp["operand1"]  = "defectCount";
setOp["operand2"]  = "5";
tool.configure(setOp);
tool.execute(input, result);  // vm.defectCount = 5

// 3) 读取变量
QJsonObject getOp;
getOp["operation"] = "get";
getOp["operand1"]  = "defectCount";
tool.configure(getOp);
tool.execute(input, result);
// result.ports["result"] == 5
```

#### 示例 4：delete 全局变量

```cpp
QJsonObject del;
del["operation"] = "delete";
del["operand1"]  = "defectCount";
tool.configure(del);
tool.execute(input, result);  // vm 中删除 defectCount
// 若变量不存在，result.ok=false，result.data["error"] 提示原因
```

### 3.7 注意事项

1. **VariableManager 注入是必须的**：`get/set/define/delete` 操作未注入时直接返回 `ok=false`，错误信息为 `"VariableManager 未注入"`。
2. **数值解析**：`add/sub/mul/div/mod` 要求 `operand1`/`operand2` 可解析为 double（支持科学计数法），否则失败。
3. **除零保护**：`div` 与 `mod` 在 `|operand2| < 1e-12` 时返回失败。
4. **`set` 的类型自适应**：`operand2` 可解析为 double 时按数值写入，否则按字符串写入；若目标变量类型不匹配（如目标是 int 但传入非数值字符串），`setValue` 会失败。
5. **`define` 重复创建失败**：变量名已存在时 `createVariable` 返回 false，`result.ok=false`。
6. **非法 operation 回退**：`configure` 收到非法 `operation` 时回退为 `add` 并打印警告。

---

## 4. LoopTool API

**头文件**：`include/Vision/LoopTool.h`  
**命名空间**：`QDV`  
**继承**：`QDV::VisionTool`

### 4.1 功能描述

循环遍历算子，生成 ROI 列表供 `ToolChainExecutor` 对每个 ROI 切片执行子链。本算子仅负责产出 ROI 列表与 overlay 可视化，**不直接执行子链**——子链执行由 `ToolChainExecutor::executeSubChain` 编排。

v2.7.0 新增两种模式：
- `count`：计数循环，固定次数 N 次循环，输出 N 个全图占位 ROI；
- `while`：条件循环，生成最大迭代数个占位 ROI，由执行器在每次迭代后评估 `whileCondition` 决定是否停止。

### 4.2 支持的 mode

| mode | 说明 |
| --- | --- |
| `grid` | 将输入图像按 `gridCols×gridRows` 切分（含 overlap），生成 ROI 列表 |
| `list` | 解析 `roiList` 字符串 `"x,y,w,h;x,y,w,h;..."` |
| `detection` | 按检测结果生成 ROI（当前无检测输入通道，**降级为 grid**） |
| `count` | 计数循环，生成 `count` 个全图 ROI 占位（含 `index` 字段） |
| `while` | 条件循环，生成 `maxWhileIterations` 个全图 ROI 占位（含 `isWhile=true` 标记） |

### 4.3 参数说明

| 参数名 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `mode` | QString | `"grid"` | 遍历模式。白名单：`grid/list/detection/count/while`，非法值回退 `grid` |
| `gridCols` | int | `3` | 网格列数（必须 >0） |
| `gridRows` | int | `3` | 网格行数（必须 >0） |
| `gridOverlap` | int | `0` | 网格重叠像素（非负，相邻单元格向彼此延伸 overlap/2） |
| `roiList` | QString | `""` | `list` 模式的 ROI 列表字符串，格式 `"x,y,w,h;x,y,w,h;..."`，分隔符支持中英文逗号/分号 |
| `startIndex` | int | `0` | 起始索引（非负，超出范围回退 0） |
| `maxIterations` | int | `100` | 最大迭代数（保护性上限，钳制到 [1, 10000]） |
| `count` | int | `1` | **v2.7.0** `count` 模式的循环次数（钳制到 [1, 10000]） |
| `whileCondition` | QString | `""` | **v2.7.0** `while` 模式的条件表达式（如 `"count>5"`），由执行器评估 |
| `maxWhileIterations` | int | `1000` | **v2.7.0** `while` 模式最大迭代数（防死循环，钳制到 [1, 100000]） |

### 4.4 count 模式说明

- 生成 `count` 个全图 ROI 占位（`x=0, y=0, w=input.cols, h=input.rows`），每个 ROI 携带 `index` 字段表示迭代索引。
- 实际子链执行由 `ToolChainExecutor::executeSubChain` 编排：对每个 ROI 切片（全图）执行子链。
- 适用场景：固定次数的批处理、统计采样、定长序列生成等不需要空间切分的循环。

### 4.5 while 模式说明

- 生成 `maxWhileIterations` 个全图 ROI 占位，每个 ROI 携带 `index` 与 `isWhile=true` 标记。
- **当前简化实现**：LoopTool 仅生成最大迭代数的占位 ROI，实际条件判断由 `ToolChainExecutor` 在每次迭代后评估 `whileCondition` 决定是否提前停止。
- `whileCondition` 为字符串表达式（如 `"count>5"`、`"defectCount>=3"`），表达式语义由执行器侧解析（本版本 LoopTool 不解析表达式）。
- 防死循环保护：`maxWhileIterations` 钳制到 [1, 100000]，超出上限被截断。

### 4.6 子链执行机制

LoopTool 自身不执行子链，工作流程如下：

1. `ToolChainExecutor::execute` 调用 `executeTool(loopTool, ...)` 得到 `loopResult`（含 `roiList`）；
2. 若 `m_subChains` 中存在该 LoopTool 的子链映射，调用 `executeSubChain(loopTool, input, loopResult, ctx)`；
3. `executeSubChain` 遍历 `roiList`，对每个 ROI 从原图裁剪切片，**为每次迭代创建独立 Tool 实例**（`ToolFactory::createTool` + `deserialize`），执行子链算子序列；
4. 每次迭代结果聚合到 `loopResult.data["iterations"]`（QJsonArray）与 `m_iterationResults[loopToolId]`；
5. 发射 `subChainIterationStarted/subChainIterationCompleted` 信号。

### 4.7 输出端口

| 端口名 | 类型 | 方向 | 说明 |
| --- | --- | --- | --- |
| `roiList` | `Points` | Out | ROI 列表，每个元素 `{x,y,w,h}`（count/while 模式额外含 `index`/`isWhile`） |
| `count` | `Number` | Out | ROI 总数 |
| `image` | `Image` | In | 输入图像（用于 grid 切分与 overlay 绘制） |

`result.data` 额外包含：`mode, count, currentIndex, startIndex, maxIterations, gridCols, gridRows, gridOverlap`。子链执行后追加 `iterations`（QJsonArray）与 `iterationCount`。

### 4.8 使用示例

#### 示例 1：grid 网格切分

```cpp
LoopTool loop;
QJsonObject params;
params["mode"]        = "grid";
params["gridCols"]    = 4;
params["gridRows"]    = 4;
params["gridOverlap"] = 20;  // 相邻网格重叠 20px
loop.configure(params);

ToolResult result;
loop.execute(inputImage, result);
// result.ports["roiList"] → 16 个 ROI
// result.ports["count"]   → 16
// result.overlayImage 上绘制黄色 ROI 框 + 索引
```

#### 示例 2：list 自定义 ROI

```cpp
QJsonObject params;
params["mode"]   = "list";
params["roiList"] = "10,10,200,200; 220,10,200,200; 10,220,200,200";
loop.configure(params);
// → 3 个 ROI
```

#### 示例 3：count 计数循环

```cpp
QJsonObject params;
params["mode"]  = "count";
params["count"] = 10;
loop.configure(params);
// → 10 个全图 ROI 占位（index=0..9）
// 配合 ToolChainExecutor 子链：每个 ROI 执行一次子链，共 10 次
```

#### 示例 4：while 条件循环

```cpp
QJsonObject params;
params["mode"]               = "while";
params["whileCondition"]     = "defectCount<3";  // 由执行器评估
params["maxWhileIterations"] = 100;              // 防死循环上限
loop.configure(params);
// → 100 个全图 ROI 占位（isWhile=true），执行器按条件提前停止
```

### 4.9 注意事项

1. **`detection` 模式当前降级**：无检测输入通道，自动降级为 `grid` 并打印 info 日志。
2. **`list` 解析为空降级**：`list` 模式 ROI 解析为空时降级为 `grid` 并打印警告。
3. **`maxIterations` 上限**：grid 模式生成 ROI 数量达到 `maxIterations` 即停止，防止内存爆炸。
4. **ROI 边界保护**：`executeSubChain` 中对 ROI 坐标做 `std::max/std::min` 钳制，确保落在输入图像范围内。
5. **`while` 条件评估不在 LoopTool 内**：本版本 LoopTool 仅生成占位 ROI，条件评估由执行器侧负责；若执行器未实现条件评估，将按 `maxWhileIterations` 跑满。
6. **overlay 绘制**：在输入图像上绘制黄色矩形框 + 索引文字（黑色描边），单通道图像自动转 BGR。

---

## 5. BranchControlTool API

**头文件**：`include/Vision/BranchControlTool.h`  
**命名空间**：全局（非 QDV）  
**继承**：`QDV::VisionTool`

### 5.1 功能描述

条件分支控制算子，评估条件后输出 `conditionMet` 供 `ToolChainExecutor` 进行 `trueBranch/falseBranch` 路由。v2.7.0 新增：

- 逻辑运算 `and/or/not/xor`：基于上游算子的 `conditionMet` 结果进行逻辑组合；
- 多分支选择 `switch`：基于整数输入匹配分支序号。

### 5.2 conditionOp 支持列表

> **重要**：逻辑运算算子在代码中为**小写**（`and/or/not/xor`），任务描述中的大写形式仅为文档可读性。配置时请使用小写。

| 类别 | 算子 | 说明 |
| --- | --- | --- |
| 原有比较 | `==`、`!=`、`>`、`>=`、`<`、`<=` | 数值比较，委托 `BranchNode::evaluate` |
| 原有状态 | `ok`、`ng` | 基于上游 `result.ok` 判断 |
| 别名 | `ge`、`le`、`ne` | `>=`、`<=`、`!=` 的语义别名 |
| 字符串 | `contains`、`startswith`、`endswith` | 字符串包含/前缀/后缀匹配 |
| 集合 | `in` | 左值在 `conditionValue` 列表中（逗号/分号分隔） |
| **v2.7.0 逻辑** | `and`、`or`、`not`、`xor` | 基于上游 `conditionMet` 的逻辑运算 |
| **v2.7.0 多分支** | `switch` | 基于 `switchValue` 匹配 `branchCases` |

### 5.3 参数说明

| 参数名 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `conditionOp` | QString | — | 比较算子（见 5.2）。配置时也可用 `op` 键，`configure` 会做映射 |
| `conditionValue` | QVariant | — | 比较阈值或参考值。配置时也可用 `value` 键 |
| `source` | QString | — | 上游算子 toolId，作为左值来源 |
| `trueBranch` | QStringList | — | 条件成立时执行的 toolId 列表（数组或逗号/换行分隔字符串） |
| `falseBranch` | QStringList | — | 条件不成立时执行的 toolId 列表 |
| `switchValue` | QString | `""` | **v2.7.0** `switch` 模式的输入值（整数索引字符串） |
| `branchCases` | QString | `""` | **v2.7.0** 分支列表，格式 `"case1:toolId1,toolId2;case2:toolId3"` |
| `logicInputs` | QString | `""` | **v2.7.0** 逻辑运算输入列表，格式 `"nodeId1:expectedBool1,nodeId2:expectedBool2"` |

### 5.4 AND/OR/NOT/XOR 逻辑运算说明

`logicInputs` 格式为 `"nodeId1:expectedBool1,nodeId2:expectedBool2"`，每个输入项为 `nodeId:expectedBool`，执行器从 `result.data["logicResults"]` 中查找 `nodeId` 对应的 `conditionMet` 值，与 `expectedBool`（`true`/`1` 为真，其余为假）比较。

| 算子 | 语义 |
| --- | --- |
| `and` | 所有输入条件均满足（`actualBool == expectedBool`）时为 true |
| `or` | 任意输入条件满足时为 true |
| `not` | 取反第一个输入条件；输入列表为空时返回 true |
| `xor` | 两个输入条件结果不一致时为 true；输入数 <2 时返回 false |

> **当前实现限制**：`evaluateLogicInput` 从 `result.data["logicResults"]` 查找上游结果，需要上游算子的结果已聚合到该字段。完整的跨算子结果查询应通过 `ExecutionContext::upstreamResults`，本版本为简化实现。

### 5.5 switch 多分支选择说明

- `switchValue` 为整数索引字符串（如 `"2"`）；
- `branchCases` 格式：`"case1:toolId1,toolId2;case2:toolId3"`（分号分隔不同 case，冒号后为该 case 的 toolId 列表，逗号分隔）；
- **当前实现**：`BranchControlTool::evaluateCondition` 对 `switch` 仅返回 `switchValue` 与 `branchCases` 均非空（即配置有效），实际分支路由由 `ToolChainExecutor` 消费 `conditionMet` + `switchValue` + `branchCases` 完成。

### 5.6 输出端口

| 端口名 | 类型 | 方向 | 说明 |
| --- | --- | --- | --- |
| `conditionMet` | `Bool` | Out | 比较条件是否成立 |
| `operation` | `String` | Out | 比较算子名 |
| `image` | `Image` | In | 输入图像（透传到 overlay） |

`result.data` 额外包含：`conditionMet, operation, conditionValue`。`switch` 模式追加 `switchValue/branchCases`；逻辑运算模式追加 `logicInputs/logicOp`。

### 5.7 使用示例

#### 示例 1：数值比较（原有算子）

```cpp
BranchControlTool branch;
QJsonObject params;
params["conditionOp"]    = ">=";
params["conditionValue"] = 0.9;
params["source"]         = "det_score";  // 上游算子 toolId
params["trueBranch"]     = QJsonArray{"pass_action", "log_ok"};
params["falseBranch"]    = QJsonArray{"reject_action"};
branch.configure(params);

ToolResult result;
branch.execute(input, result);
// result.ports["conditionMet"] == (上游 det_score 的 score >= 0.9)
```

#### 示例 2：AND 逻辑运算

```cpp
// 场景：两个上游分支条件必须同时成立
QJsonObject params;
params["conditionOp"] = "and";
params["logicInputs"] = "det_a:true,det_b:true";
// det_a 与 det_b 的 conditionMet 均需为 true
branch.configure(params);
```

#### 示例 3：NOT 逻辑运算

```cpp
QJsonObject params;
params["conditionOp"] = "not";
params["logicInputs"] = "det_a:true";
// det_a 的 conditionMet 为 false 时，整体为 true
branch.configure(params);
```

#### 示例 4：switch 多分支

```cpp
QJsonObject params;
params["conditionOp"] = "switch";
params["switchValue"] = "2";  // 选择 case 2
params["branchCases"] = "0:idle_action;1:run_action;2:alarm_action;3:reset_action";
branch.configure(params);
// 执行器消费 switchValue=2 → 路由到 alarm_action
```

### 5.8 注意事项

1. **逻辑算子大小写**：代码中为**小写** `and/or/not/xor`，配置时请使用小写；大写形式不会被识别（会回退 `BranchNode::evaluate` 并打印警告）。
2. **参数名映射**：`configure`/`deserialize` 会将 `conditionOp` → `op`、`conditionValue` → `value`，兼容元数据字段名与 `BranchNode` 内部键名。
3. **`switch` 路由由执行器完成**：算子本身仅输出 `switchValue/branchCases`，实际分支选择在 `ToolChainExecutor` 侧消费。
4. **左值来源优先级**：`result.data["value"]` → `result.data["count"]` → `result.score`。
5. **`ge/le/ne` 别名**：与 `>=`/`<=`/`!=` 语义等价，`ne` 优先数值比较，无法解析时回退字符串比较。
6. **未知算子兜底**：未识别的 `conditionOp` 会打印警告并回退 `BranchNode::evaluate`。

---

## 6. FlowJoinTool API

**头文件**：`include/Vision/FlowJoinTool.h`  
**命名空间**：`QDV`  
**继承**：`QDV::VisionTool`

### 6.1 功能描述

流程合并/等待算子，作为并行分支的同步点。典型场景："图像采集预处理"与"从数据库读取产品参数"两条并行分支，需全部完成后才启动正式检测。

**实现方式**：
- 算子本身不执行图像处理，仅透传输入图像并在 overlay 标注 "JOIN"；
- `ToolChainExecutor` 执行到 `FlowJoin` 时，检查 `ExecutionContext::parallelBranchStatus`，若所有等待分支完成则继续，否则等待；
- 当前为简化版，实际并行分支管理由 `ToolChainExecutor::executeParallelBranches` 编排。

### 6.2 参数说明

| 参数名 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `waitBranches` | QString | `""` | 等待的分支 ID 列表，逗号分隔（如 `"branch1,branch2,branch3"`） |
| `timeoutMs` | int | `5000` | 超时时间（毫秒），必须 >0 |

### 6.3 并行分支同步机制

1. `Scheme` 通过 `addParallelBranch(branchId, tools)` 注册并行分支算子；
2. `ToolChainExecutor::setParallelBranches` 接收分支映射；
3. 执行到 `FlowJoin` 时，`executeParallelBranches` 为每条分支创建独立 Tool 实例（`ToolFactory::createTool` + `deserialize`），通过 `QtConcurrent::blockingMap` 并行执行；
4. 每条分支使用独立的 `ToolChainExecutor subExecutor`，避免 `m_results` 竞争；
5. 所有分支完成后，`FlowJoin` 透传最后一个分支的结果，主链继续。

### 6.4 输出端口

| 端口名 | 类型 | 方向 | 说明 |
| --- | --- | --- | --- |
| `image` | `Image` | Out | 合并后透传的输入图像 |
| `joined` | `Bool` | Out | 所有等待分支是否已合并完成 |
| `waitBranches` | `String` | Out | 等待的分支 ID 列表（逗号分隔） |
| `image` | `Image` | In | 输入图像（透传到 overlay） |

`result.data` 包含 `waitBranches` 与 `timeoutMs`。

### 6.5 使用示例

```cpp
FlowJoinTool join;
QJsonObject params;
params["waitBranches"] = "preprocess,db_load";
params["timeoutMs"]    = 3000;
join.configure(params);

ToolResult result;
join.execute(input, result);
// result.ports["joined"] == true（当前实现固定为 true，实际同步由执行器完成）
// result.overlayImage 标注 "JOIN"
```

并行分支注册（在 Scheme/Executor 侧）：

```cpp
// Scheme 侧：注册两条并行分支
scheme.addParallelBranch("preprocess", std::move(preprocessTools));
scheme.addParallelBranch("db_load",    std::move(dbLoadTools));

// Executor 侧：注入分支映射
executor.setParallelBranches(scheme.parallelBranchPtrs());
// 执行到 FlowJoin 时，executeParallelBranches 并行执行两条分支
```

### 6.6 注意事项

1. **算子本身不阻塞**：`execute` 仅透传图像与配置，实际等待逻辑在 `ToolChainExecutor` 侧；当前简化版 `joined` 固定为 true。
2. **超时未生效**：本版本 `timeoutMs` 仅作为配置存储，未实际实现超时中断；后续版本将在执行器侧实现。
3. **分支独立 Tool 实例**：并行执行时每条分支通过 `ToolFactory` 创建独立 Tool 实例，避免状态竞争（P1-B12 架构约束）。
4. **`blockingMap` 阻塞调用**：`executeParallelBranches` 使用 `QtConcurrent::blockingMap`，调用线程会阻塞直到所有分支完成。
5. **空输入图像失败**：`execute` 收到空图像时返回 `ok=false`。

---

## 7. ToolChainExecutor 架构

**头文件**：`include/Vision/ToolChainExecutor.h`  
**命名空间**：全局（非 QDV）  
**继承**：`QObject`

### 7.1 功能概述

v2.7.0 将 `ToolChainExecutor` 从纯线性执行器升级为编排引擎，新增能力：

1. **子链循环执行**：`LoopTool` 输出 ROI 列表后，对每个 ROI 切片执行子链；
2. **`trueBranch/falseBranch` 路由**：`BranchControlTool` 评估条件后选择分支执行；
3. **多分支选择**：基于整数输入匹配分支序号（switch/case 语义，算子侧输出，执行器侧消费）；
4. **并行分支 + FlowJoin 同步**：多分支并行执行，FlowJoin 等待全部完成；
5. **ExecutionContext**：贯穿执行链的上下文对象，支持跨算子数据传递。

### 7.2 公共方法

| 方法签名 | 说明 |
| --- | --- |
| `void setTools(const QList<QDV::VisionTool*>& tools)` | 设置主链算子列表（NON-OWNING） |
| `void setBranches(const QMap<QString, BranchNode*>& branches)` | 设置分支节点映射（key=source toolId） |
| `void setSubChains(const QMap<QString, QList<QDV::VisionTool*>>& subChains)` | **v2.7.0** 设置子链映射（key=LoopTool toolId，value=子链算子列表，NON-OWNING） |
| `void setParallelBranches(const QMap<QString, QList<QDV::VisionTool*>>& branches)` | **v2.7.0** 设置并行分支映射（key=分支起始 toolId） |
| `bool execute(const cv::Mat& input)` | 同步执行工具链；原子检查防止重入 |
| `QFuture<bool> executeAsync(const cv::Mat& input)` | 异步执行（`QtConcurrent::run`） |
| `void stop()` | 停止执行（设置 `m_running=false`） |
| `QMap<QString, ToolResult> getResults() const` | 获取所有算子结果 |
| `ToolResult getResult(const QString& toolId) const` | 获取指定算子结果 |
| `QList<ToolResult> getIterationResults(const QString& loopToolId) const` | **v2.7.0** 获取子链迭代结果（按迭代索引顺序） |

### 7.3 子链循环执行机制（executeSubChain）

```
executeSubChain(loopTool, input, loopResult, ctx):
  1. 检查 ctx.canRecurse()（depth < MAX_DEPTH=5），超限返回 false
  2. 从 loopResult.data["roiList"] 提取 ROI 列表
  3. 查找 m_subChains[loopTool->id()]，无子链则返回 true（跳过）
  4. for i in 0..rois.size():
       a. 发射 subChainIterationStarted(loopToolId, i, total)
       b. ROI 边界保护（钳制到图像范围内）
       c. 从 input 裁剪 ROI 切片
       d. 为子链每个算子创建独立实例（ToolFactory::createTool + deserialize）
       e. 创建子上下文 ctx.createChild(i, total, rois[i])
       f. 调用 executeBranchSequence(iterTools, roiInput, childCtx)
       g. 聚合结果到 iterationResults
       h. 释放迭代 Tool 实例（qDeleteAll）
       i. 发射 subChainIterationCompleted(loopToolId, i, iterResult)
  5. 聚合到 loopResult.data["iterations"] 与 loopResult.data["iterationCount"]
  6. 取最后一次迭代的 overlayImage 作为 loopResult.overlayImage
  7. 存储到 m_iterationResults[loopToolId]
```

**关键设计**：
- **独立 Tool 实例**：每次迭代通过 `ToolFactory::createTool(proto->type())` + `deserialize(proto->serialize())` 创建独立实例，避免并行/循环场景下的状态竞争（P1-B12 约束）。
- **递归深度保护**：`ExecutionContext::MAX_DEPTH=5`，防止子链嵌套导致的无限递归。
- **边界保护**：ROI 坐标用 `std::max/std::min` 钳制，确保 `cv::Rect` 落在图像范围内。

### 7.4 trueBranch/falseBranch 路由

在 `execute` 主循环中，每个算子执行完成后检查 `branchesCopy`：

```
if branchesCopy.contains(tool->id()):
    branch = branchesCopy[tool->id()]
    if branch.valid:
        conditionMet = evaluateBranch(branch)  // 委托 BranchNode::evaluate
        targetBranch = conditionMet ? branch.trueBranchToolIds
                                     : branch.falseBranchToolIds
        if not targetBranch.empty():
            # 有分支工具：执行选中分支
            executeBranchSequence(branchTools, currentInput, ctx)
            # 分支最后结果覆盖 currentInput，主链继续（不 break）
        elif not conditionMet:
            break  # 条件不满足且无 falseBranch → 保留原 break 行为
        # 条件满足但无 trueBranch → 主链继续（不 break）
    else:
        if not evaluateBranch(branch): break  # 分支无效，保留原行为
```

**与原行为兼容**：原行为是"条件不满足 → break"；新行为在配置了 `falseBranch` 时改为路由到 false 分支，无 `falseBranch` 时保留 break。

### 7.5 并行分支执行（executeParallelBranches）

```
executeParallelBranches(branchIds, input, ctx):
  1. 为每条分支创建独立 Tool 实例（ToolFactory::createTool + deserialize）
  2. 使用 QtConcurrent::blockingMap 并行执行：
       for each branch i (并行):
         ToolChainExecutor subExecutor;  # 独立 executor 避免 m_results 竞争
         results[i] = subExecutor.executeBranchSequence(branchCopies[i], input, ctx)
  3. 释放分支 Tool 实例（qDeleteAll）
  4. 检查所有分支是否成功
```

**关键设计**：
- **独立 subExecutor**：每条分支创建独立 `ToolChainExecutor`，避免 `m_results` 数据竞争；
- **`blockingMap` 阻塞**：调用线程阻塞直到所有分支完成；
- **状态隔离**：每条分支的 Tool 实例独立创建，不共享状态。

### 7.6 ExecutionContext 说明

**头文件**：`include/Vision/ExecutionContext.h`  
**命名空间**：`QDV`

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `iterationIndex` | int | `0` | 当前迭代索引（非循环场景为 0） |
| `currentRoi` | QVariant | — | 当前迭代的 ROI（`{x,y,w,h}`），子链切片用 |
| `totalIterations` | int | `1` | 循环总迭代数（非循环场景为 1） |
| `upstreamResults` | `const QMap<QString, ToolResult>*` | `nullptr` | 上游所有算子结果（NON-OWNING，指向 `m_results`） |
| `iterationResults` | `QList<QVariantMap>` | — | 子链每次迭代的结果（聚合用） |
| `parentLoopId` | QString | `""` | 当前所属父 LoopTool 节点 ID |
| `parallelBranchStatus` | `QMap<QString, bool>` | — | 并行分支同步状态（FlowJoin 等待的分支 ID → 是否完成） |
| `depth` | int | `0` | 递归深度（子链嵌套场景） |

**静态常量**：`static constexpr int MAX_DEPTH = 5`（对齐 AGENTS.md III.2 步数限制）

**方法**：
- `bool canRecurse() const`：`depth < MAX_DEPTH`
- `ExecutionContext createChild(int iterIdx, int total, const QVariant& roi) const`：创建子上下文（`depth+1`，继承 `upstreamResults` 与 `parentLoopId`）

**设计要点**：
- **线程安全**：context 为栈对象，不跨线程共享；并行分支各自拷贝独立 context；
- **轻量级**：仅持有指针和 QVariant，不拷贝 `cv::Mat`；
- **向后兼容**：旧算子不消费 context，`execute` 签名不变时无感知。

### 7.7 信号

| 信号 | 说明 |
| --- | --- |
| `void toolExecuted(const QString& toolId, const ToolResult& result)` | 单个算子执行完成 |
| `void chainCompleted(bool success)` | 工具链执行完成（success=无失败且至少执行一个算子） |
| `void executionProgress(int current, int total)` | 执行进度 |
| `void toolFailed(const QString& toolId, const QString& errorMessage)` | 单个算子执行失败（透传具体错误信息） |
| `void subChainIterationStarted(const QString& loopToolId, int index, int total)` | **v2.7.0** 子链迭代开始 |
| `void subChainIterationCompleted(const QString& loopToolId, int index, const ToolResult& result)` | **v2.7.0** 子链迭代完成 |

### 7.8 使用示例

#### 示例 1：完整执行链（含子链与分支）

```cpp
ToolChainExecutor executor;
executor.setTools(scheme.toolPtrs());           // 主链
executor.setBranches(scheme.branches());        // 分支节点
executor.setSubChains(scheme.subChainPtrs());   // 子链映射
executor.setParallelBranches(scheme.parallelBranchPtrs());  // 并行分支

QObject::connect(&executor, &ToolChainExecutor::subChainIterationStarted,
                 [](const QString& loopId, int idx, int total) {
    qDebug() << "子链迭代:" << loopId << idx << "/" << total;
});
QObject::connect(&executor, &ToolChainExecutor::toolFailed,
                 [](const QString& id, const QString& msg) {
    qWarning() << "工具失败:" << id << msg;
});

const bool ok = executor.execute(inputImage);
if (!ok) {
    qWarning() << "工具链执行失败";
}

// 查询子链迭代结果
const QList<ToolResult> iters = executor.getIterationResults("loop_1");
for (int i = 0; i < iters.size(); ++i) {
    qDebug() << "迭代" << i << "ok=" << iters[i].ok;
}
```

#### 示例 2：异步执行

```cpp
QFuture<bool> future = executor.executeAsync(inputImage);
// 不阻塞主线程，future.result() 获取结果
```

#### 示例 3：停止执行

```cpp
// 在另一个线程中调用
executor.stop();  // 主循环检测到 m_running=false 后退出
```

### 7.9 注意事项

1. **重入保护**：`execute` 通过 `m_running.exchange(true)` 原子检查，防止多线程同时执行；重入时打印警告并返回 false。
2. **主线程执行提示**：在主线程调用 `execute` 时打印 info 日志（建议使用 `executeAsync`）。
3. **失败计数**：`failCount` 统计失败算子数，链失败时返回 false 并打印错误日志。
4. **`executeBranchSequence` 的 `m_running` 处理**：子执行器场景下 `m_running` 默认 false，方法内通过 `RunningGuard` 临时设置为 true，执行结束后恢复。
5. **子链失败不中断主链**：`executeSubChain` 失败时仅 `failCount++` 并发射 `toolFailed`，主链继续执行（结果已在 `loopResult` 中）。
6. **NON-OWNING 指针**：`setTools/setBranches/setSubChains/setParallelBranches` 传入的指针非拥有，生命周期由 `Scheme` 管理，Executor 析构时不释放。

---

## 8. Scheme 数据模型

**头文件**：`include/Core/Scheme.h`  
**命名空间**：全局（非 QDV）

### 8.1 功能概述

方案数据模型，持有工具链、分支节点、子链、并行分支的**拥有权**（`unique_ptr`）。v2.7.0 新增子链与并行分支管理。

### 8.2 子链管理

| 方法签名 | 说明 |
| --- | --- |
| `void addSubChain(const QString& loopToolId, std::vector<std::unique_ptr<QDV::VisionTool>> tools)` | 添加子链（LoopTool id → 子链算子列表，Scheme 接管所有权） |
| `void removeSubChain(const QString& loopToolId)` | 移除子链（含算子释放） |
| `QMap<QString, QList<QDV::VisionTool*>> subChainPtrs() const` | 获取所有子链的裸指针映射（供 `ToolChainExecutor` 使用，NON-OWNING） |
| `QList<QDV::VisionTool*> subChainPtrs(const QString& loopToolId) const` | 获取指定 LoopTool 的子链算子裸指针列表 |

### 8.3 并行分支管理

| 方法签名 | 说明 |
| --- | --- |
| `void addParallelBranch(const QString& branchId, std::vector<std::unique_ptr<QDV::VisionTool>> tools)` | 添加并行分支（分支起始 toolId → 分支算子列表，Scheme 接管所有权） |
| `void removeParallelBranch(const QString& branchId)` | 移除并行分支（含算子释放） |
| `QMap<QString, QList<QDV::VisionTool*>> parallelBranchPtrs() const` | 获取所有并行分支的裸指针映射（供 `ToolChainExecutor` 使用） |

### 8.4 序列化/反序列化

#### 序列化（`serialize`）

方案 JSON 中新增两个字段：

```json
{
  "subChains": {
    "<loopToolId>": [
      { "id": "...", "type": "Threshold", ... },
      { "id": "...", "type": "BlobDetect", ... }
    ]
  },
  "parallelBranches": {
    "<branchId>": [
      { "id": "...", "type": "GaussFilter", ... }
    ]
  }
}
```

- `subChains`：key 为 LoopTool 的 toolId，value 为子链算子序列化数组；
- `parallelBranches`：key 为分支起始 toolId，value 为分支算子序列化数组；
- 每个算子通过 `tool->serialize()` 序列化。

#### 反序列化（`deserialize`）

- `m_subChains.clear()` 后重新装载；
- 遍历 `subChains` 对象，对每个 loopId 的算子数组，通过 `ToolFactory::createTool(type)` 创建实例并 `deserialize`，`emplace_back` 到 `std::vector<unique_ptr<VisionTool>>`；
- `m_parallelBranches` 同理；
- 算子创建失败时跳过该算子（不中断整体反序列化）。

### 8.5 其他方法（沿用既有）

| 方法签名 | 说明 |
| --- | --- |
| `QDV::VisionTool* getTool(const QString& toolId) const` | 获取主链算子 |
| `void addTool(std::unique_ptr<QDV::VisionTool> tool)` | 添加主链算子 |
| `void removeTool(const QString& toolId)` | 移除主链算子 |
| `QList<QDV::VisionTool*> toolPtrs() const` | 主链算子裸指针列表 |
| `size_t toolCount() const` | 主链算子数量 |
| `void addBranch(std::unique_ptr<BranchNode> branch)` | 添加分支节点 |
| `QMap<QString, BranchNode*> branches() const` | 分支节点映射 |
| `QJsonObject serialize() const` | 序列化整个方案 |
| `bool deserialize(const QJsonObject& data)` | 反序列化 |
| `QDV::Result<void> tryDeserialize(const QJsonObject& data)` | 反序列化（带校验） |
| `static bool validateJsonSchema(const QJsonObject& data)` | JSON Schema 校验 |

### 8.6 使用示例

#### 示例 1：构建含子链与并行分支的方案

```cpp
Scheme scheme("myScheme");

// 主链：采集 → 循环遍历 → 分支判断 → 输出
scheme.addTool(std::make_unique<GrabImageTool>());
scheme.addTool(std::make_unique<LoopTool>());
scheme.addTool(std::make_unique<BranchControlTool>());
scheme.addTool(std::make_unique<OutputTool>());

// LoopTool 的子链：对每个 ROI 切片执行阈值 + Blob 检测
std::vector<std::unique_ptr<QDV::VisionTool>> subChain;
subChain.emplace_back(std::make_unique<ThresholdTool>());
subChain.emplace_back(std::make_unique<BlobDetectTool>());
scheme.addSubChain("loop_1", std::move(subChain));

// 并行分支：预处理 + 数据库加载
std::vector<std::unique_ptr<QDV::VisionTool>> preprocessBranch;
preprocessBranch.emplace_back(std::make_unique<GaussFilterTool>());
scheme.addParallelBranch("preprocess", std::move(preprocessBranch));

std::vector<std::unique_ptr<QDV::VisionTool>> dbBranch;
dbBranch.emplace_back(std::make_unique<ScriptTool>());  // 模拟数据库加载
scheme.addParallelBranch("db_load", std::move(dbBranch));

// 序列化保存
const QJsonObject json = scheme.serialize();
// 写入文件...
```

#### 示例 2：注入 Executor 并执行

```cpp
ToolChainExecutor executor;
executor.setTools(scheme.toolPtrs());
executor.setBranches(scheme.branches());
executor.setSubChains(scheme.subChainPtrs());
executor.setParallelBranches(scheme.parallelBranchPtrs());

const bool ok = executor.execute(inputImage);
```

#### 示例 3：加载方案

```cpp
Scheme scheme;
const QJsonObject json = loadJsonFromFile("scheme.qdvproj");
if (scheme.deserialize(json)) {
    // 子链与并行分支已自动装载
    ToolChainExecutor executor;
    executor.setTools(scheme.toolPtrs());
    executor.setSubChains(scheme.subChainPtrs());
    executor.setParallelBranches(scheme.parallelBranchPtrs());
    executor.execute(inputImage);
}
```

### 8.7 注意事项

1. **所有权清晰**：`Scheme` 通过 `unique_ptr` 持有所有算子；`ToolChainExecutor` 仅持有裸指针，不参与生命周期管理。
2. **`addSubChain` 覆盖**：相同 `loopToolId` 重复调用会覆盖旧子链（旧算子被释放）。
3. **反序列化容错**：单个算子创建失败时跳过，不中断整体加载；建议加载后校验算子数量。
4. **`subChainPtrs` 返回裸指针**：供 `ToolChainExecutor` 使用，**不要释放**这些指针。
5. **序列化字段向后兼容**：旧方案无 `subChains/parallelBranches` 字段时，反序列化跳过这两部分，不影响主链加载。

---

## 附录：版本对齐速查表

| 模块 | v2.7.0 新增能力 | 关键 API |
| --- | --- | --- |
| `VariableManager` | ROI/Region/Points 类型 + 线程安全 | `Type::Roi/Region/Points`、`QRecursiveMutex`、`toRoi/fromRoi/toPoints/fromPoints` |
| `VariableTool` | get/set/define/delete 操作 | `operation="get/set/define/delete"`、`setVariableManager` |
| `LoopTool` | count/while 模式 | `mode="count/while"`、`count/whileCondition/maxWhileIterations` |
| `BranchControlTool` | and/or/not/xor 逻辑运算 + switch 多分支 | `conditionOp="and/or/not/xor/switch"`、`logicInputs/switchValue/branchCases` |
| `FlowJoinTool` | 全新算子，流程合并/等待 | `waitBranches/timeoutMs` |
| `ToolChainExecutor` | 子链循环 + 分支路由 + 并行分支 | `setSubChains/setParallelBranches`、`executeSubChain/executeBranchSequence/executeParallelBranches`、`getIterationResults` |
| `Scheme` | 子链/并行分支拥有权管理 + 序列化 | `addSubChain/removeSubChain/subChainPtrs`、`addParallelBranch/removeParallelBranch/parallelBranchPtrs` |
| `ExecutionContext` | 执行上下文，跨算子数据传递 | `iterationIndex/currentRoi/upstreamResults/parallelBranchStatus/depth`、`createChild` |

---

**文档结束**  
如需了解算子开发规范，请参考 `docs/algorithms/算子开发指南.md`；  
如需了解海康VM对齐细节，请参考 `docs/海康VisionMaster 自定义参数与逻辑控制算子手册.md`。
