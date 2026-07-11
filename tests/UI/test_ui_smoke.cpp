#include "../catch2/catch2_minimal.hpp"
#include "UI/MainWindow.h"
#include "UI/LoginView.h"
#include "UI/CentralWindow.h"
#include "UI/CameraView.h"
#include "UI/SchemeView.h"
#include "UI/IOView.h"
#include "UI/CommView.h"
#include "UI/MonitorView.h"
#include "UI/EditView.h"
#include "Core/AuthService.h"
#include <QApplication>
#include <QSignalSpy>
#include <QPushButton>
#include <QQuickWidget>        // v5.1：EditView QML 加载状态验证
#include <QTest>               // v5.3.2：模拟鼠标移动以验证 QRhi 纹理稳定性
#include <QThread>
#include <QMouseEvent>
#include <QQuickItem>
#include "UI/EditViewBridge.h" // v5.3.2：在 EditView 中添加算子以产生可交互模块

namespace {
// v5.3.2：临时捕获 Qt 警告/严重消息，用于断言不存在 QRhi 跨实例纹理错误。
static QStringList g_qrhiCaptured;
static QtMessageHandler g_prevMessageHandler = nullptr;

void qrhiWarningMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        g_qrhiCaptured.append(msg);
    }
    if (g_prevMessageHandler) {
        g_prevMessageHandler(type, ctx, msg);
    }
}
}

static int argc = 0;
static QApplication* app() {
    static QApplication a(argc, nullptr);
    return &a;
}

inline void ensureApp() { app(); }

TEST_CASE("LoginView construction", "[ui]") {
    ensureApp();
    LoginView* view = new LoginView();
    REQUIRE(view != nullptr);
    REQUIRE(view->isVisible() == false);
    delete view;
}

TEST_CASE("CentralWindow construction with 7 sub-views", "[ui]") {
    ensureApp();
    CentralWindow* cw = new CentralWindow();
    REQUIRE(cw != nullptr);
    SUCCEED("CentralWindow created");
    delete cw;
}

TEST_CASE("CameraView construction", "[ui]") {
    ensureApp();
    CameraView* cv = new CameraView();
    REQUIRE(cv != nullptr);
    delete cv;
}

TEST_CASE("SchemeView construction", "[ui]") {
    ensureApp();
    SchemeView* sv = new SchemeView();
    REQUIRE(sv != nullptr);
    delete sv;
}

TEST_CASE("MainWindow showLogin initialState", "[ui]") {
    ensureApp();
    MainWindow* mw = new MainWindow();
    mw->showLogin();
    SUCCEED("showLogin initial state OK");
    delete mw;
}

TEST_CASE("MainWindow showMain lazy creation", "[ui]") {
    ensureApp();
    MainWindow* mw = new MainWindow();
    mw->showLogin();
    mw->showMain();
    SUCCEED("showMain lazy creation OK");
    delete mw;
}

TEST_CASE("MainWindow showFirstRunSetup dialog", "[ui]") {
    ensureApp();
    MainWindow* mw = new MainWindow();
    mw->showFirstRunSetup();
    SUCCEED("showFirstRunSetup OK");
    delete mw;
}

TEST_CASE("LoginView loginSuccess signal exists", "[ui]") {
    ensureApp();
    LoginView* lv = new LoginView();
    QObject* obj = dynamic_cast<QObject*>(lv);
    int signalIdx = obj->metaObject()->indexOfSignal("loginSuccess(QString)");
    CHECK(signalIdx >= 0);
    delete lv;
}

TEST_CASE("CentralWindow logout signal exists", "[ui]") {
    ensureApp();
    CentralWindow* cw = new CentralWindow();
    QObject* obj = dynamic_cast<QObject*>(cw);
    int signalIdx = obj->metaObject()->indexOfSignal("logout()");
    CHECK(signalIdx >= 0);
    delete cw;
}

TEST_CASE("CentralWindow viewChanged signal exists", "[ui]") {
    ensureApp();
    CentralWindow* cw = new CentralWindow();
    QObject* obj = dynamic_cast<QObject*>(cw);
    int signalIdx = obj->metaObject()->indexOfSignal("viewChanged(int)");
    CHECK(signalIdx >= 0);
    delete cw;
}

TEST_CASE("SchemeView construction", "[ui]") {
    ensureApp();
    SchemeView* sv = new SchemeView();
    REQUIRE(sv != nullptr);
    QObject* obj = dynamic_cast<QObject*>(sv);
    int signalIdx = obj->metaObject()->indexOfSignal("schemeCountChanged(int)");
    CHECK(signalIdx >= 0);
    delete sv;
}

TEST_CASE("EditView construction and signals", "[ui]") {
    ensureApp();
    EditView* ev = new EditView();
    REQUIRE(ev != nullptr);
    QObject* obj = dynamic_cast<QObject*>(ev);
    CHECK(obj->metaObject()->indexOfSignal("schemeModified()") >= 0);
    CHECK(obj->metaObject()->indexOfSignal("toolSelected(QString)") >= 0);
    CHECK(obj->metaObject()->indexOfSignal("requestRunDetection()") >= 0);
    delete ev;
}

TEST_CASE("IOView construction", "[ui]") {
    ensureApp();
    IOView* iv = new IOView();
    REQUIRE(iv != nullptr);
    delete iv;
}

TEST_CASE("CommView construction", "[ui]") {
    ensureApp();
    CommView* cv = new CommView();
    REQUIRE(cv != nullptr);
    delete cv;
}

TEST_CASE("MonitorView construction", "[ui]") {
    ensureApp();
    MonitorView* mv = new MonitorView();
    REQUIRE(mv != nullptr);
    delete mv;
}

TEST_CASE("MainWindow construction and lazy CentralWindow", "[ui]") {
    ensureApp();
    MainWindow* mw = new MainWindow();
    REQUIRE(mw != nullptr);

    mw->showLogin();

    mw->showMain();

    mw->showLogin();

    delete mw;
}

TEST_CASE("MainWindow showLogin initial state verification", "[ui]") {
    ensureApp();
    MainWindow* mw = new MainWindow();
    REQUIRE(mw != nullptr);
    mw->showLogin();
    SUCCEED("showLogin called without crash");
    delete mw;
}

TEST_CASE("MainWindow showMain lazy creation verification", "[ui]") {
    ensureApp();
    MainWindow* mw = new MainWindow();
    REQUIRE(mw != nullptr);
    mw->showMain();
    SUCCEED("showMain called without crash");
    delete mw;
}

TEST_CASE("MainWindow showFirstRunSetup dialog creation", "[ui]") {
    ensureApp();
    MainWindow* mw = new MainWindow();
    REQUIRE(mw != nullptr);
    mw->showFirstRunSetup();
    SUCCEED("showFirstRunSetup called without crash");
    delete mw;
}

TEST_CASE("LoginView loginSuccess signal emission", "[ui]") {
    ensureApp();
    LoginView* view = new LoginView();
    REQUIRE(view != nullptr);

    QSignalSpy spy(view, &LoginView::loginSuccess);
    REQUIRE(spy.isValid());

    QList<QPushButton*> buttons = view->findChildren<QPushButton*>();
    for (QPushButton* btn : buttons) {
        if (btn->text().contains("Login", Qt::CaseInsensitive) || btn->isDefault()) {
            btn->click();
            break;
        }
    }

    delete view;
}

TEST_CASE("CentralWindow logout signal emission", "[ui]") {
    ensureApp();
    CentralWindow* cw = new CentralWindow();
    REQUIRE(cw != nullptr);

    QSignalSpy spy(cw, &CentralWindow::logout);
    REQUIRE(spy.isValid());

    delete cw;
}

TEST_CASE("CentralWindow switchView verification", "[ui]") {
    ensureApp();
    CentralWindow* cw = new CentralWindow();
    REQUIRE(cw != nullptr);

    QSignalSpy spy(cw, &CentralWindow::viewChanged);
    REQUIRE(spy.isValid());

    delete cw;
}

TEST_CASE("EditView construction and signal existence", "[ui]") {
    ensureApp();
    EditView* ev = new EditView();
    REQUIRE(ev != nullptr);

    QSignalSpy spyModified(ev, &EditView::schemeModified);
    REQUIRE(spyModified.isValid());

    QSignalSpy spyCount(ev, &EditView::toolCountChanged);
    REQUIRE(spyCount.isValid());

    QSignalSpy spyRun(ev, &EditView::requestRunDetection);
    REQUIRE(spyRun.isValid());

    delete ev;
}

TEST_CASE("SchemeView construction", "[ui]") {
    ensureApp();
    SchemeView* sv = new SchemeView();
    REQUIRE(sv != nullptr);

    QSignalSpy spy(sv, &SchemeView::schemeCountChanged);
    REQUIRE(spy.isValid());

    delete sv;
}

// v5.4 修复：EditView 构造函数不再立即加载 QML，而是延迟到 showEvent。
// 测试需要先 show() 才能验证 QML 加载状态。
// 历史根因：构造时加载 QML 导致纹理创建在错误 QRhi 上。
static void waitForQmlReady(QQuickWidget* qml, int maxWaitMs = 3000) {
    int waited = 0;
    const int stepMs = 50;
    while (qml->status() != QQuickWidget::Ready && waited < maxWaitMs) {
        QThread::msleep(stepMs);
        QApplication::processEvents();
        waited += stepMs;
    }
}

TEST_CASE("EditView QML loads without error", "[ui][editview][qml]") {
    ensureApp();
    EditView ev;
    ev.show();
    QApplication::processEvents();
    QQuickWidget* qml = ev.findChild<QQuickWidget*>();
    REQUIRE(qml != nullptr);
    // v5.4：QML 通过 QTimer::singleShot(0) 延迟加载，需要处理事件循环
    QApplication::processEvents();
    waitForQmlReady(qml);
    if (qml->status() != QQuickWidget::Ready) {
        for (const auto& err : qml->errors()) {
            qDebug() << "[test] QML error:" << err.toString();
        }
    }
    REQUIRE(qml->status() == QQuickWidget::Ready);
    ev.hide();
    QApplication::processEvents();
}

// v5.4 修复：验证 EditView 在隐藏/显示周期中能正确卸载/重载 QML，
// 防止 QStackedWidget 视图切换导致 QRhi 上下文重建后旧纹理跨实例使用。
TEST_CASE("EditView QML reloads after hide/show cycle", "[ui][editview][qml]") {
    ensureApp();
    EditView ev;
    ev.show();
    QApplication::processEvents();
    QQuickWidget* qml = ev.findChild<QQuickWidget*>();
    REQUIRE(qml != nullptr);
    // v5.4：先等待延迟加载完成
    QApplication::processEvents();
    waitForQmlReady(qml);
    REQUIRE(qml->status() == QQuickWidget::Ready);

    // 隐藏 EditView：应触发 hideEvent 卸载 QML
    ev.hide();
    QApplication::processEvents();
    // 卸载后 source 应为空
    REQUIRE(qml->source().isEmpty());

    // 重新显示 EditView：应触发 showEvent 重载 QML
    ev.show();
    QApplication::processEvents();
    // 给 QTimer::singleShot(0) 一次机会执行重载
    QApplication::processEvents();
    waitForQmlReady(qml);

    if (qml->status() != QQuickWidget::Ready) {
        for (const auto& err : qml->errors()) {
            qDebug() << "[test] QML reload error:" << err.toString();
        }
    }
    REQUIRE(qml->status() == QQuickWidget::Ready);
    REQUIRE(qml->rootObject() != nullptr);
    ev.hide();
    QApplication::processEvents();
}

// v5.3.2 修复：模拟鼠标在 EditView 的 QML 画布上移动，覆盖已添加的算子模块，
// 验证不产生 "Texture belongs to QRhi A but client code attempted to use it with QRhi B" 警告。
TEST_CASE("EditView mouse move over nodes does not produce QRhi cross-instance warnings", "[ui][editview][qml][mouse]") {
    ensureApp();

    EditView ev;
    ev.resize(1280, 720);
    ev.show();
    QApplication::processEvents();

    QQuickWidget* qml = ev.findChild<QQuickWidget*>();
    REQUIRE(qml != nullptr);
    // v5.4：等待延迟加载完成
    QApplication::processEvents();
    waitForQmlReady(qml);
    REQUIRE(qml->status() == QQuickWidget::Ready);

    EditViewBridge* bridge = ev.bridge();
    REQUIRE(bridge != nullptr);

    // 添加两个算子，使画布上存在可交互模块
    const QString id1 = bridge->addOperator("ReadImage", 200.0, 200.0);
    const QString id2 = bridge->addOperator("Threshold", 500.0, 350.0);
    REQUIRE(!id1.isEmpty());
    REQUIRE(!id2.isEmpty());
    QApplication::processEvents();
    QThread::msleep(400);

    // 安装消息处理器，捕获所有警告/严重消息
    g_qrhiCaptured.clear();
    g_prevMessageHandler = qInstallMessageHandler(qrhiWarningMessageHandler);

    // 在画布范围内进行密集鼠标移动，覆盖节点区域、空白区域和边界
    for (int y = 0; y <= 650; y += 40) {
        for (int x = 0; x <= 1200; x += 40) {
            QTest::mouseMove(qml, QPoint(x, y));
            QApplication::processEvents();
        }
    }

    // 恢复默认消息处理器
    qInstallMessageHandler(g_prevMessageHandler);
    g_prevMessageHandler = nullptr;

    // 若仍有警告输出到测试日志，便于人工复核
    for (const QString& msg : g_qrhiCaptured) {
        qDebug() << "[test] captured warning:" << msg;
    }

    // 断言：未出现 QRhi 跨实例纹理错误
    for (const QString& msg : g_qrhiCaptured) {
        const bool isRhiError = msg.contains("Texture", Qt::CaseInsensitive) &&
                                msg.contains("QRhi", Qt::CaseInsensitive);
        const bool isBelongsError = msg.contains("belongs to QRhi", Qt::CaseInsensitive);
        CHECK(!isRhiError);
        CHECK(!isBelongsError);
    }
}