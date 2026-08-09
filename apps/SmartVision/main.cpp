// =====================================================================
// QDetectVision 应用入口
//
// =====================================================================
// M8 临时特性隔离：密码登录功能模块化隔离
// ---------------------------------------------------------------------
// 背景：当前版本需要暂时关闭密码登录验证，让用户绕过 LoginView 直接
//       进入主功能界面。原始 LoginView / AuthService 全部代码保留，
//       仅通过"启动门控"集中拦截主程序对登录模块的调用。
//
// 隔离范围（v1）：
//   * main.cpp 的常规启动分支（isFirstRun / showFirstRunSetup）
//   * main.cpp 的登录界面激活（MainWindow::showLogin 由其构造隐式调用；
//     LoginIsolation::enter() 内部会触发 showMain()，整体 hide MainWindow，
//     LoginView 永远不会被用户看到）
//
// 不在隔离范围（v1）：
//   * AutoTestRunner 路径：本来就走 QTimer 模拟登录成功，
//     始终不显示 LoginView，无需额外处理
//   * 单元测试（test_ui_smoke.cpp 等）：直接调用 LoginView API，
//     用于保证 API 完整性以便未来恢复登录功能
//   * LoginView / AuthService 源码：完整保留，恢复登录无需重写
//
// 恢复步骤（未来需要重新启用登录时）：
//   1. 将 LoginIsolation::kLoginEnabled 改为 true
//   2. 删除 main() 中对 LoginIsolation::enter() 的调用，
//      改为原来的 "if (isFirstRun) showFirstRunSetup; window.show()" 即可
//   3. 重新构建即可恢复完整登录流程
// =====================================================================

#include <QApplication>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <QDateTime>

#include <optional>

#include "MainWindow.h"
#include "AuthService.h"
#include "Logger.h"
#include "Core/PathValidator.h"  // S6 修复：路径校验
#include "UI/OperatorDescriptors.h"
#include "UI/AutoTestRunner.h"
#include "OperatorSDK/OperatorManifest.h"  // Phase 2: 动态算子加载
#include "Vision/ToolFactory.h"           // v5.3：注入推理引擎
#include "AI/InferenceEngine.h"           // v5.3：AI 推理引擎
#include "AI/InferenceEngineAdapter.h"    // v5.3：IInferenceEngine 适配器
#include "OperatorLibrary/OperatorLibraryController.h"  // v2.7.0 O1a：算子库控制器

// Windows 崩溃处理器：使用 Vectored Exception Handler 捕获所有线程的异常
// 改进版：正确设置符号路径 + 生成 Minidump + 模块信息记录
#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#include <shlwapi.h>
#include <psapi.h>

// 生成 Minidump 文件，供后续用 WinDbg/VS 分析完整调用栈
static void writeMinidump(EXCEPTION_POINTERS* ep) {
    // 确保目录存在
    CreateDirectoryA("logs", nullptr);

    char dumpPath[MAX_PATH];
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(dumpPath, MAX_PATH, "logs/crash_%04d%02d%02d_%02d%02d%02d.dmp",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = CreateFileA(dumpPath, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;

    // MiniDumpWithFullMemory 包含完整堆内存，文件较大但信息最全
    // MiniDumpNormal + MiniDumpWithThreadInfo + MiniDumpWithModuleHeaders 兼顾大小和信息量
    DWORD flags = MiniDumpNormal
                | MiniDumpWithThreadInfo
                | MiniDumpWithModuleHeaders
                | MiniDumpWithIndirectlyReferencedMemory;

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                      static_cast<MINIDUMP_TYPE>(flags), &mei, nullptr, nullptr);
    CloseHandle(hFile);

    fprintf(stderr, "Minidump written to: %s\n", dumpPath);
    fflush(stderr);
}

LONG WINAPI crashHandler(EXCEPTION_POINTERS* ep) {
    // 只处理真正的崩溃异常，不处理正常的信号
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW ||
        code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED ||
        code == EXCEPTION_DATATYPE_MISALIGNMENT || code == EXCEPTION_IN_PAGE_ERROR ||
        code == EXCEPTION_INT_DIVIDE_BY_ZERO) {

        const char* reason = "Unknown";
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:         reason = "ACCESS_VIOLATION (0xC0000005)"; break;
            case EXCEPTION_STACK_OVERFLOW:           reason = "STACK_OVERFLOW (0xC00000FD)"; break;
            case EXCEPTION_ILLEGAL_INSTRUCTION:      reason = "ILLEGAL_INSTRUCTION (0xC000001D)"; break;
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    reason = "ARRAY_BOUNDS_EXCEEDED (0xC000008C)"; break;
            case EXCEPTION_DATATYPE_MISALIGNMENT:    reason = "DATATYPE_MISALIGNMENT (0x80000002)"; break;
            case EXCEPTION_IN_PAGE_ERROR:            reason = "IN_PAGE_ERROR (0xC0000006)"; break;
            case EXCEPTION_INT_DIVIDE_BY_ZERO:       reason = "DIVIDE_BY_ZERO (0xC0000094)"; break;
            default: break;
        }

        // 获取当前线程 ID
        DWORD tid = GetCurrentThreadId();

        QString crashLog = QString("===== CRASH DETECTED =====\n"
                                   "Time: %1\n"
                                   "Thread ID: 0x%2\n"
                                   "Exception Code: 0x%3 (%4)\n"
                                   "Exception Address: 0x%5\n"
                                   "===========================")
            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"))
            .arg(tid, 8, 16, QChar('0'))
            .arg(code, 8, 16, QChar('0'))
            .arg(reason)
            .arg(reinterpret_cast<quintptr>(ep->ExceptionRecord->ExceptionAddress), 0, 16);

        // 生成 Minidump（供 WinDbg 离线分析）
        writeMinidump(ep);

        // 捕获调用堆栈 - 改进符号解析
        // 关键修复：不再使用 SYMOPT_DEFERRED_LOADS，改为立即加载符号
        // 并设置正确的符号搜索路径（exe 目录 + build 目录）
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEBUG);
        SymInitialize(GetCurrentProcess(), nullptr, FALSE);

        // 设置符号搜索路径：exe 目录 + 当前工作目录 + build 目录
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        PathRemoveFileSpecW(exePath);
        QString symPath = QString::fromWCharArray(exePath) + ";.;..\\build";
        SymSetSearchPathW(GetCurrentProcess(), symPath.toStdWString().c_str());

        // 枚举已加载模块，强制加载符号
        // 这解决了 SYMOPT_DEFERRED_LOADS 导致崩溃时符号尚未加载的问题
        {
            HANDLE hProcess = GetCurrentProcess();
            HMODULE hMods[1024];
            DWORD cbNeeded = 0;
            if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
                DWORD count = cbNeeded / sizeof(HMODULE);
                for (DWORD i = 0; i < count; ++i) {
                    wchar_t modPath[MAX_PATH];
                    if (GetModuleFileNameW(hMods[i], modPath, MAX_PATH)) {
                        SymLoadModuleExW(hProcess, hMods[i], nullptr, modPath,
                                         0, 0, nullptr, 0);
                    }
                }
            }
        }

        void* stack[62];
        USHORT frames = CaptureStackBackTrace(0, 62, stack, nullptr);

        QString stackTrace = "\nCall Stack:";
        for (USHORT i = 0; i < frames; ++i) {
            DWORD64 address = reinterpret_cast<DWORD64>(stack[i]);

            // 获取模块名
            HMODULE hMod = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(address), &hMod);
            wchar_t modName[MAX_PATH] = {0};
            if (hMod) {
                GetModuleBaseNameW(GetCurrentProcess(), hMod, modName, MAX_PATH);
            }

            DWORD64 symDisplacement = 0;
            char symbolBuffer[sizeof(SYMBOL_INFO) + 512];
            PSYMBOL_INFO symbol = reinterpret_cast<PSYMBOL_INFO>(symbolBuffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 512;

            QString frameInfo;
            if (SymFromAddr(GetCurrentProcess(), address, &symDisplacement, symbol)) {
                IMAGEHLP_LINE64 line;
                line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
                DWORD lineDisplacement = 0;
                if (SymGetLineFromAddr64(GetCurrentProcess(), address, &lineDisplacement, &line)) {
                    frameInfo = QString("\n  #%1 [%2] %3+0x%4 @ %5:%6 (addr=0x%7)")
                        .arg(i)
                        .arg(QString::fromWCharArray(modName))
                        .arg(QString::fromLocal8Bit(symbol->Name))
                        .arg(symDisplacement, 0, 16)
                        .arg(QString::fromLocal8Bit(line.FileName))
                        .arg(line.LineNumber)
                        .arg(address, 0, 16);
                } else {
                    frameInfo = QString("\n  #%1 [%2] %3+0x%4 (addr=0x%5)")
                        .arg(i)
                        .arg(QString::fromWCharArray(modName))
                        .arg(QString::fromLocal8Bit(symbol->Name))
                        .arg(symDisplacement, 0, 16)
                        .arg(address, 0, 16);
                }
            } else {
                frameInfo = QString("\n  #%1 [%2] <unknown> (addr=0x%3)")
                    .arg(i)
                    .arg(QString::fromWCharArray(modName))
                    .arg(address, 0, 16);
            }
            stackTrace += frameInfo;
        }

        // 写入文件
        QFile crashFile("logs/crash_dump.log");
        if (crashFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QTextStream stream(&crashFile);
            stream << crashLog << stackTrace << "\n\n";
            crashFile.close();
        }

        // 输出到 stderr
        fprintf(stderr, "%s\n%s\n", crashLog.toLocal8Bit().constData(), stackTrace.toLocal8Bit().constData());
        fflush(stderr);

        SymCleanup(GetCurrentProcess());
    }

    return EXCEPTION_CONTINUE_SEARCH;
}
#endif // Q_OS_WIN

using namespace QDV;

namespace {

/// 解析后的命令行选项。
struct CommandLineArgs {
    QString exportPath;            ///< --export-operators <path>
    QString autoTestPng;           ///< --auto-test <png>
    QStringList addOps;            ///< --add-op <type>（可多次出现）
};

// =====================================================================
// 登录功能隔离模块（LoginIsolation）
// ---------------------------------------------------------------------
// 设计原则：
//   * 单一职责：仅负责决定"是否激活登录功能"以及"如何进入主窗口"
//   * 多源控制：编译期常量（kLoginEnabled）+ 运行时环境变量（QDV_FORCE_LOGIN）
//   * 可观测：调用 enter() 时打印一行 banner 状态，运维一眼可见
//   * 集中化：所有登录相关 bypass 逻辑仅在本命名空间内出现，
//            main() 不再直接接触 isFirstRun / showFirstRunSetup
// =====================================================================
namespace LoginIsolation {

// S2 修复：登录功能默认启用（原值 false 导致整个登录模块被绕过）
// 安全要求：生产构建必须强制启用登录，不允许通过环境变量绕过
constexpr bool kLoginEnabled = true;

/// 决定当前进程是否启用登录功能。
/// 优先级：环境变量 QDV_FORCE_LOGIN > 编译期常量 kLoginEnabled
///
/// S2 修复：环境变量 bypass 逻辑仅在调试构建（QT_DEBUG）下生效，
///         生产构建（Release）始终强制启用登录，忽略任何环境变量。
///   * 调试构建：
///     - QDV_FORCE_LOGIN=1 / true / yes → 强制启用
///     - QDV_FORCE_LOGIN=0 / false / no  → 强制禁用（仅供本地调试）
///     - 未设置                          → 使用 kLoginEnabled（默认 true）
///   * 生产构建：始终返回 true，忽略环境变量
inline bool isLoginActive() {
#ifdef QT_DEBUG
    // 仅调试构建允许环境变量覆盖，便于自动化测试与本地开发
    const QByteArray env = qgetenv("QDV_FORCE_LOGIN").toLower().trimmed();
    if (env == "1" || env == "true" || env == "yes") return true;
    if (env == "0" || env == "false" || env == "no") return false;
    return kLoginEnabled;
#else
    // S2 修复：生产构建强制启用登录，环境变量 bypass 失效
    Q_UNUSED(kLoginEnabled)
    return true;
#endif
}

/// 启动 banner：打印登录功能当前状态到日志。
/// 目的：运维/测试人员启动时即可肉眼判断走的是哪条路径。
inline void logStatusBanner() {
    if (isLoginActive()) {
        Logger::info("[LoginIsolation] LOGIN ENABLED — LoginView will be shown");
    } else {
        Logger::warn(
            "[LoginIsolation] LOGIN DISABLED — bypassing LoginView, entering main window directly. "
            "To re-enable: set LoginIsolation::kLoginEnabled = true or QDV_FORCE_LOGIN=1");
    }
}

/// 统一入口：决定 MainWindow 的初始化流程。
/// 启用登录：调用 showFirstRunSetup()（若首次运行）+ 正常 show() LoginView
/// 禁用登录：跳过所有登录相关调用，直接 show() + showMain() 进入主窗口
///
/// 注意：本函数是 main() 中唯一与登录逻辑交互的入口点。
inline void enter(MainWindow& window) {
    logStatusBanner();

    if (isLoginActive()) {
        // === 启用登录的标准流程 ===
        if (AuthService::instance()->isFirstRun()) {
            window.showFirstRunSetup();
        }
        window.show();  // MainWindow 默认显示 LoginView
    } else {
        // === 隔离登录的 bypass 流程 ===
        // 1) window.show()：构造 MainWindow 必须调用 show() 以触发事件循环就绪
        window.show();
        // 2) window.showMain()：内部 hide LoginView + 创建并显示 CentralWindow
        //    用户实际看到的是 CentralWindow 主功能界面
        window.showMain();
    }
}

} // namespace LoginIsolation

/// 一次性遍历 argv 提取所有支持的选项，避免重复扫描。
/// 注意：argv 解码只发生一次，减少 QString::fromLocal8Bit 的重复调用。
CommandLineArgs parseCommandLine(int argc, char *argv[]) {
    CommandLineArgs out;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--export-operators" && i + 1 < argc) {
            out.exportPath = QString::fromLocal8Bit(argv[++i]);
        } else if (a == "--auto-test" && i + 1 < argc) {
            out.autoTestPng = QString::fromLocal8Bit(argv[++i]);
        } else if (a == "--add-op" && i + 1 < argc) {
            out.addOps << QString::fromLocal8Bit(argv[++i]);
        }
    }
    return out;
}

/// 处理 --export-operators：成功返回 true，失败返回 false。
/// 返回 std::optional<int>：含值表示已处理，调用方直接 return 该值。
std::optional<int> handleExportOperators(const QString& exportPath) {
    if (exportPath.isEmpty()) {
        return std::nullopt;
    }
    QDir().mkpath(QFileInfo(exportPath).absolutePath());
    if (UI::OperatorDescriptors::exportToJson(exportPath)) {
        qInfo().noquote() << "[export] wrote" << exportPath;
        return 0;
    }
    qCritical().noquote() << "[export] FAILED to write" << exportPath;
    return 2;
}

/// 从 Qt 资源中加载并应用暗色主题样式表。
/// 加载失败时打印警告但不中断启动（QSS 缺失不应阻止应用运行）。
void applyDarkTheme() {
    QFile f(":/styles/dark_theme.qss");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[theme] failed to open :/styles/dark_theme.qss, fallback to default style";
        return;
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    qApp->setStyleSheet(in.readAll());
}

/// 在所有退出路径上安全地关闭日志系统。
/// 原因：异常分支不会触发 QCoreApplication::aboutToQuit，必须显式调用。
void safeLoggerShutdown() noexcept {
    try {
        Logger::shutdown();
    } catch (...) {
        // 静默吞掉：清理阶段不应再抛异常
    }
}

/// RAII 守卫：构造时记录启动日志，析构时确保 Logger 被关闭。
/// 这样可以保证即便 try 块抛出异常，Logger 也会被正确清理。
struct LoggerGuard {
    LoggerGuard() {
        QDir().mkdir("logs");
        Logger::info("Q-DetectVision v2.0-0809 starting...");
    }
    ~LoggerGuard() { safeLoggerShutdown(); }
    LoggerGuard(const LoggerGuard&)            = delete;
    LoggerGuard& operator=(const LoggerGuard&) = delete;
};

} // namespace

int main(int argc, char *argv[]) {
    // 注册 Windows VEH 崩溃处理器：捕获所有线程的段错误等异常，写入 logs/crash_dump.log
    // 使用 Vectored Exception Handler 而非 SetUnhandledExceptionFilter，
    // 因为后者无法捕获非主线程的异常（Qt6Widgets.dll 的崩溃可能在工作线程）
#ifdef Q_OS_WIN
    AddVectoredExceptionHandler(1 /*第一个被调用*/, crashHandler);

    // 修复终端中文乱码：Windows 控制台默认 GBK（代码页 936），
    // 而 ZeroShotKit 等模块用 std::printf 以 UTF-8 输出中文日志，
    // 不切换代码页会显示为乱码。这里统一将控制台输入/输出代码页切到 UTF-8。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // 让 QML 控件走基础样式（避免 Universal/Fusion 主题对自定义颜色造成覆盖）
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    // v5.3 彻底修复编辑模块 QRhi 跨实例错误 + Device loss：
    // 根因：QQuickWidget 内部使用 QQuickRenderControl 渲染到纹理，在 QStackedWidget
    // 视图切换或 GPU 空闲时，RHI 上下文会被重建，而 QSG 纹理缓存中仍引用旧 QRhi 的
    // 纹理，导致 "Texture belongs to QRhi A but client code attempted to use it with QRhi B"。
    // 修复组合：
    // 1. QSG_RHI_BACKEND=d3d11：固定 RHI 后端（Windows 最稳定）
    // 2. QSG_RENDER_LOOP=basic：单线程渲染循环，减少多线程竞争
    // 3. QSG_NO_TEXTURE_CACHE=1：禁用 QSG 纹理缓存，避免旧 QRhi 纹理被复用
    // 4. QSG_NO_DEPTH_BUFFER=1：禁用深度缓冲，降低 RHI 重建时的资源冲突
    qputenv("QSG_RHI_BACKEND", "d3d11");
    qputenv("QSG_RENDER_LOOP", "basic");
    qputenv("QSG_NO_TEXTURE_CACHE", "1");
    qputenv("QSG_NO_DEPTH_BUFFER", "1");

    QApplication app(argc, argv);
    app.setApplicationName("QDetectVision");
    app.setOrganizationName("QDV");
    app.setApplicationVersion("V2.0-0809");

    // 解析命令行（一次性遍历，避免重复扫描 argv）
    const CommandLineArgs args = parseCommandLine(argc, argv);

    // 分支 1：--export-operators 导出算子元数据
    if (auto rc = handleExportOperators(args.exportPath); rc.has_value()) {
        return *rc;
    }

    // 分支 2：--auto-test 端到端自动化测试
    if (!args.autoTestPng.isEmpty()) {
        return AutoTestRunner::run(app, args.autoTestPng, args.addOps);
    }

    // === 正常启动流程 ===
    applyDarkTheme();
    LoggerGuard logGuard;  // 异常安全：保证 Logger::shutdown 一定被调用

    // v5.3：创建 AI 推理引擎并注入到 ToolFactory
    // AiClassifyTool 通过依赖注入获取 IInferenceEngine，若不注入则 execute() 必然失败
    static ::InferenceEngine* g_inferenceEngine = new ::InferenceEngine();
    static QDV::InferenceEngineAdapter* g_engineAdapter = new QDV::InferenceEngineAdapter(g_inferenceEngine);
    ToolFactory::instance()->setInferenceEngine(g_engineAdapter);

    // 预热 OperatorDescriptors，避免首次拖入算子时出现卡顿
    (void)UI::OperatorDescriptors::all();

    // Phase 2: 加载动态算子扩展（bin/operators/*.dll）
    // 扫描 exe 同级 operators/ 目录下的所有 .dll，通过 OperatorPluginLoader 加载并注册
    {
        // S6 修复：清理插件目录路径，防止路径穿越
        QString pluginDir = QDV::PathValidator::sanitize(
            QCoreApplication::applicationDirPath() + "/operators");
        if (pluginDir.isEmpty()) {
            Logger::error("Plugin directory path invalid after sanitize, skipping plugin load");
        } else {
            QDir().mkpath(pluginDir);
            QDir dir(pluginDir);
            QStringList filters; filters << "*.dll";
            // S6 修复：白名单根目录 = 清理后的 pluginDir，确保所有加载的 DLL 都在该目录内
            const QStringList allowedRoots = { pluginDir };
            for (const QFileInfo& fi : dir.entryInfoList(filters)) {
                // S6 修复：校验每个 DLL 路径都在 operators/ 目录内（防穿越）
                const QString dllPath = QDV::PathValidator::sanitize(fi.absoluteFilePath());
                if (dllPath.isEmpty() ||
                    !QDV::PathValidator::isWithinAllowedDir(dllPath, allowedRoots)) {
                    Logger::warn(QString("Plugin path rejected by PathValidator (traversal detected): %1")
                                     .arg(fi.absoluteFilePath()));
                    continue;
                }
                QString err;
                QDV::OperatorManifest manifest;
                // 先用 loadPlugin 获取 manifest 元数据（type/cnName/category/params 等）
                if (!QDV::loadPlugin(dllPath, manifest, &err)) {
                    Logger::warn(QString("Failed to load plugin manifest: %1 (%2)")
                                     .arg(dllPath, err));
                    continue;
                }
                // 再用 loadAndRegister 注册算子创建器到 IOperatorRegistry（供 ToolFactory 使用）
                if (QDV::loadAndRegister(dllPath, &err)) {
                    // 同时注册到 OperatorDescriptors 供 UI 显示
                    QDV::UI::OperatorMeta om;
                    om.type = manifest.type;
                    om.cnName = manifest.cnName;
                    om.category = manifest.category;
                    om.iconPath = manifest.iconPath;
                    om.description = manifest.description;
                    om.params = manifest.params;
                    QDV::UI::OperatorDescriptors::registerExternalOperator(om);
                    Logger::info(QString("Loaded plugin: %1").arg(manifest.type));
                } else {
                    Logger::warn(QString("Failed to register plugin: %1 (%2)")
                                     .arg(dllPath, err));
                }
            }
        }
    }

    // v2.7.0 O1a：初始化算子库控制器（扫描 config/operators_imported/*.qdvop）
    // 必须在 EditViewBridge 创建之前完成（Bridge 构造时 connect Controller 信号）
    {
        // S6 修复：清理算子库目录路径，防止路径穿越
        const QString operatorsPath = QDV::PathValidator::sanitize(
            QCoreApplication::applicationDirPath() + "/config/operators_imported");
        const QString versionsPath = QDV::PathValidator::sanitize(
            QCoreApplication::applicationDirPath() + "/config/operators_versions");
        if (operatorsPath.isEmpty() || versionsPath.isEmpty()) {
            Logger::error("Operator library path invalid after sanitize, skipping init");
        } else {
            QDV::OperatorLibrary::OperatorLibraryController::instance()->init(operatorsPath, versionsPath);
        }
    }

    try {
        MainWindow window;

        // 登录功能隔离：单一入口决定是否激活登录流程
        // 详见 LoginIsolation 命名空间顶部注释
        LoginIsolation::enter(window);

        Logger::info("Main window displayed successfully");

        return app.exec();
    } catch (const std::exception& e) {
        const QString errorMsg = QString("Application startup failed: ") + e.what();
        Logger::error(errorMsg);
        qCritical().noquote() << errorMsg;
        QMessageBox::critical(nullptr, "Startup Error", errorMsg);
        return 1;
    } catch (...) {
        const QString errorMsg = "Application startup failed with unknown exception";
        Logger::error(errorMsg);
        qCritical().noquote() << errorMsg;
        QMessageBox::critical(nullptr, "Startup Error", errorMsg);
        return 1;
    }
    // LoggerGuard 析构 → safeLoggerShutdown() 自动执行
}
