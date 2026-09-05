# 零样本检测模块易用性优化 — 测试与验证操作指导

> 适用版本：本次优化改动（引导条、控件提示、按钮联动、结果面板空状态、新手文档）
> 读者对象：测试人员 / 接入工程师 / 质量验收人员
> 目标：按本指引独立完成"改动是否生效、功能是否完整"的验证，**无需阅读源码**。

---

## 0. 本次改动清单（验证范围）

| 编号 | 改动位置 | 改动内容 | 验证方式 |
| --- | --- | --- | --- |
| A | `src/UI/ZeroShotDetectView.cpp` | 顶部新增**分步新手引导条**（①选模型→②加载模型→③加载图像→④推理），随状态实时更新 | 手动 UI + 单测 |
| B | `third_party/ZeroShotKit/ui/ZeroShotPanel.cpp` | 全控件**通俗 tooltip**；模型未加载时**推理按钮置灰**；路径为空给出明确指引 | 手动 UI |
| C | `third_party/ZeroShotKit/ui/ZeroShotResultPanel.cpp` | 结果区**空状态改为分步操作引导**；异常分数/检测表/掩码/热力图加通俗提示 | 手动 UI |
| D | `include/UI/ZeroShotGuide.h`（新增） | 引导逻辑**纯函数模块**（步骤状态机、HTML 生成、文案、术语解释） | 单测 |
| E | `tests/UI/test_zeroshot_guide.cpp`（新增） | 12 组引导逻辑单元测试 | 单测 |
| F | `docs/UserGuide.md`、`docs/FAQ.md` | 新手手册 + 常见问题文档 | 文档查阅 |

> 说明：推理引擎（Kit/Engine/ORT）**未改动**，检测精度与行为与优化前一致。本验证只确认"易用性增强生效"，不验证算法精度。

---

## 1. 测试前准备与环境确认

### 1.1 确认代码已更新到本次改动

在 `E:\anchor\Trae\QDV` 根目录执行（任选其一）：

- **Git 用户**：确认工作区包含上述 6 个文件改动
  ```bash
  git status --short
  ```
  应看到 `include/UI/ZeroShotGuide.h`、`tests/UI/test_zeroshot_guide.cpp` 等文件为新增/修改状态。
- **非 Git 用户**：直接确认以下文件存在且修改时间在本次优化日期之后：
  - `third_party/ZeroShotKit/docs/UserGuide.md`
  - `third_party/ZeroShotKit/docs/FAQ.md`
  - `include/UI/ZeroShotGuide.h`

### 1.2 确认构建工具链就绪

本项目的构建环境（**缺一不可**）：

| 组件 | 期望路径 / 版本 | 检查命令 |
| --- | --- | --- |
| Qt 6 | `D:\Qt_new\6.11.1\mingw_64` | `ls /d/Qt_new/6.11.1/mingw_64/bin/qmake.exe` |
| MinGW 编译器 | `D:\Qt_new\Tools\mingw1310_64\bin` | `ls /d/Qt_new/Tools/mingw1310_64/bin/g++.exe` |
| CMake | `D:\Qt_new\Tools\CMake_64\bin` | `ls /d/Qt_new/Tools/CMake_64/bin/cmake.exe` |
| 构建目录 | `E:\anchor\Trae\QDV\build`（已初始化） | `ls /e/anchor/Trae/QDV/build/CMakeCache.txt` |

> 若工具链路径不同，请按本机实际路径替换下文命令中的 `D:/Qt_new/...`。

### 1.3 确认构建目录已配置（首次或切换分支后）

```bash
export PATH="/d/Qt_new/Tools/mingw1310_64/bin:/d/Qt_new/6.11.1/mingw_64/bin:/d/Qt_new/Tools/CMake_64/bin:$PATH"
cd /e/anchor/Trae/QDV/build
cmake .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="D:/Qt_new/6.11.1/mingw_64"
```

预期：末尾出现 `-- Configuring done` 与 `-- Generating done`，**无报错**。

### 1.4 内存与并行限制（重要）

本机沙箱对并行编译大测试文件有内存上限。若直接 `-j4` 编译 `QDV_tests`，可能报 `cc1plus: out of memory`。**请一律使用 `-j1` 串行构建测试目标**（不影响最终产物，仅慢一些）。

---

## 2. 自动化测试（验证改动 D / E：引导逻辑）

> 用途：用代码断言确认"分步状态计算、引导文案、术语解释"逻辑正确，**无需打开界面**。

### 2.1 编译测试目标

```bash
export PATH="/d/Qt_new/Tools/mingw1310_64/bin:/d/Qt_new/6.11.1/mingw_64/bin:/d/Qt_new/Tools/CMake_64/bin:$PATH"
cd /e/anchor/Trae/QDV/build
mingw32-make QDV_tests -j1
```

预期：
- 编译过程持续 3~8 分钟（串行），末尾出现 `Linking CXX executable QDV_tests.exe` 或 `[100%] Built target QDV_tests`。
- **若中途报 `cc1plus: out of memory`**：关闭其他占用内存的程序后重试；仍失败则改为逐文件编译（见 2.4 备用法）。

### 2.2 运行测试

> ⚠️ **不要直接双击 `QDV_tests.exe` 启动！** 如果你的系统 PATH 里 `D:\Qt_new\Tools\QtCreator\bin` 排在 `D:\Qt_new\6.11.1\mingw_64\bin` 前面，Windows 会加载 Qt Creator 自带的旧版本 `Qt6Test.dll`，导致"无法定位程序输入点"弹窗。这是 DLL 版本冲突，不是测试代码问题。
>
> 正确做法：在命令行中**先把正确的 Qt/MinGW 路径放到 PATH 最前面**，再启动；或双击已生成的 `run_QDV_tests.bat`。

**方式一：命令行（推荐）**

```bash
export PATH="/d/Qt_new/6.11.1/mingw_64/bin:/d/Qt_new/Tools/mingw1310_64/bin:$PATH"
cd /e/anchor/Trae/QDV/build/bin
./QDV_tests.exe
```

PowerShell 版本：

```powershell
$env:PATH = "D:\Qt_new\6.11.1\mingw_64\bin;D:\Qt_new\Tools\mingw1310_64\bin;" + $env:PATH
cd E:\anchor\Trae\QDV\build\bin
.\QDV_tests.exe
```

**方式二：双击启动脚本**

直接双击 `E:\anchor\Trae\QDV\build\bin\run_QDV_tests.bat`。
该脚本会自动修正 PATH 并运行 `QDV_tests.exe`，运行结束后按任意键关闭窗口。

> 说明：本项目测试框架（catch2_minimal）**不支持用例过滤器**，运行会执行全部用例（数百个）。请用下方命令**只看与本改动相关的部分**。

### 2.3 观察与判定（只看零样本引导用例）

```bash
./QDV_tests.exe 2>&1 | grep -E "ZeroShotGuide|PASS|FAIL|=====|passed|failed" | tail -40
```

**判定标准（全部满足 = 通过）：**

1. 输出中包含以下 12 个 `ZeroShotGuide` 用例名（即全部被执行）：
   - `ZeroShotGuide: 初始状态仅步骤①完成`
   - `ZeroShotGuide: 模型加载后步骤②完成`
   - `ZeroShotGuide: 图像加载后步骤③完成`
   - `ZeroShotGuide: 有结果后四步全部完成`
   - `ZeroShotGuide: nextPendingStep 推导`
   - `ZeroShotGuide: 引导 HTML 含步骤标记与状态`
   - `ZeroShotGuide: 全部完成后提示完成`
   - `ZeroShotGuide: 空状态引导包含完整步骤指引`
   - `ZeroShotGuide: 前置条件提示组合`
   - `ZeroShotGuide: 模型类型通俗解释`
   - `ZeroShotGuide: 常用术语通俗解释`
   - `ZeroShotGuide: 步骤文案与详细说明`
2. **没有任何一行包含 `FAIL`** 或 `FAILED`。
3. 末尾汇总中 `[FAIL]` 计数为 **0**，`[PASS]` 计数 ≥ 744（含本次新增 12 组）。

> 通过即代表：引导状态机、HTML 标记（✔/▶/○）、空状态文本、前置条件提示四种组合、模型类型与术语通俗解释——逻辑层面全部正确。

### 2.4 备用法：仅编译/运行新增测试文件（跳过全量）

若只想快速验证本次新增逻辑（不必等全量编译）：

```bash
# 仅编译单个测试对象（验证语法与逻辑可编译）
cd /e/anchor/Trae/QDV/build
mingw32-make -f tests/CMakeFiles/QDV_tests.dir/build.make \
  tests/CMakeFiles/QDV_tests.dir/UI/test_zeroshot_guide.cpp.obj
```

预期：命令结束无报错，退出码 0。该 `.obj` 文件出现在 `build/tests/CMakeFiles/QDV_tests.dir/UI/`。

---

## 3. 手动 UI 验证（验证改动 A / B / C：界面与提示）

> 用途：在真实界面上确认"新手无需查文档即可理解每一步"。需要**先构建并启动主程序**。

### 3.1 构建并启动主程序

```bash
export PATH="/d/Qt_new/Tools/mingw1310_64/bin:/d/Qt_new/6.11.1/mingw_64/bin:/d/Qt_new/Tools/CMake_64/bin:$PATH"
cd /e/anchor/Trae/QDV/build
mingw32-make QDV -j1        # 主程序目标名以 CMakeLists 为准，常见为 QDV 或 app
```

构建成功后启动（示例）：

```bash
cd /e/anchor/Trae/QDV/build/bin
./QDV.exe
```

### 3.2 进入零样本检测模块（验证入口）

1. 程序启动后，查看**左侧导航栏**。
2. 找到带 `✨` 图标的 **"零样本检测"** 按钮（导航索引第 8 项）。
3. 点击它 → 主区域切换到零样本检测视图。

> 该视图为「左：配置面板 ｜ 中：图像与结果 ｜ 右：模型注意事项」三栏布局。后续所有验证均在此视图内完成。

### 3.3 逐项验证清单（建议按顺序做）

#### ✅ 验证 A1：新手引导条（顶部，4 步状态）

| 操作 | 预期表现 | 判定 |
| --- | --- | --- |
| 刚进入视图（未加载任何东西） | 顶部出现一行 **"新手引导"**：①✔绿色 ②○灰 ③○灰 ④○灰，下方提示"下一步：② 加载模型…" | 通过 / 失败 |
| 在面板点【加载模型】成功 | ② 变为 ✔绿色，提示跳到"③ 加载图像" | 通过 / 失败 |
| 点顶部【加载图像】选 1 张图 | ③ 变为 ✔绿色，提示跳到"④ 开始推理" | 通过 / 失败 |
| 点【推理当前】出结果 | ④ 变为 ✔绿色，提示变为"（全部完成：可导出结果…）" | 通过 / 失败 |

**判定标准**：每一步的"已完成 ✔ / 下一步 ▶琥珀 / 未完成 ○灰"与实际状态严格对应；提示文字随步骤推进实时变化。任一步错位即判失败。

#### ✅ 验证 B1：控件悬停提示（tooltip）

将鼠标**悬停**（不点击）在以下控件 1~2 秒，应弹出通俗说明：

| 控件 | 应出现的提示要点 |
| --- | --- |
| 模型类型下拉框 | 四种模型各自"适合做什么"（如 AnomalyCLIP：判断有没有缺陷） |
| 模型路径输入框 / 【浏览...】 | 选择模型目录的说明 |
| 异常阈值滑块 | 调高更宽松（漏报多）、调低更严格（误报多），建议 0.5 起 |
| 检测阈值滑块 | 置信度门槛，调低显示更多、调高更少 |
| NMS 滑块 | 去重设置，建议默认 0.45 |
| 量化复选框 | 压缩模型、省显存但精度略降 |
| 【加载模型】按钮 | 加载模型到引擎 |
| 【推理当前】/【推理全部】按钮 | 对当前图 / 整个目录推理 |

**判定标准**：每个关键控件悬停均有提示，且用语为"大白话"（如"漏报/误报"而非仅"阈值"）。无任何控件缺失提示即判通过。

#### ✅ 验证 B2：推理按钮联动（防误点）

| 操作 | 预期表现 | 判定 |
| --- | --- | --- |
| 未加载模型时点【推理当前】 | 弹出**明确指引弹窗**："检测前需要两步准备：1.先加载模型 2.再加载图像"，而非无声无息或笼统报错 | 通过 / 失败 |
| 仅加载模型、未加载图像时点推理 | 弹窗指明"还没有待检测的图像，请点击加载图像" | 通过 / 失败 |
| 模型未加载时，【推理当前】按钮 | 按钮处于**置灰（禁用）**状态，不可点击 | 通过 / 失败 |
| 模型加载成功后 | 【推理当前】按钮**恢复可点击**（变亮） | 通过 / 失败 |

**判定标准**：缺少前置条件时绝不"静默失败"；要么按钮置灰，要么弹窗给分步指引。二者至少满足其一且提示清晰。

#### ✅ 验证 C1：结果面板空状态引导

| 操作 | 预期表现 | 判定 |
| --- | --- | --- |
| 刚进入、未推理时看右侧/中间结果区 | 不再显示旧版"暂无零样本推理结果"；改为**多行分步指引**："还没有检测结果。完成以下 4 步…1.选模型 2.加载模型 3.加载图像 4.推理当前" | 通过 / 失败 |
| 悬停异常分数仪表 / 检测表 / 掩码 / 热力图区域 | 出现通俗说明（如"异常分数超过阈值显示红色"、"掩码：目标轮廓"） | 通过 / 失败 |

**判定标准**：空状态文案为"可操作的步骤指引"而非空洞提示；结果相关控件均有解释。

#### ✅ 验证 F：文档可查

在文件管理器中打开 `third_party/ZeroShotKit/docs/`，确认：
- `UserGuide.md`（新手手册：4 步快速上手、模型选型表、4 个典型场景、术语大白话表）
- `FAQ.md`（14 条常见问题：模型加载失败 5 类原因、误报/漏报调参、提示词写法等）

打开任一文档，确认内容可读、步骤清晰。**判定标准**：两份文档均存在且非空。

### 3.4 完整手动验证速查表

| 项 | 入口 | 通过后现象 | 失败现象 |
| --- | --- | --- | --- |
| A 引导条 | 视图顶部 | 4 步 ✔/▶/○ 实时正确 | 步骤状态错乱、不更新 |
| B1 tooltip | 悬停各控件 | 弹通俗说明 | 无提示 / 仍是术语 |
| B2 按钮联动 | 缺前置条件点推理 | 置灰 或 弹窗指引 | 静默无反应 / 笼统报错 |
| C1 空状态 | 结果区初始 | 多行分步指引 | 仍显"暂无结果" |
| F 文档 | docs 目录 | 两份文档可读 | 缺失 / 空白 |

> 全部 5 项通过 → 手动验证通过。

---

## 4. 异常记录与反馈

发现任何不符合"判定标准"的现象，请按以下格式记录并反馈，便于精准定位。

### 4.1 必须记录的信息

1. **现象描述**：具体看到了什么（截图最佳）。
2. **复现步骤**：从"进入零样本检测"到出错点，逐步写下点击了什么。
3. **环境信息**：Qt 版本、构建方式（是否 `-j1`）、操作系统。
4. **日志**：程序运行时的终端输出，以及 `E:\anchor\Trae\QDV\logs\` 目录下的最新 `.log` 文件（命名含日期）。
5. **测试结论**：标注上述哪一项（A/B1/B2/C1/F 或单测用例名）未通过。

### 4.2 反馈方式

- **内部 Issue / 工单**：标题格式 `【零样本易用性验证】<现象简述>`，正文附 4.1 五项内容。
- **日志与截图位置**：
  - 运行日志：`E:\anchor\Trae\QDV\logs\`（排查模型加载失败、推理异常首选）
  - 崩溃转储：`E:\anchor\Trae\QDV\logs\*.dmp`（如有）
- **单测失败**：把 `QDV_tests.exe` 的完整输出（含 `FAIL` 行与断言表达式）贴入反馈。框架会打印失败的表达式与预期/实际值。

### 4.3 常见误判（先自查，避免无效反馈）

| 你看到的 | 是否算失败 | 说明 |
| --- | --- | --- |
| 推理按钮置灰 | **否** | 这是 B2 的预期行为（未加载模型时禁用） |
| 点推理弹"请先加载模型" | **否** | 这是 B2 的预期引导弹窗 |
| 空状态显示"完成以下 4 步" | **否** | 这是 C1 的预期引导文案 |
| 悬停无提示 | **是** | tooltip 应全覆盖关键控件 |
| 引导条步骤序号与实际不符 | **是** | 状态机逻辑错误，需修 |

### 4.4 启动时弹窗"无法定位程序输入点 / 找不到入口"

**典型弹窗内容**：

- `无法定位程序输入点 _ZN10QSignalSpy4wait... 于动态链接库 QDV_tests.exe 上`
- `无法定位程序输入点 ?machineHostName@QSysInfo... 于动态链接库 D:\Qt_new\Tools\QtCreator\bin\Qt6Test.dll 上`

**根因**：系统 PATH 里有多个 Qt 版本，Windows 优先加载了 Qt Creator 自带的 `Qt6Test.dll`（与 MinGW 构建的 exe ABI 不兼容）。

**解决方法（按推荐顺序）**：

1. **双击 `run_QDV_tests.bat`**（已放在 `build\bin\` 下，自动修正 PATH）。
2. **命令行启动**：先 `export PATH` 或 `$env:PATH = ...` 把 `D:\Qt_new\6.11.1\mingw_64\bin` 放到最前，再运行 `QDV_tests.exe`。
3. **根治 PATH 顺序**（可选）：在系统环境变量中，把 `D:\Qt_new\6.11.1\mingw_64\bin` 上移到 `D:\Qt_new\Tools\QtCreator\bin` 之前，然后重新打开资源管理器再双击 exe。

**注意**：此问题与本次代码改动无关，属于 Qt 多版本共存时的 Windows DLL 搜索顺序问题。

---

## 5. 验证通过结论模板

验证完成后，输出一句结论 + 清单：

```
零样本检测模块易用性优化验证结论：
- 自动化单测：12 组 ZeroShotGuide 用例全 PASS，[FAIL]=0
- 手动 UI：A 引导条 / B1 tooltip / B2 按钮联动 / C1 空状态 / F 文档 全部通过
- 总体判定：通过 ✅（或：未通过，见 <项>）
```

---

## 附：关键路径速查

| 内容 | 路径 |
| --- | --- |
| 引导逻辑源码 | `E:\anchor\Trae\QDV\include\UI\ZeroShotGuide.h` |
| 单元测试 | `E:\anchor\Trae\QDV\tests\UI\test_zeroshot_guide.cpp` |
| 测试启动脚本（修正 PATH） | `E:\anchor\Trae\QDV\build\bin\run_QDV_tests.bat` |
| 测试可执行 | `E:\anchor\Trae\QDV\build\bin\QDV_tests.exe` |
| 主程序可执行 | `E:\anchor\Trae\QDV\build\bin\QDV.exe` |
| 新手手册 | `E:\anchor\Trae\QDV\third_party\ZeroShotKit\docs\UserGuide.md` |
| 常见问题 | `E:\anchor\Trae\QDV\third_party\ZeroShotKit\docs\FAQ.md` |
| 运行日志 | `E:\anchor\Trae\QDV\logs\` |
