#include <QApplication>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include "MainWindow.h"
#include "AuthService.h"
#include "Logger.h"
#include "UI/OperatorDescriptors.h"
#include "UI/AutoTestRunner.h"

using namespace QDV;

int main(int argc, char *argv[]) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    QApplication app(argc, argv);

    // --- 命令行模式：--export-operators 导出算子元数据 ---
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--export-operators" && i + 1 < argc) {
            const QString exportPath = QString::fromLocal8Bit(argv[i + 1]);
            QDir().mkpath(QFileInfo(exportPath).absolutePath());
            const bool ok = UI::OperatorDescriptors::exportToJson(exportPath);
            if (ok) {
                qInfo().noquote() << "[export] wrote" << exportPath;
                return 0;
            }
            qCritical().noquote() << "[export] FAILED to write" << exportPath;
            return 2;
        }
    }

    // --- 命令行模式：--auto-test <png> [--add-op <type>...] ---
    {
        QString autoTestPng;
        QStringList addOps;
        for (int i = 1; i < argc; ++i) {
            const QString a = QString::fromLocal8Bit(argv[i]);
            if (a == "--auto-test" && i + 1 < argc)
                autoTestPng = QString::fromLocal8Bit(argv[i + 1]);
            else if (a == "--add-op" && i + 1 < argc)
                addOps << QString::fromLocal8Bit(argv[i + 1]);
        }
        const int testRc = AutoTestRunner::run(app, autoTestPng, addOps);
        if (testRc >= 0) {
            return testRc;
        }
    }

    app.setStyleSheet(R"(
        QWidget { background-color: #1e1e1e; color: #e0e0e0; }
        QMainWindow { background-color: #1e1e1e; }
        QGroupBox { border: 1px solid #444; border-radius: 4px; margin-top: 8px; padding-top: 16px; font-weight: bold; color: #e0e0e0; }
        QGroupBox::title { color: #e0e0e0; subcontrol-origin: margin; left: 12px; }
        QPushButton { background-color: #3d3d3d; color: #e0e0e0; border: 1px solid #555; padding: 6px 16px; border-radius: 4px; }
        QPushButton:hover { background-color: #555; }
        QPushButton:pressed { background-color: #660874; }
        QPushButton:disabled { background-color: #2a2a2a; color: #666; }
        QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox { background-color: #2d2d2d; color: #e0e0e0; border: 1px solid #555; border-radius: 3px; padding: 4px 8px; }
        QComboBox::drop-down { border: none; }
        QComboBox QAbstractItemView { background-color: #2d2d2d; color: #e0e0e0; selection-background-color: #660874; border: 1px solid #555; }
        QTableWidget, QTreeWidget, QListWidget { background-color: #252525; alternate-background-color: #2a2a2a; color: #e0e0e0; border: 1px solid #444; gridline-color: #444; }
        QTableWidget::item, QTreeWidget::item { color: #e0e0e0; }
        QTableWidget::item:selected, QTreeWidget::item:selected { background-color: #660874; color: #fff; }
        QHeaderView::section { background-color: #333; color: #e0e0e0; border: 1px solid #444; padding: 4px 8px; }
        QProgressBar { background-color: #2d2d2d; border: 1px solid #444; border-radius: 4px; text-align: center; color: #e0e0e0; }
        QProgressBar::chunk { background-color: #660874; border-radius: 3px; }
        QScrollBar:vertical { background: #252525; width: 12px; margin: 0; }
        QScrollBar::handle:vertical { background: #555; border-radius: 6px; min-height: 20px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar:horizontal { background: #252525; height: 12px; margin: 0; }
        QScrollBar::handle:horizontal { background: #555; border-radius: 6px; min-width: 20px; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
        QLabel { color: #e0e0e0; }
        QToolTip { background-color: #333; color: #e0e0e0; border: 1px solid #555; padding: 4px; }
        QStatusBar { background-color: #2d2d2d; color: #aaa; border-top: 1px solid #444; }
        QMenu { background-color: #3d3d3d; color: #ddd; border: 1px solid #555; padding: 4px 0; }
        QMenu::item { padding: 6px 32px 6px 16px; }
        QMenu::item:selected { background-color: #660874; color: #fff; }
        QMenu::separator { height: 1px; background-color: #555; margin: 4px 8px; }
        QDialog { background-color: #1e1e1e; }
    )");

    QDir().mkdir("logs");
    Logger::info("Q-DetectVision v1.0 starting...");

    // 预热 OperatorDescriptors
    (void)UI::OperatorDescriptors::all();

    try {
        MainWindow window;

        if (AuthService::instance()->isFirstRun()) {
            window.showFirstRunSetup();
        }

        window.show();

        app.processEvents();

        QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
            Logger::shutdown();
        });

        Logger::info("Main window displayed successfully");
        return app.exec();
    } catch (const std::exception& e) {
        QString errorMsg = QString("Application startup failed: ") + e.what();
        Logger::error(errorMsg);
        qCritical() << errorMsg;
        QMessageBox::critical(nullptr, "Startup Error", errorMsg);
        return 1;
    } catch (...) {
        QString errorMsg = "Application startup failed with unknown exception";
        Logger::error(errorMsg);
        qCritical() << errorMsg;
        QMessageBox::critical(nullptr, "Startup Error", errorMsg);
        return 1;
    }
}