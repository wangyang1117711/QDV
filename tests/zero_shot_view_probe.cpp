// ============================================================================
// 零样本检测视图"能否打开"独立探针
// 目的：在沙箱中隔离验证 ZeroShotDetectView 的构造 / 首显 / QStackedWidget 切换
//       动画后的实际几何尺寸，复现"界面未打开"缺陷。
// 该探针独立成 exe，避免 QDV_tests 全量在无关大测试上 OOM 段错误。
// 进度以 fflush 写入 %TEMP%/zsv_probe.log，即使子进程崩溃也能留存证据。
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <QApplication>
#include <QStackedWidget>
#include <QSplitter>
#include <QWidget>
#include <QString>
#include <QRect>
#include <QTimer>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QLayout>
#include "UI/ZeroShotDetectView.h"

static const char* LOG = "C:/Users/wangy/AppData/Local/Temp/zsv_probe.log";

static void mark(const char* s) {
    FILE* f = fopen(LOG, "a");
    if (f) { fprintf(f, "%s\n", s); fflush(f); fclose(f); }
}
static void markf(const char* fmt, ...) {
    FILE* f = fopen(LOG, "a");
    if (f) {
        va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
        fprintf(f, "\n"); fflush(f); fclose(f);
    }
}

int main(int argc, char** argv) {
    // 清空旧日志
    FILE* clr = fopen(LOG, "w"); if (clr) fclose(clr);

    mark("START");
    QApplication app(argc, argv);
    mark("APP_CREATED");

    // 1) 构造
    ZeroShotDetectView* view = nullptr;
    try {
        view = new ZeroShotDetectView();
        mark("CONSTRUCTED_OK");
    } catch (const std::exception& e) {
        markf("CONSTRUCT_THREW: %s", e.what());
        return 2;
    } catch (...) {
        mark("CONSTRUCT_THREW_UNKNOWN");
        return 2;
    }

    // 2) 放入 QStackedWidget 并切到它（模拟 CentralWindow 的索引 8）
    QStackedWidget* stack = new QStackedWidget();
    QWidget* dummy = new QWidget();  // 占位页 index 0
    stack->addWidget(dummy);
    stack->addWidget(view);          // 零样本视图 index 1
    const int ZS_INDEX = 1;

    // 给栈一个真实尺寸（模拟主窗口中央区域）
    stack->resize(1200, 800);
    stack->show();
    QApplication::processEvents();
    mark("STACK_SHOWN");

    // --- 复刻 CentralWindow::switchView 的滑动动画逻辑 ---
    const int oldIndex = stack->currentIndex();  // 0
    QWidget* newWidget = stack->widget(ZS_INDEX);

    // ---- 测试 A：直接 setCurrentIndex（等价 finalizeSwitch 把几何置为满尺寸） ----
    stack->setCurrentIndex(ZS_INDEX);
    QRect gDirect = view->geometry();
    markf("A_DIRECT_SHOW viewGeometry=%d,%d,%d,%d visible=%d",
          gDirect.x(), gDirect.y(), gDirect.width(), gDirect.height(),
          view->isVisible() ? 1 : 0);

    // ---- 测试 B：复刻 CentralWindow::switchView 的滑动动画 + finalizeSwitch ----
    // 先切回占位页，再走动画路径
    stack->setCurrentIndex(0);
    QApplication::processEvents();

    const QRect targetGeometry = newWidget->geometry();  // 切之前捕获（关键嫌疑点）
    markf("B_BEFORE_SWITCH targetGeometry=%d,%d,%d,%d",
          targetGeometry.x(), targetGeometry.y(),
          targetGeometry.width(), targetGeometry.height());

    const QRect tg = targetGeometry;
    const int xOffset = stack->width();
    newWidget->move(tg.x() + xOffset, tg.y());
    stack->setCurrentIndex(ZS_INDEX);
    QPropertyAnimation* anim = new QPropertyAnimation(newWidget, "geometry", &app);
    anim->setDuration(200);
    anim->setStartValue(QRect(tg.x() + xOffset, tg.y(), tg.width(), tg.height()));
    anim->setEndValue(tg);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    // finalizeSwitch（与 CentralWindow 完全一致）：动画结束后强制置为满尺寸 + 发 resize
    auto finalizeSwitch = [&]() {
        const QSize sz = stack->size();
        if (sz.isValid() && sz.width() > 0 && sz.height() > 0) {
            newWidget->setGeometry(stack->rect());
            newWidget->updateGeometry();
            if (QLayout* l = newWidget->layout()) { l->invalidate(); l->activate(); }
        }
    };
    QObject::connect(anim, &QPropertyAnimation::finished, &app, [finalizeSwitch, &app, view]() {
        finalizeSwitch();
        QRect g = view->geometry();
        markf("B_AFTER_ANIM_FINALIZE viewGeometry=%d,%d,%d,%d visible=%d",
              g.x(), g.y(), g.width(), g.height(), view->isVisible() ? 1 : 0);
        QSplitter* sp = view->findChild<QSplitter*>(QString(), Qt::FindDirectChildrenOnly);
        if (sp) {
            QList<int> sz = sp->sizes();
            QString s;
            for (int v : sz) s += QString::number(v) + " ";
            markf("SPLITTER_SIZES=%s", s.toUtf8().constData());
            mark(sp->isVisible() ? "SPLITTER_VISIBLE" : "SPLITTER_HIDDEN");
        } else {
            mark("SPLITTER_MISSING");
        }
        mark("PROBE_DONE");
        app.quit();
    });
    anim->start();

    const int rc = app.exec();
    markf("APP_EXEC_RC=%d", rc);
    return 0;
}
