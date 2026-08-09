#include "../catch2/catch2_minimal.hpp"
#include "TrainingInference/TrainingInferenceView.h"
#include "TrainingInference/ImageManager.h"
#include <QApplication>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QListWidget>
#include <QListWidgetItem>
#include <QImage>
#include <QColor>
#include <QTemporaryDir>
#include <QDir>
#include <QFileInfo>
#include <QPainter>

inline void ensureApp() {
    // 复用 test_main.cpp 中创建的全局 QApplication 实例
    if (!QCoreApplication::instance()) {
        static int argc = 0;
        static QApplication a(argc, nullptr);
    }
}

static QString createTempPng(QTemporaryDir& dir, const QString& name, int w, int h, QColor color)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    QString path = dir.filePath(name);
    img.save(path, "PNG");
    return path;
}

static void invokeDeleteSelected(TrainingInferenceView* view)
{
    QMetaObject::invokeMethod(view, "onDeleteSelected", Qt::DirectConnection);
}

static void invokeClearAll(TrainingInferenceView* view)
{
    QMetaObject::invokeMethod(view, "onClearAll", Qt::DirectConnection);
}

static void invokeImagesImported(TrainingInferenceView* view, int count)
{
    QMetaObject::invokeMethod(view, "onImagesImported", Qt::DirectConnection,
                              Q_ARG(int, count));
}

TEST_CASE("TrainingInferenceView 删除选中图片后列表视觉状态正确", "[ui:delete]")
{
    ensureApp();
    auto* mgr = ImageManager::instance();
    mgr->clearImages();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QString path1 = createTempPng(dir, "a.png", 64, 64, QColor(255, 0, 0));
    QString path2 = createTempPng(dir, "b.png", 64, 64, QColor(0, 255, 0));
    QString path3 = createTempPng(dir, "c.png", 64, 64, QColor(0, 0, 255));

    mgr->importImages({path1, path2, path3});
    REQUIRE_EQUAL(mgr->imageCount(), 3);

    TrainingInferenceView* view = new TrainingInferenceView();
    QListWidget* list = view->findChild<QListWidget*>();
    REQUIRE(list != nullptr);
    REQUIRE_EQUAL(list->count(), 3);

    SECTION("删除中间项后列表count减少且剩余项正确")
    {
        list->setCurrentRow(1);
        invokeDeleteSelected(view);

        REQUIRE_EQUAL(mgr->imageCount(), 2);
        REQUIRE_EQUAL(list->count(), 2);

        QStringList paths = mgr->allPaths();
        CHECK(paths.contains(path1));
        CHECK(!paths.contains(path2));
        CHECK(paths.contains(path3));

        QStringList visiblePaths;
        for (int i = 0; i < list->count(); ++i) {
            visiblePaths.append(list->item(i)->toolTip());
        }
        CHECK(visiblePaths.contains(path1));
        CHECK(!visiblePaths.contains(path2));
        CHECK(visiblePaths.contains(path3));
    }

    SECTION("删除首项后列表正确更新")
    {
        list->setCurrentRow(0);
        invokeDeleteSelected(view);

        REQUIRE_EQUAL(list->count(), 2);

        QStringList visiblePaths;
        for (int i = 0; i < list->count(); ++i) {
            visiblePaths.append(list->item(i)->toolTip());
        }
        CHECK(!visiblePaths.contains(path1));
        CHECK(visiblePaths.contains(path2));
        CHECK(visiblePaths.contains(path3));
    }

    SECTION("删除末项后列表正确更新")
    {
        list->setCurrentRow(2);
        invokeDeleteSelected(view);

        REQUIRE_EQUAL(list->count(), 2);

        QStringList visiblePaths;
        for (int i = 0; i < list->count(); ++i) {
            visiblePaths.append(list->item(i)->toolTip());
        }
        CHECK(visiblePaths.contains(path1));
        CHECK(visiblePaths.contains(path2));
        CHECK(!visiblePaths.contains(path3));
    }

    SECTION("删除唯一项后列表为空")
    {
        mgr->clearImages();
        mgr->importImages({path1});
        invokeImagesImported(view, 1);
        REQUIRE_EQUAL(list->count(), 1);

        list->setCurrentRow(0);
        invokeDeleteSelected(view);

        REQUIRE_EQUAL(mgr->imageCount(), 0);
        REQUIRE_EQUAL(list->count(), 0);
    }

    delete view;
    mgr->clearImages();
}

TEST_CASE("TrainingInferenceView 清空列表后视觉状态正确", "[ui:clear]")
{
    ensureApp();
    auto* mgr = ImageManager::instance();
    mgr->clearImages();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QString path1 = createTempPng(dir, "x.png", 64, 64, QColor(100, 0, 0));
    QString path2 = createTempPng(dir, "y.png", 64, 64, QColor(0, 100, 0));

    mgr->importImages({path1, path2});
    REQUIRE_EQUAL(mgr->imageCount(), 2);

    TrainingInferenceView* view = new TrainingInferenceView();
    QListWidget* list = view->findChild<QListWidget*>();
    REQUIRE_EQUAL(list->count(), 2);

    SECTION("清空非空列表后数据层和视图层均为空")
    {
        invokeClearAll(view);

        REQUIRE_EQUAL(mgr->imageCount(), 0);
        REQUIRE_EQUAL(list->count(), 0);
    }

    SECTION("清空后再次导入图片正常工作")
    {
        invokeClearAll(view);
        REQUIRE_EQUAL(list->count(), 0);

        QString path3 = createTempPng(dir, "z.png", 64, 64, QColor(0, 0, 200));
        mgr->importImages({path3});
        invokeImagesImported(view, 1);

        REQUIRE_EQUAL(mgr->imageCount(), 1);
        REQUIRE_EQUAL(list->count(), 1);
        CHECK(list->item(0)->toolTip() == path3);
    }

    SECTION("对已空列表再次清空不崩溃")
    {
        invokeClearAll(view);
        REQUIRE_EQUAL(list->count(), 0);

        REQUIRE_NOTHROW(invokeClearAll(view));
        REQUIRE_EQUAL(list->count(), 0);
    }

    delete view;
    mgr->clearImages();
}

TEST_CASE("TrainingInferenceView 删除操作信号完整性验证", "[ui:delete-signals]")
{
    ensureApp();
    auto* mgr = ImageManager::instance();
    mgr->clearImages();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QString path = createTempPng(dir, "sig.png", 64, 64, QColor(50, 50, 50));
    mgr->importImages({path});

    TrainingInferenceView* view = new TrainingInferenceView();
    QListWidget* list = view->findChild<QListWidget*>();

    QSignalSpy spyRemoved(mgr, &ImageManager::imageRemoved);
    REQUIRE(spyRemoved.isValid());

    list->setCurrentRow(0);
    invokeDeleteSelected(view);

    REQUIRE_EQUAL(spyRemoved.count(), 1);
    CHECK(spyRemoved.at(0).at(0).toString() == path);

    delete view;
    mgr->clearImages();
}

TEST_CASE("TrainingInferenceView 清空操作信号完整性验证", "[ui:clear-signals]")
{
    ensureApp();
    auto* mgr = ImageManager::instance();
    mgr->clearImages();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QString path1 = createTempPng(dir, "c1.png", 64, 64, QColor(10, 20, 30));
    QString path2 = createTempPng(dir, "c2.png", 64, 64, QColor(40, 50, 60));
    mgr->importImages({path1, path2});

    TrainingInferenceView* view = new TrainingInferenceView();

    QSignalSpy spyCleared(mgr, &ImageManager::imagesCleared);
    REQUIRE(spyCleared.isValid());

    invokeClearAll(view);

    REQUIRE_EQUAL(spyCleared.count(), 1);

    delete view;
    mgr->clearImages();
}

TEST_CASE("TrainingInferenceView 删除操作性能<300ms", "[ui:delete-performance]")
{
    ensureApp();
    auto* mgr = ImageManager::instance();
    mgr->clearImages();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QStringList paths;
    for (int i = 0; i < 10; ++i) {
        paths.append(createTempPng(dir, QString("perf_%1.png").arg(i),
                                   64, 64, QColor(i * 25, 100, 200 - i * 20)));
    }
    mgr->importImages(paths);
    REQUIRE_EQUAL(mgr->imageCount(), 10);

    TrainingInferenceView* view = new TrainingInferenceView();
    QListWidget* list = view->findChild<QListWidget*>();
    REQUIRE_EQUAL(list->count(), 10);

    QElapsedTimer timer;
    timer.start();

    list->setCurrentRow(5);
    invokeDeleteSelected(view);

    qint64 elapsed = timer.elapsed();
    CHECK(elapsed < 300);
    REQUIRE_EQUAL(list->count(), 9);

    delete view;
    mgr->clearImages();
}

TEST_CASE("TrainingInferenceView 清空操作性能<300ms", "[ui:clear-performance]")
{
    ensureApp();
    auto* mgr = ImageManager::instance();
    mgr->clearImages();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QStringList paths;
    for (int i = 0; i < 10; ++i) {
        paths.append(createTempPng(dir, QString("cperf_%1.png").arg(i),
                                   64, 64, QColor(i * 25, 50, 200)));
    }
    mgr->importImages(paths);
    REQUIRE_EQUAL(mgr->imageCount(), 10);

    TrainingInferenceView* view = new TrainingInferenceView();
    QListWidget* list = view->findChild<QListWidget*>();
    REQUIRE_EQUAL(list->count(), 10);

    QElapsedTimer timer;
    timer.start();

    invokeClearAll(view);

    qint64 elapsed = timer.elapsed();
    CHECK(elapsed < 300);
    REQUIRE_EQUAL(list->count(), 0);

    delete view;
    mgr->clearImages();
}

TEST_CASE("TrainingInferenceView 连续删除操作数据一致性", "[ui:delete-consistency]")
{
    ensureApp();
    auto* mgr = ImageManager::instance();
    mgr->clearImages();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QStringList paths;
    for (int i = 0; i < 5; ++i) {
        paths.append(createTempPng(dir, QString("seq_%1.png").arg(i),
                                   64, 64, QColor(i * 50, i * 30, 255)));
    }
    mgr->importImages(paths);
    REQUIRE_EQUAL(mgr->imageCount(), 5);

    TrainingInferenceView* view = new TrainingInferenceView();
    QListWidget* list = view->findChild<QListWidget*>();
    REQUIRE_EQUAL(list->count(), 5);

    SECTION("从后往前连续删除全部")
    {
        for (int i = 4; i >= 0; --i) {
            REQUIRE_EQUAL(list->count(), i + 1);
            list->setCurrentRow(i);
            invokeDeleteSelected(view);
        }
        REQUIRE_EQUAL(mgr->imageCount(), 0);
        REQUIRE_EQUAL(list->count(), 0);
    }

    SECTION("从前往后连续删除全部")
    {
        for (int i = 0; i < 5; ++i) {
            REQUIRE_EQUAL(list->count(), 5 - i);
            list->setCurrentRow(0);
            invokeDeleteSelected(view);
        }
        REQUIRE_EQUAL(mgr->imageCount(), 0);
        REQUIRE_EQUAL(list->count(), 0);
    }

    delete view;
    mgr->clearImages();
}

TEST_CASE("TrainingInferenceView 导入→删除→清空→再导入 完整生命周期", "[ui:lifecycle]")
{
    ensureApp();
    auto* mgr = ImageManager::instance();
    mgr->clearImages();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    TrainingInferenceView* view = new TrainingInferenceView();
    QListWidget* list = view->findChild<QListWidget*>();
    REQUIRE_EQUAL(list->count(), 0);

    QString pathA = createTempPng(dir, "life_a.png", 64, 64, QColor(255, 0, 0));
    QString pathB = createTempPng(dir, "life_b.png", 64, 64, QColor(0, 255, 0));
    QString pathC = createTempPng(dir, "life_c.png", 64, 64, QColor(0, 0, 255));

    mgr->importImages({pathA, pathB, pathC});
    invokeImagesImported(view, 3);
    REQUIRE_EQUAL(list->count(), 3);

    list->setCurrentRow(1);
    invokeDeleteSelected(view);
    REQUIRE_EQUAL(list->count(), 2);

    invokeClearAll(view);
    REQUIRE_EQUAL(list->count(), 0);

    mgr->importImages({pathA});
    invokeImagesImported(view, 1);
    REQUIRE_EQUAL(list->count(), 1);
    CHECK(list->item(0)->toolTip() == pathA);

    delete view;
    mgr->clearImages();
}

TEST_CASE("TrainingInferenceView 未选中任何项时删除不崩溃", "[ui:delete-edge]")
{
    ensureApp();
    auto* mgr = ImageManager::instance();
    mgr->clearImages();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QString path = createTempPng(dir, "edge.png", 64, 64, QColor(128, 128, 128));
    mgr->importImages({path});

    TrainingInferenceView* view = new TrainingInferenceView();
    QListWidget* list = view->findChild<QListWidget*>();
    REQUIRE_EQUAL(list->count(), 1);

    list->setCurrentRow(-1);

    REQUIRE_NOTHROW(invokeDeleteSelected(view));
    REQUIRE_EQUAL(list->count(), 1);
    REQUIRE_EQUAL(mgr->imageCount(), 1);

    delete view;
    mgr->clearImages();
}