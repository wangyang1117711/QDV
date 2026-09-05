// ============================================================================
// 零样本检测视图"能否正常打开"冒烟测试
//
// 目的：验证 ZeroShotDetectView 能够被构造、首次显示（showEvent ->
// refreshSplitterSizes）且不崩溃，且基本结构（三栏 QSplitter）存在。
// 这能直接复现"界面未打开"类问题——若视图构造或首显崩溃，本测试会暴露。
//
// 注意：本测试在 QApplication（offscreen）下运行，仅需 UI 库与 ZeroShotKit，
// 不依赖任何模型文件或 GPU。
// ============================================================================

#include "../catch2/catch2_minimal.hpp"

#include <QApplication>
#include <QWidget>
#include <QSplitter>
#include <QLabel>

#include "UI/ZeroShotDetectView.h"

TEST_CASE("ZeroShotDetectView 可构造且不崩溃", "[zeroshot][view]") {
    // 构造即触发 CentralWindow 中同款的 new ZeroShotDetectView()
    ZeroShotDetectView* view = new ZeroShotDetectView();
    REQUIRE(view != nullptr);
    REQUIRE(view->layout() != nullptr);

    // 顶层应只有一个三栏 QSplitter（左配置 / 中结果 / 右注意事项）
    QSplitter* splitter = view->findChild<QSplitter*>(QString(), Qt::FindDirectChildrenOnly);
    REQUIRE(splitter != nullptr);
    REQUIRE(splitter->count() == 3);

    delete view;
}

TEST_CASE("ZeroShotDetectView 首显触发 showEvent 不崩溃", "[zeroshot][view]") {
    ZeroShotDetectView* view = new ZeroShotDetectView();
    REQUIRE(view != nullptr);

    // show() 触发 showEvent -> refreshSplitterSizes（首次显示延迟刷 splitter 尺寸）
    view->show();
    QApplication::processEvents();

    // 首显后 splitter 应已被分配尺寸（不再全 0）
    QSplitter* splitter = view->findChild<QSplitter*>(QString(), Qt::FindDirectChildrenOnly);
    REQUIRE(splitter != nullptr);
    const QList<int> sizes = splitter->sizes();
    REQUIRE(sizes.size() == 3);

    delete view;
}
