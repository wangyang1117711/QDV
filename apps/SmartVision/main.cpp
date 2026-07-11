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

#include <optional>

#include "MainWindow.h"
#include "AuthService.h"
#include "Logger.h"
#include "UI/OperatorDescriptors.h"
#include "UI/AutoTestRunner.h"
#include "OperatorSDK/OperatorManifest.h"  // Phase 2: 动态算子加载
#include "Vision/ToolFactory.h"           // v5.3：注入推理引擎
#include "AI/InferenceEngine.h"           // v5.3：AI 推理引擎
#include "AI/InferenceEngineAdapter.h"    // v5.3：IInferenceEngine 适配器

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

// 编译期默认：当前版本登录功能被禁用
// 恢复登录：将此值改为 true，并按文件顶部"恢复步骤"修改 main()
constexpr bool kLoginEnabled = false;

/// 决定当前进程是否启用登录功能。
/// 优先级：环境变量 QDV_FORCE_LOGIN > 编译期常量 kLoginEnabled
///   * QDV_FORCE_LOGIN=1 / true / yes → 强制启用
///   * QDV_FORCE_LOGIN=0 / false / no  → 强制禁用
///   * 未设置                          → 使用 kLoginEnabled
inline bool isLoginActive() {
    const QByteArray env = qgetenv("QDV_FORCE_LOGIN").toLower().trimmed();
    if (env == "1" || env == "true" || env == "yes") return true;
    if (env == "0" || env == "false" || env == "no") return false;
    return kLoginEnabled;
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
        Logger::info("Q-DetectVision v1.0 starting...");
    }
    ~LoggerGuard() { safeLoggerShutdown(); }
    LoggerGuard(const LoggerGuard&)            = delete;
    LoggerGuard& operator=(const LoggerGuard&) = delete;
};

} // namespace

int main(int argc, char *argv[]) {
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
        QString pluginDir = QCoreApplication::applicationDirPath() + "/operators";
        QDir().mkpath(pluginDir);
        QDir dir(pluginDir);
        QStringList filters; filters << "*.dll";
        for (const QFileInfo& fi : dir.entryInfoList(filters)) {
            QString err;
            QDV::OperatorManifest manifest;
            // 先用 loadPlugin 获取 manifest 元数据（type/cnName/category/params 等）
            if (!QDV::loadPlugin(fi.absoluteFilePath(), manifest, &err)) {
                Logger::warn(QString("Failed to load plugin manifest: %1 (%2)")
                                 .arg(fi.absoluteFilePath(), err));
                continue;
            }
            // 再用 loadAndRegister 注册算子创建器到 IOperatorRegistry（供 ToolFactory 使用）
            if (QDV::loadAndRegister(fi.absoluteFilePath(), &err)) {
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
                                 .arg(fi.absoluteFilePath(), err));
            }
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
