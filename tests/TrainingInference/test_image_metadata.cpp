#include "../catch2/catch2_minimal.hpp"
#include "TrainingInference/ImageManager.h"
#include <QImage>
#include <QColor>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QFile>

static QString createTempImage(QTemporaryDir& dir, const QString& name, int w, int h, QColor color, const char* format)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    QString path = dir.filePath(name);
    img.save(path, format);
    return path;
}

TEST_CASE("ImageEntry metadata extraction PNG", "[imagemetadata:png]")
{
    auto* mgr = ImageManager::instance();
    mgr->clearImages();
    
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    
    QString path = createTempImage(dir, "meta_test.png", 1920, 1080, QColor(255, 0, 0), "PNG");
    REQUIRE(QFile::exists(path));
    
    QStringList imported = mgr->importImages({path});
    REQUIRE_EQUAL(imported.size(), 1);
    
    ImageEntry entry = mgr->imageInfo(path);
    CHECK(entry.width == 1920);
    CHECK(entry.height == 1080);
    CHECK(entry.channels >= 1);  // 至少1个通道
    CHECK(entry.format == "PNG");
    CHECK(entry.fileSize > 0);
}

TEST_CASE("ImageEntry metadata extraction BMP", "[imagemetadata:bmp]")
{
    auto* mgr = ImageManager::instance();
    mgr->clearImages();
    
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    
    QString path = createTempImage(dir, "meta_test.bmp", 640, 480, QColor(0, 255, 0), "BMP");
    REQUIRE(QFile::exists(path));
    
    QStringList imported = mgr->importImages({path});
    REQUIRE_EQUAL(imported.size(), 1);
    
    ImageEntry entry = mgr->imageInfo(path);
    CHECK(entry.width == 640);
    CHECK(entry.height == 480);
    CHECK(entry.format == "BMP");
    CHECK(entry.fileSize > 0);
}

TEST_CASE("ImageEntry metadata extraction JPEG", "[imagemetadata:jpeg]")
{
    auto* mgr = ImageManager::instance();
    mgr->clearImages();
    
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    
    QString path = createTempImage(dir, "meta_test.jpg", 800, 600, QColor(0, 0, 255), "JPEG");
    REQUIRE(QFile::exists(path));
    
    QStringList imported = mgr->importImages({path});
    REQUIRE_EQUAL(imported.size(), 1);
    
    ImageEntry entry = mgr->imageInfo(path);
    CHECK(entry.width == 800);
    CHECK(entry.height == 600);
    CHECK(entry.format == "JPEG");
    CHECK(entry.fileSize > 0);
}

TEST_CASE("ImageEntry corrupted image does not block import", "[imagemetadata:corrupt]")
{
    auto* mgr = ImageManager::instance();
    mgr->clearImages();
    
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    
    // 创建一个损坏的"图像"文件（内容不是有效图像）
    QString corruptPath = dir.filePath("corrupt.png");
    {
        QFile f(corruptPath);
        if (!f.open(QIODevice::WriteOnly)) {
            REQUIRE(false);
        }
        f.write("This is not a valid PNG file");
        f.close();
    }
    REQUIRE(QFile::exists(corruptPath));
    
    // 导入应失败（QPixmap 无法加载），但不应崩溃
    QStringList imported = mgr->importImages({corruptPath});
    CHECK(imported.isEmpty());
    
    // ImageManager 不应包含该图像
    CHECK(mgr->imageCount() == 0);
}

TEST_CASE("ImageManager toSnapshot/importFromSnapshot roundtrip", "[imagemanager:snapshot]")
{
    auto* mgr = ImageManager::instance();
    mgr->clearImages();
    
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    
    // 创建并导入测试图像
    QString path1 = createTempImage(dir, "snap1.png", 100, 100, QColor(255, 0, 0), "PNG");
    QString path2 = createTempImage(dir, "snap2.png", 200, 200, QColor(0, 255, 0), "PNG");
    
    mgr->importImages({path1, path2});
    mgr->setLabel(path1, "类别A");
    
    // 收集快照
    QList<ImageEntrySnapshot> snapshots = mgr->toSnapshot();
    REQUIRE_EQUAL(snapshots.size(), 2);
    
    // 验证快照内容
    bool foundPath1 = false, foundPath2 = false;
    for (const auto& snap : snapshots) {
        if (snap.filePath == path1) {
            CHECK(snap.width == 100);
            CHECK(snap.label == "类别A");
            CHECK(snap.format == "PNG");
            foundPath1 = true;
        }
        if (snap.filePath == path2) {
            CHECK(snap.width == 200);
            foundPath2 = true;
        }
    }
    CHECK(foundPath1);
    CHECK(foundPath2);
    
    // 清空后从快照恢复
    mgr->clearImages();
    CHECK(mgr->imageCount() == 0);
    
    mgr->importFromSnapshot(snapshots);
    CHECK(mgr->imageCount() == 2);
    
    // 验证恢复后的数据
    ImageEntry entry1 = mgr->imageInfo(path1);
    CHECK(entry1.label == "类别A");
    CHECK(entry1.width == 100);
    
    ImageEntry entry2 = mgr->imageInfo(path2);
    CHECK(entry2.width == 200);
}
