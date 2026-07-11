# Debug Session: qdv-startup-health-check

**Status**: [OPEN]
**Created**: 2026-05-27
**Target**: QDetectVision.exe v1.0 (build e:\anchor\Trae\QDV\build\bin)
**Context**: 上一轮完成了 6 项安全/质量修复 + UI 深色主题，184 测试通过但 shutdown 时出现 access violation (0xC0000005)

## Symptoms
- 测试套件 184/184 全部通过
- 测试进程退出码 0xC0000005 (ACCESS_VIOLATION) — 发生在所有测试完成后
- 自上次运行时监控以来已完成多项修复 (S1, S2, S3, S5, P1, Q2, UI 深色主题)
- 主程序在 isFirstRun=true 时会弹出 FirstRunSetup 模态对话框阻塞

## Hypotheses (Evidence Gate — 未取得运行时证据前不可修改业务逻辑)

### H1: Logger 初始化与日志写入正常
**前提**: 上次修复了 Logger 死锁 (P0-Critical)
**观测点**: `logs/<date>.log` 文件是否存在且包含启动日志
**预期**: 日志文件包含 "Q-DetectVision v1.0 starting..." 及后续日志

### H2: FirstRunSetup 对话框正常显示并可用
**前提**: 上次确认对话框阻塞是预期行为
**观测点**: 对话框是否出现在屏幕上、是否可交互
**预期**: 对话框正常显示，字段验证正常

### H3: CentralWindow 7 子视图延迟初始化正常
**前提**: 上次架构评估 D4 缺陷 — CentralWindow 延迟初始化
**观测点**: 登录成功后 7 个子视图是否正确创建
**预期**: 无 SIGSEGV、无 QObject 错误

### H4: 测试进程 Shutdown 时 ACCESS_VIOLATION
**前提**: 184 测试通过后退出码 0xC0000005
**观测点**: 静态析构顺序、Logger shutdown 时序
**预期**: 定位 crash 根因

### H5: 新增的 Token 加密/解密周期正常
**前提**: S3 修复 — Token 加密存储
**观测点**: 首次创建用户 → 记住我 → 重启自动登录的完整周期
**预期**: Token 正确加密后存入 QSettings，解密还原后验证通过

## Observation Plan
| H | 观测方法 | 插桩位置 |
|---|---------|--------|
| H1 | 检查日志文件 | main.cpp::Logger::info() |
| H2 | 窗口句柄 + 截图 | main.cpp::FirstRunSetup |
| H3 | 构造函数日志 | CentralWindow::setViews() |
| H4 | QApplication::aboutToQuit + 信号 | main.cpp::QApplication |
| H5 | Token 加密前后值 | LoginView::onLoginClicked |

## Resolution
_(待填写)_