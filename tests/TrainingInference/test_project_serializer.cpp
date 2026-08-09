#include "../catch2/catch2_minimal.hpp"
#include "TrainingInference/ProjectSerializer.h"
#include "TrainingInference/TrainingProject.h"
#include <QTemporaryDir>
#include <QFile>
#include <QImage>
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QTimer>

// 辅助函数：创建临时 PNG 图像
static QString createTempPng(QTemporaryDir& dir, const QString& name, int w, int h, QColor color)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    QString path = dir.filePath(name);
    img.save(path, "PNG");
    return path;
}

TEST_CASE("ProjectSerializer reference mode save/load", "[projectserializer:reference]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    
    // 创建测试图像
    QString imgPath = createTempPng(dir, "test_img.png", 64, 64, QColor(255, 0, 0));
    REQUIRE(QFile::exists(imgPath));
    
    // 创建项目
    TrainingProject project;
    project.setName("引用模式测试");
    project.setFilePath(dir.filePath("test_ref.qdvproj"));
    
    QList<ImageEntrySnapshot> images;
    ImageEntrySnapshot img;
    img.filePath = imgPath;
    img.originalPath = imgPath;
    img.fileName = "test_img.png";
    img.width = 64;
    img.height = 64;
    img.channels = 3;
    img.format = "PNG";
    img.fileSize = 100;
    images.append(img);
    project.setImagesSnapshot(images);
    
    // 保存（引用模式）
    QString savePath = dir.filePath("test_ref.qdvproj");
    QString errMsg;
    bool saved = ProjectSerializer::saveReferenceMode(savePath, &project, &errMsg);
    
    REQUIRE(saved);
    CHECK(QFile::exists(savePath));
    
    // 验证文件是 JSON 格式（非 gzip）
    ProjectSerializer::SaveMode mode = ProjectSerializer::detectMode(savePath);
    CHECK(mode == ProjectSerializer::ModeReference);
    
    // 加载
    TrainingProject loadedProject;
    QStringList missingImages;
    bool loaded = ProjectSerializer::loadReferenceMode(savePath, &loadedProject, &missingImages, &errMsg);
    
    REQUIRE(loaded);
    CHECK(loadedProject.name() == "引用模式测试");
    CHECK(missingImages.isEmpty());  // 图像存在，不应有缺失
    
    // 验证图像快照
    auto loadedImages = loadedProject.imagesSnapshot();
    REQUIRE_EQUAL(loadedImages.size(), 1);
    CHECK(loadedImages[0].filePath == imgPath);
    CHECK(loadedImages[0].width == 64);
}

TEST_CASE("ProjectSerializer bundled mode save/load", "[projectserializer:bundled]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    
    // 创建测试图像
    QString imgPath = createTempPng(dir, "test_bundle.png", 32, 32, QColor(0, 255, 0));
    REQUIRE(QFile::exists(imgPath));
    
    // 创建项目
    TrainingProject project;
    project.setName("打包模式测试");
    
    QList<ImageEntrySnapshot> images;
    ImageEntrySnapshot img;
    img.filePath = imgPath;
    img.originalPath = imgPath;
    img.fileName = "test_bundle.png";
    img.width = 32;
    img.height = 32;
    img.channels = 3;
    img.format = "PNG";
    images.append(img);
    project.setImagesSnapshot(images);
    
    // 保存（打包模式）
    QString savePath = dir.filePath("test_bundle.qdvproj");
    QString errMsg;
    bool saved = ProjectSerializer::saveBundledMode(savePath, &project, &errMsg);
    
    REQUIRE(saved);
    CHECK(QFile::exists(savePath));
    
    // 验证文件是打包格式（gzip）
    ProjectSerializer::SaveMode mode = ProjectSerializer::detectMode(savePath);
    CHECK(mode == ProjectSerializer::ModeBundled);
    
    // 加载
    TrainingProject loadedProject;
    bool loaded = ProjectSerializer::loadBundledMode(savePath, &loadedProject, &errMsg);
    
    REQUIRE(loaded);
    CHECK(loadedProject.name() == "打包模式测试");
    
    // 验证图像快照（路径应被重写为临时目录路径）
    auto loadedImages = loadedProject.imagesSnapshot();
    REQUIRE_EQUAL(loadedImages.size(), 1);
    CHECK(loadedImages[0].fileName == "test_bundle.png");
    // 打包模式加载后路径应指向临时目录
    CHECK(loadedImages[0].filePath.contains("images/"));
    // 图像文件应存在
    CHECK(QFile::exists(loadedImages[0].filePath));
}

TEST_CASE("ProjectSerializer missing images detection", "[projectserializer:missing]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    
    // 创建项目文件，引用不存在的图像
    QString savePath = dir.filePath("test_missing.qdvproj");
    
    TrainingProject project;
    project.setName("缺失图像测试");
    
    QList<ImageEntrySnapshot> images;
    ImageEntrySnapshot img;
    img.filePath = "/nonexistent/path/image.png";
    img.fileName = "image.png";
    images.append(img);
    project.setImagesSnapshot(images);
    
    // 保存（引用模式）
    QString errMsg;
    bool saved = ProjectSerializer::saveReferenceMode(savePath, &project, &errMsg);
    REQUIRE(saved);
    
    // 加载
    TrainingProject loadedProject;
    QStringList missingImages;
    bool loaded = ProjectSerializer::loadReferenceMode(savePath, &loadedProject, &missingImages, &errMsg);
    
    REQUIRE(loaded);
    REQUIRE_EQUAL(missingImages.size(), 1);
    CHECK(missingImages[0] == "/nonexistent/path/image.png");
}

TEST_CASE("ProjectSerializer detectMode", "[projectserializer:detectmode]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    
    // 测试引用模式检测
    QString refPath = dir.filePath("ref.qdvproj");
    TrainingProject project;
    project.setName("test");
    ProjectSerializer::saveReferenceMode(refPath, &project, nullptr);
    CHECK(ProjectSerializer::detectMode(refPath) == ProjectSerializer::ModeReference);
    
    // 测试打包模式检测
    QString bundlePath = dir.filePath("bundle.qdvproj");
    ProjectSerializer::saveBundledMode(bundlePath, &project, nullptr);
    CHECK(ProjectSerializer::detectMode(bundlePath) == ProjectSerializer::ModeBundled);
}
