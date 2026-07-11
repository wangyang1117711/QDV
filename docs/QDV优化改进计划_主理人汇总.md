# QDV 工程综合评价与优化改进计划（主理人汇总）

- **团队**：software-code-audit（主理人 齐活林 / 架构师 高见远 / QA 严过关）
- **范围**：`E:\anchor\Trae\QDV` — Qt6.8 + QML + C++17，CMake + MSVC，电芯视觉检测客户端（OpenCV / ONNX / SQLite / 自研加密）
- **方法**：探索 Agent 事实摸底 → 架构师 5 维评审（含代码证据 + Mermaid 目标架构图）→ QA 4 维评审（实测 `QDV_tests.exe` + 读 `auto_test/*.log` 取证）。所有结论可溯源到 `文件:行`。
- **配套文档**：`docs/QDV架构安全评审_高见远.md`（架构师详细报告，含两张 Mermaid 图）

---

## 一、综合评分矩阵（6 维度）

| 维度 | 评分(1-10) | 等级 | 一句话评语 |
|------|-----------|------|-----------|
| 代码质量 | 6 | C | 命名/日志规范一致，但历史注释污染(~63处)、魔法数字、裸指针所有权不清、孤儿依赖拉低一致性 |
| 架构设计 | 6 | C+ | 模块分包清晰、算子/插件/Scheme 三套扩展点优秀；但上帝对象 + 双 DB 单例破坏内聚 |
| 性能表现 | 5 | C | 主线程同步推理阻塞 UI、cv::Mat 双拷贝是明确瓶颈；QCache 缓存与异步序列化是亮点 |
| 安全性 | 2 | F | 硬编码密钥 + XOR 伪加密 + 登录可 bypass + DLL 无校验 + QML 调试敞开 + DB 明文，几乎全线失守 |
| 可维护性 | 5 | C− | 文档量充分；但构建混乱、孤儿依赖、无 CI、注释噪音削弱整体质量 |
| 可扩展性 | 7 | B− | 算子/插件/Scheme 扩展点清晰；仅设备端(串口)与训练推理为残桩 |
| **综合** | **≈5.5** | C | 功能骨架扎实、测试存量可观（实测 300 例全过），工程化（CI/门禁/依赖一致性）与安全可靠性是最大短板 |

---

## 二、与历史背景不符的关键澄清（务必先读）

团队成员**逐一核实**了项目记忆中的若干"已完成"声明，多数与源码现状不符，需修正认知：

| # | 历史背景描述 | 实测结论 | 证据 |
|---|------------|---------|------|
| 1 | 已完成 `DatabaseManager` 单例 + `SchemeRepository`/`SchemeModel` | **不存在**。真实数据层是 `ResultDatabase` + `DatabaseIntegrator` 两个重叠单例 | Grep 0 命中；`src/Database/ResultDatabase.cpp:26`、`DatabaseIntegrator.cpp:23` |
| 2 | 采用 AES-256-CBC 加密 | **实为自研 XOR 流密码**，无完整性校验，密钥一半来自源码常量 | `src/Core/AuthService.cpp:12`（硬编码种子）、`:371-392`（`plaintext^key^iv`） |
| 3 | 已实现 `resolveQtPluginPaths()` + PATH 注入防护 | **不存在**（Grep 0 命中），仅 `qputenv("QT_QUICK_CONTROLS_STYLE","Basic")` | `apps/SmartVision/main.cpp` |
| 4 | 登录认证已实现 | **默认关闭**（`kLoginEnabled=false`）且可用环境变量 `QDV_FORCE_LOGIN` 强制开关 | `main.cpp:73`、`:80-85` |
| 5 | 27/27 Catch2 测试通过 | **实测 300/300 通过**，但框架是**自研 `catch2_minimal.hpp`**（非真 Catch2）；`cmake/Dependencies.cmake` 里 `find_package(Catch2 3.0 REQUIRED)` 是死代码 | 实跑 `build/bin/QDV_tests.exe`；`tests/` 自研 harness |
| 6 | `spdlog` 为日志库 | **死依赖**：`Logger` 为自定义 Qt 文件日志，无 spdlog 引用；`Dependencies.cmake` 整个文件未被任何 `CMakeLists.txt` 包含（孤儿） | `cmake/Dependencies.cmake:23`；`include/Core/Logger.h` |
| 7 | `auto_test/*.log` 是测试 | **是运行时 UI 冒烟 + 截图日志**，非单元测试 | 读 `auto_test/auto.log` |

> 结论：项目实际安全水位**低于**历史背景描述的"D 级"——加密实现比记录的更弱，且数据层/路径防护等描述存在夸大。建议以本次实测为准更新项目记忆。

---

## 三、优化改进计划（问题 → 目标 → 措施 → 优先级）

### 🔴 P0 — 发布前必须修复（安全 + 工程化门禁，约 12–20 人日）

| 编号 | 维度 | 问题 | 优化目标 | 具体措施（含证据） | 工作量 |
|------|------|------|---------|-------------------|--------|
| S1 | 安全 | XOR 伪加密 + 硬编码种子，密文可还原 | 达到"密文不可逆、无密钥不可解密、防篡改" | 改用 **AES-256-GCM**（OpenSSL `EVP_aes_256_gcm`，12B 随机 IV + 16B tag）；密钥由用户口令经 PBKDF2/HKDF 派生；已有 `QMessageAuthenticationCode` include 但未用，并入 HMAC | 3–5d |
| S2 | 安全 | 登录默认关闭 + 环境变量 bypass | 生产构建认证默认开启且不可被环境变量绕过 | `main.cpp:73` 改为 `true`；移除 `:80-85` 的 `QDV_FORCE_LOGIN` 开关；增加失败限流 | 1–2d |
| S3 | 安全 | 动态 DLL 加载无签名/哈希校验（RCE 风险） | 仅加载经签名/白名单的插件 | 加载前 `WinVerifyTrust` 或 SHA-256 + 内嵌公钥校验 manifest（已有 `PluginVerifier` 可复用） | 3–5d |
| S4 | 安全 | QML 调试器可附加篡改状态 | 发布构建禁止 QML 调试 | 发布配置定义 `QT_NO_QML_DEBUGGER`（`build/CMakeCache.txt:1160` `qml_debug=ON`） | 0.5d |
| P1 | 性能/可靠性 | UI 入口同步 `execute()` 阻塞主线程做 ONNX 推理 | 推理不卡 UI，主线程零阻塞 | `EditViewBridge.cpp:1311`/`:1444` 改用已存在的 `executeAsync()` + `QFutureWatcher`，信号回传结果；UI 入口禁止同步 `execute()` | 2–3d |
| M4 | 可维护性 | 无 CI，测试靠手动跑 | 提交即测，回归有人守门 | GitHub Actions：MSVC+Qt6+OpenCV → `cmake --build` → `ctest --output-on-failure`，失败阻合并 | 2–4d |

> ⚠️ P0 中"安全 F 级"与"无 CI"必须**同步起步**：安全修复若没有 CI 回归屏障，等于没修。

### 🟠 P1 — 本迭代（质量与正确性，约 25–40 人日）

| 编号 | 维度 | 问题 | 优化目标 | 具体措施 | 工作量 |
|------|------|------|---------|---------|--------|
| S5 | 安全 | SQLite 明文存储业务数据 | 数据库静态加密 | 引入 SQLCipher（`PRAGMA key`） | 2–3d |
| P2 | 性能 | `InferenceEngine.infer` 同步、无内部线程 | 推理在工作者线程执行 | 在 `executeTool` 内用 `QtConcurrent` 派发 | 2–4d |
| P3 | 性能 | cv::Mat 多次 clone（按值捕获 + 内部 clone） | 大图仅一次拷贝 | 改 `std::shared_ptr<cv::Mat>`/move，去冗余 clone（`ToolChainExecutor.cpp:41,104`） | 1–2d |
| R4 | 可靠性/扩展 | 训练推理为 mock（QRandomGenerator 生成结果） | 接真实 ONNX 后端或显式演示开关 | `TrainingInferenceView.cpp:890-901` 接真实 `InferenceEngine`（TODO M7）；未就绪时 UI 显式标注"演示模式" | 2–3d |
| A1 | 架构 | `EditViewBridge` 上帝对象（1735 行） | 职责拆分、可测试 | 拆 `SchemeIO`/`SingleOperatorRunner`/`ImageProcessor`/`VariableBridge`/`ROIManager`/`UndoController`，`EditViewBridge` 仅做适配（分阶段，先阶段1） | 5–8d |
| A2 | 架构 | `ResultDatabase` 与 `DatabaseIntegrator` 双单例重叠 | 单一数据门面 | 保留 `DatabaseIntegrator` 为唯一门面，降级 `ResultDatabase` 为内部实现、删除公开 `instance()` | 2–3d |
| R9 | 代码质量 | 裸指针所有权依赖 `EditViewBridge` | 明确所有权、消除悬垂/泄漏 | `ToolChainExecutor` 的 `QList<VisionTool*>`/`QMap<QString,BranchNode*>` 改 `std::unique_ptr`/`QSharedPointer` | 1–2d |
| R12 | 可维护性 | `Dependencies.cmake` 死依赖 + 清单漂移 | 依赖清单与真实一致 | 删除或真实引入 spdlog/Catch2/nlohmann；引入 vcpkg/conan 锁版本 | 0.5–1d |
| R13 | 安全 | 路径无 `cleanPath` 防穿越 | 模型/图像路径白名单 + 规范化 | 对 `modelPath`/`filePath` 做 `canonicalFilePath` + allow-list 校验 | 1–2d |
| M1 | 可维护性 | 6 构建目录 + ~10 脚本 + 硬编码 `D:/opencv` 等绝对路径 | 可移植、单一可靠构建 | `CMakePresets.json` + `find_package` + `CACHE` 默认空；清理冗余构建目录/脚本 | 3–5d |
| M2 | 可维护性 | 孤儿 `Dependencies.cmake` 未被包含却 REQUIRE | 构建配置自洽 | 统一进根 `CMakeLists.txt` 或删除 | 1–2d |
| R5 | 可靠性/扩展 | `SerialCommunicator` 为存根（虚假连接） | 真实串口通信或显式禁用 | 接 `QtSerialPort`；过渡期显式告警"串口未实现" | 2–3d |

### 🟡 P2 — 后续（长尾重构）

| 编号 | 维度 | 问题 | 措施 | 工作量 |
|------|------|------|------|--------|
| S6 | 安全 | 路径穿越白名单补全 | 全量用户派生路径 canonicalize | 1d |
| S7 | 安全 | HMAC 完整性（并入 S1） | 接 GCM tag / HMAC | 0.5d |
| P4 | 性能/扩展 | ONNX Runtime 未真正启用 | 启用 `ENABLE_ONNX_RUNTIME` + GPU 推理 | 3–5d |
| A3 | 架构 | 单例泛滥（≥7，实际 30+ 处 `::instance()`） | `ServiceLocator`/DI 收敛 | 5–10d |
| R7 | 代码质量 | 魔法数字（阈值/epsilon） | 收敛为具名常量/配置项 | 0.5d |
| R8 | 代码质量 | 重复代码（XOR 循环、configure/deserialize 同构） | 抽取 `xorTransform()`/共用解析 | 0.5d |
| R10 | 代码质量 | 单例永不释放 | `std::atexit`/静态析构释放插件 | 0.5d |
| R11 | 可维护性 | ~63 处 "P1-修复" 注释残留 | 脚本批量迁移至 CHANGELOG | 1–2d |
| T-视觉 | 测试 | UI 视觉回归 | 扩 `visual_test_utils.hpp` 与 `auto_test/baseline/*.png` 像素对比 | — |
| T-性能 | 测试 | 性能基准测试 | 大图推理耗时/拷贝次数基准，防 R1/R2 回归 | — |

---

## 四、阶段路线图

```
Phase 0 (P0, 约 2–3 周)  ── 安全止血 + 工程化地基
  ├─ S1 AES-GCM 替换 XOR        （同时消灭硬编码种子）
  ├─ S2 登录开启 + 去 bypass
  ├─ S3 DLL 签名校验
  ├─ S4 关 QML 调试器
  ├─ P1 UI 推理异步化
  └─ M4 搭 CI（与安全修复同步，作回归屏障）

Phase 1 (P1, 约 4–6 周)  ── 质量与正确性
  ├─ S5 SQLCipher | P2/P3 推理线程化+拷贝优化 | R4 去 mock
  ├─ A1 EditViewBridge 拆分(阶段1) | A2 双DB收敛
  ├─ R9 指针所有权 | R12/R13 依赖对齐+路径防护
  └─ M1/M2 构建清理 | R5 串口真实化

Phase 2 (P2, 持续)  ── 长尾重构与增强
  ├─ A3 单例 DI | P4 ONNX+GPU | S6/S7 安全补全
  └─ R7–R11 代码质量清理 | 视觉/性能测试
```

---

## 五、质量门禁建议（最关键 5 条）

1. **零警告策略**：MSVC `/W4 /WX`，CI 阻断任何警告（保持历史 Release 零警告水准）。
2. **静态分析卡点**：PR 必过 `clang-tidy` + `cppcheck`，覆盖空指针/裸指针所有权/未校验返回值。
3. **覆盖率门禁**：OpenCppCoverage/lcov，PR 行覆盖 ≥60% 且不降。
4. **测试门禁**：CI 必须 `ctest` 全绿（锁定 300 例为硬指标）+ 新增代码须配套测试。
5. **依赖与构建一致性**：修正 `Dependencies.cmake` 与真实依赖对齐，引入 vcpkg/conan 锁版本。

---

## 六、结论

QDV 的**功能骨架与领域建模是扎实的**（算子/插件/Scheme 三套扩展点、300 例单测存量、QCache/异步序列化亮点），但**工程化与安全防护严重滞后**：安全评级 F，且多处"已完成"声明与源码不符。建议以 **P0（安全止血 + CI 门禁）** 为起点立即动手，再按 P1/P2 推进质量与架构收敛。最大杠杆点是先搭起 CI——它能把后续所有修复都变成"可回归、可守护"的持续改进。
