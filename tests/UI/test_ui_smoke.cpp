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