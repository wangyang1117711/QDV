#include "../catch2/catch2_minimal.hpp"
#include "TrainingInference/ImageManager.h"
#include <QImage>
#include <QColor>
#include <QTemporaryDir>
#include <QDir>
#include <QFileInfo>
#include <QPainter>

static QString createTempPng(QTemporaryDir& dir, const QString& name, int w, int h, QColor color)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    QString path = dir.filePath(name);
    bool saved = img.save(path, "PNG");
    if (!saved) {
        return QString();
    }
    return path;
}

TEST_CASE("ImageManager removeImage deletes from data store", "[imagemanager:delete]")
{
    auto* mgr = ImageManager::instance();
    mgr->clearImages();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QString path1 = createTempPng(dir, "test_a.png", 64, 64, QColor(255, 0, 0));
    QString path2 = createTempPng(dir, "test_b.png", 64, 64, QColor(0, 255, 0));
    QString path3 = createTempPng(dir, "test_c.png", 64, 64, QColor(0, 0, 255));

    QStringList paths = {path1, path2, path3};
    QStringList imported = mgr->importImages(paths);
    REQUIRE_EQUAL(imported.size(), 3);
    REQUIRE_EQUAL(mgr->imageCount(), 3);

    SECTION("remove single image")
    {
        mgr->removeImage(path2);
        REQUIRE_EQUAL(mgr->imageCount(), 2);

        QStringList remaining = mgr->allPaths();
        CHECK(remaining.contains(path1));
        CHECK(!remaining.contains(path2));
        CHECK(remaining.contains(path3));
    }

    SECTION("remove non-existent image")
    {
        QString fakePath = dir.filePath("nonexistent.png");
        mgr->removeImage(fakePath);
        REQUIRE_EQUAL(mgr->imageCount(), 3);
    }

    SECTION("remove same image twice")
    {
        mgr->removeImage(path1);
        REQUIRE_EQUAL(mgr->imageCount(), 2);
        mgr->removeImage(path1);
        REQUIRE_EQUAL(mgr->imageCount(), 2);
    }

    mgr->clearImages();
}

TEST_CASE("ImageManager clearImages removes all data", "[imagemanager:clear]")
{
    auto* mgr = ImageManager::instance();
    mgr->clearImages();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QString path1 = createTempPng(dir, "img1.png", 32, 32, QColor(255, 0, 0));
    QString path2 = createTempPng(dir, "img2.png", 32, 32, QColor(0, 255, 0));

    QStringList imported = mgr->importImages({path1, path2});
    REQUIRE_EQUAL(imported.size(), 2);
    REQUIRE_EQUAL(mgr->imageCount(), 2);

    SECTION("clear non-empty store")
    {
        mgr->clearImages();
        REQUIRE_EQUAL(mgr->imageCount(), 0);
        REQUIRE(mgr->allPaths().isEmpty());
        REQUIRE(mgr->images().isEmpty());
    }

    SECTION("clear already empty store")
    {
        mgr->clearImages();
        REQUIRE_EQUAL(mgr->imageCount(), 0);
        mgr->clearImages();
        REQUIRE_EQUAL(mgr->imageCount(), 0);
    }

    mgr->clearImages();
}

TEST_CASE("ImageManager import after clear works correctly", "[imagemanager:delete-clear]")
{
    auto* mgr = ImageManager::instance();
    mgr->clearImages();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QString path1 = createTempPng(dir, "batch1_a.png", 64, 64, QColor(100, 0, 0));
    QStringList imported1 = mgr->importImages({path1});
    REQUIRE_EQUAL(imported1.size(), 1);

    mgr->clearImages();
    REQUIRE_EQUAL(mgr->imageCount(), 0);

    QString path2 = createTempPng(dir, "batch2_b.png", 64, 64, QColor(0, 100, 0));
    QString path3 = createTempPng(dir, "batch2_c.png", 64, 64, QColor(0, 0, 100));
    QStringList imported2 = mgr->importImages({path2, path3});
    REQUIRE_EQUAL(imported2.size(), 2);
    REQUIRE_EQUAL(mgr->imageCount(), 2);

    QStringList paths = mgr->allPaths();
    CHECK(!paths.contains(path1));
    CHECK(paths.contains(path2));
    CHECK(paths.contains(path3));

    mgr->clearImages();
}

TEST_CASE("ImageManager remove then re-import same file", "[imagemanager:delete]")
{
    auto* mgr = ImageManager::instance();
    mgr->clearImages();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QString path = createTempPng(dir, "reimport.png", 64, 64, QColor(128, 128, 128));

    QStringList imported1 = mgr->importImages({path});
    REQUIRE_EQUAL(imported1.size(), 1);
    REQUIRE_EQUAL(mgr->imageCount(), 1);

    mgr->removeImage(path);
    REQUIRE_EQUAL(mgr->imageCount(), 0);

    QStringList imported2 = mgr->importImages({path});
    REQUIRE_EQUAL(imported2.size(), 1);
    REQUIRE_EQUAL(mgr->imageCount(), 1);

    mgr->clearImages();
}

TEST_CASE("ImageManager imageInfo returns empty for removed file", "[imagemanager:delete]")
{
    auto* mgr = ImageManager::instance();
    mgr->clearImages();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QString path = createTempPng(dir, "info_test.png", 64, 64, QColor(50, 100, 200));
    QStringList imported = mgr->importImages({path});
    REQUIRE_EQUAL(imported.size(), 1);

    ImageEntry beforeDelete = mgr->imageInfo(path);
    CHECK(beforeDelete.fileName == "info_test.png");

    mgr->removeImage(path);

    ImageEntry afterDelete = mgr->imageInfo(path);
    CHECK(afterDelete.filePath.isEmpty());
    CHECK(afterDelete.fileName.isEmpty());

    mgr->clearImages();
}