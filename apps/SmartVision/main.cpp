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

using namespace QDV;

namespace {

/// 解析后的命令行选项。
struct CommandLineArgs {
    QString exportPath;            ///< --export-operators <path>
    QString autoTestPng;           ///< --auto-test <png>
    QStringList addOps;            ///< --add-op <type>（可多次出现）
};

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

    // 预热 OperatorDescriptors，避免首次拖入算子时出现卡顿
    (void)UI::OperatorDescriptors::all();

    try {
        MainWindow window;

        // 首次运行：引导用户创建管理员账户
        if (AuthService::instance()->isFirstRun()) {
            window.showFirstRunSetup();
        }

        window.show();
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
