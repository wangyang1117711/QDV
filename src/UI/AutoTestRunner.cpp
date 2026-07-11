#include "UI/AutoTestRunner.h"
#include "UI/MainWindow.h"
#include "Core/Logger.h"
#include "UI/OperatorDescriptors.h"
#include "UI/EditViewBridge.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QThread>
#include <QStackedWidget>
#include <QScreen>

using namespace QDV;

int AutoTestRunner::run(QApplication& app,
                         const QString& autoTestPng,
                         const QStringList& addOps) {
    if (autoTestPng.isEmpty()) {
        return -1;  // 未触发测试
    }

    // 路径含 :edit: 时为双视图模式，截经典+QML 两张图
    const bool editMode = autoTestPng.contains(QStringLiteral(":edit:"));
    QDir().mkdir("logs");
    Logger::info("Q-DetectVision v1.0 starting (--auto-test)...");

    // 预热 OperatorDescriptors（同正常路径）
    (void)UI::OperatorDescriptors::all();

    try {
        MainWindow w;
        w.show();
        app.processEvents();
        Logger::info("[auto-test] login as qc...");

        // 跳过登录界面，直接走 showMain()
        QTimer::singleShot(1500, &w, &MainWindow::showMain);

        // 等待 CentralWindow 出现 + 切换到编辑 tab
        QTimer::singleShot(4500, [&w, autoTestPng, editMode, addOps]() {
            w.activateWindow();
            w.raise();

            // --- 辅助 lambda：查找 EditViewBridge ---
            auto findBridge = []() -> ::EditViewBridge* {
                for (QWidget* tl : QApplication::topLevelWidgets()) {
                    if (auto* b = tl->findChild<::EditViewBridge*>()) {
                        return b;
                    }
                }
                return nullptr;
            };

            // --- 辅助 lambda：查找 count=8 的中央 stack ---
            auto findCentralStack = []() -> QStackedWidget* {
                for (QWidget* tl : QApplication::topLevelWidgets()) {
                    for (QStackedWidget* s : tl->findChildren<QStackedWidget*>()) {
                        if (s->count() == 8) return s;
                    }
                }
                return nullptr;
            };

            // --- 辅助 lambda：查找 EditView 内的 canvasStack (count=2) ---
            auto findCanvasStack = [](::EditViewBridge* bridge) -> QStackedWidget* {
                if (!bridge) return nullptr;
                QWidget* ev = qobject_cast<QWidget*>(bridge->parent());
                if (!ev) return nullptr;
                for (QStackedWidget* s : ev->findChildren<QStackedWidget*>()) {
                    if (s->count() == 2) return s;
                }
                return nullptr;
            };

            // --- 辅助 lambda：添加算子并选中最后一个 ---
            auto runAddOps = [&addOps](::EditViewBridge* bridge) -> QString {
                if (!bridge || addOps.isEmpty()) return {};
                QString lastId;
                int yPos = 80;
                for (const QString& opType : addOps) {
                    QApplication::processEvents();
                    const QString nid = bridge->addOperator(opType, 200, yPos);
                    if (!nid.isEmpty()) {
                        Logger::info(QString("[auto-test] added op %1 -> %2").arg(opType, nid));
                        lastId = nid;
                    } else {
                        Logger::warn(QString("[auto-test] addOperator(%1) returned empty id").arg(opType));
                    }
                    yPos += 120;
                    QApplication::processEvents();
                    QThread::msleep(200);
                }
                if (!lastId.isEmpty()) {
                    bridge->selectNode(lastId);
                    Logger::info(QString("[auto-test] selected last node: %1").arg(lastId));
                }
                QApplication::processEvents();
                QThread::msleep(1500);
                QApplication::processEvents();
                return lastId;
            };

            // --- 截图辅助 lambda ---
            // 改为直接抓取指定 QWidget，避免被 TRAE 等顶层窗口遮挡
            auto grabAndSave = [](const QString& path, QWidget* target) {
                QPixmap pix;
                if (target) {
                    target->activateWindow();
                    target->raise();
                    target->show();
                    QApplication::processEvents();
                    QThread::msleep(200);
                    pix = target->grab();
                } else {
                    pix = QApplication::primaryScreen()->grabWindow(0);
                }
                QDir().mkpath(QFileInfo(path).absolutePath());
                const bool ok = pix.save(path, "PNG");
                Logger::info(QString("[auto-test] screenshot %1: %2 (%3x%4)")
                                 .arg(ok ? "OK" : "FAILED").arg(path)
                                 .arg(pix.width()).arg(pix.height()));
                return ok;
            };

            if (editMode) {
                // === 双视图模式：经典画布 + QML 画布 ===
                const int editIdx = autoTestPng.indexOf(QStringLiteral(":edit:"));
                // basePath + baseName: "path/:edit:file.png" → path/file
                const QString basePath = (editIdx >= 0) ? autoTestPng.left(editIdx)
                                                         : QFileInfo(autoTestPng).absolutePath();
                const QString baseName = (editIdx >= 0) ? autoTestPng.mid(editIdx + 6)
                                                         : QFileInfo(autoTestPng).fileName();
                const int dotIdx = baseName.lastIndexOf(QLatin1Char('.'));
                const QString stem = (dotIdx > 0) ? baseName.left(dotIdx) : baseName;
                const QString ext  = (dotIdx > 0) ? baseName.mid(dotIdx) : QStringLiteral(".png");
                const QString classicPath = basePath + "/" + stem + "_classic" + ext;
                const QString qmlPath     = basePath + "/" + stem + "_qml"     + ext;

                ::EditViewBridge* bridge = findBridge();
                QStackedWidget* centralStack = findCentralStack();
                QStackedWidget* canvasStack  = findCanvasStack(bridge);
                QWidget* centralWindow = centralStack ? centralStack->window() : nullptr;

                if (!canvasStack && bridge) {
                    Logger::warn("[auto-test] canvasStack (count=2) not found, will fallback");
                }

                if (centralStack) {
                    Logger::info(QString("[auto-test] switching central stack to EditView index 3 (was %1)")
                                     .arg(centralStack->currentIndex()));
                    centralStack->setCurrentIndex(3);
                } else {
                    Logger::warn("[auto-test] no central stack (count=8) found");
                }
                QApplication::processEvents();
                QThread::msleep(800);
                QApplication::processEvents();

                // 经典画布截图
                if (canvasStack) {
                    canvasStack->setCurrentIndex(0);
                    QApplication::processEvents();
                    QThread::msleep(400);
                }
                grabAndSave(classicPath, centralWindow);

                // 切到 QML 画布
                if (canvasStack) {
                    canvasStack->setCurrentIndex(1);
                }
                QApplication::processEvents();
                QThread::msleep(1200);
                QApplication::processEvents();

                // 可选：添加算子（让属性面板渲染参数）
                runAddOps(bridge);

                // QML 画布截图
                grabAndSave(qmlPath, centralWindow);
            } else {
                // === 单截图模式 ===
                QStackedWidget* centralStack = findCentralStack();
                QWidget* centralWindow = centralStack ? centralStack->window() : nullptr;
                if (centralStack) {
                    Logger::info(QString("[auto-test] switching to EditView index 3 (was %1)")
                                     .arg(centralStack->currentIndex()));
                    centralStack->setCurrentIndex(3);
                }
                QApplication::processEvents();
                QThread::msleep(800);
                QApplication::processEvents();

                runAddOps(findBridge());
                grabAndSave(autoTestPng, centralWindow);
            }

            QApplication::quit();
        });

        return app.exec();
    } catch (const std::exception& e) {
        Logger::error(QString("[auto-test] failed: %1").arg(e.what()));
        return 3;
    }
}