#include "TrainingInference/ImageManager.h"
#include "Core/Logger.h"
#include <QPixmap>
#include <QImageReader>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QByteArray>
#include <opencv2/imgcodecs.hpp>

ImageManager* ImageManager::s_instance = nullptr;

ImageManager* ImageManager::instance()
{
    if (!s_instance)
    {
        s_instance = new ImageManager();
    }
    return s_instance;
}

ImageManager::ImageManager(QObject* parent)
    : QObject(parent)
{
}

QStringList ImageManager::importImages(const QStringList& filePaths)
{
    QStringList imported;

    for (const QString& filePath : filePaths)
    {
        if (m_images.contains(filePath))
        {
            QDV::Logger::warn(QString("Duplicate image skipped: %1").arg(filePath));
            continue;
        }

        QFileInfo fi(filePath);
        if (!fi.exists())
        {
            QDV::Logger::error(QString("File not found: %1").arg(filePath));
            continue;
        }

        QString suffix = fi.suffix().toLower();
        if (suffix != "png" && suffix != "jpg" && suffix != "jpeg" &&
            suffix != "bmp" && suffix != "tiff" && suffix != "tif" && suffix != "webp")
        {
            QDV::Logger::warn(QString("Unsupported format .%1: %2").arg(suffix).arg(filePath));
            continue;
        }

        QPixmap pixmap(filePath);
        if (pixmap.isNull())
        {
            QDV::Logger::error(QString("Failed to load image: %1").arg(filePath));
            continue;
        }

        ImageEntry entry;
        entry.filePath = filePath;
        entry.fileName = fi.fileName();
        entry.icon = pixmap.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        entry.label = QString();
        entry.isAnnotated = false;
        entry.isSelected = false;

        // 提取图像元数据（提取失败时字段保持默认值，不阻断导入流程）
        extractMetadata(entry);

        m_images[filePath] = entry;
        imported.append(filePath);
    }

    if (!imported.isEmpty())
    {
        emit imagesImported(imported.size());
    }

    return imported;
}

void ImageManager::extractMetadata(ImageEntry& entry)
{
    // 获取文件大小
    QFile file(entry.filePath);
    if (file.open(QIODevice::ReadOnly)) {
        entry.fileSize = file.size();
        // 读取文件内容用于 cv::imdecode（遵循 Unicode 路径约束，禁用 cv::imread）
        QByteArray fileData = file.readAll();
        file.close();

        // 用 cv::imdecode 解码图像获取尺寸和通道信息
        cv::Mat img = cv::imdecode(cv::Mat(1, fileData.size(), CV_8UC1, fileData.data()), cv::IMREAD_UNCHANGED);
        if (!img.empty()) {
            entry.width = img.cols;
            entry.height = img.rows;
            entry.channels = img.channels();
        }
    }

    // 后缀名映射格式字符串
    QString suffix = QFileInfo(entry.filePath).suffix().toLower();
    if (suffix == "png") entry.format = "PNG";
    else if (suffix == "jpg" || suffix == "jpeg") entry.format = "JPEG";
    else if (suffix == "bmp") entry.format = "BMP";
    else if (suffix == "tiff" || suffix == "tif") entry.format = "TIFF";
    else if (suffix == "webp") entry.format = "WEBP";
}

void ImageManager::removeImage(const QString& filePath)
{
    m_images.remove(filePath);
    emit imageRemoved(filePath);
}

void ImageManager::clearImages()
{
    m_images.clear();
    emit imagesCleared();
}

QPixmap ImageManager::loadPixmap(const QString& filePath)
{
    if (m_images.contains(filePath))
    {
        QPixmap pix(filePath);
        if (!pix.isNull())
        {
            return pix;
        }
    }

    return QPixmap();
}

QPixmap ImageManager::thumbnail(const QString& filePath, int size)
{
    if (m_images.contains(filePath))
    {
        return m_images[filePath].icon.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QPixmap pix(filePath);
    if (!pix.isNull())
    {
        return pix.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return QPixmap();
}

QList<ImageEntry> ImageManager::images() const
{
    return m_images.values();
}

ImageEntry ImageManager::imageInfo(const QString& filePath) const
{
    return m_images.value(filePath);
}

int ImageManager::imageCount() const
{
    return m_images.size();
}

QStringList ImageManager::allPaths() const
{
    return m_images.keys();
}

void ImageManager::setSelected(const QString& filePath, bool selected)
{
    if (m_images.contains(filePath) && m_images[filePath].isSelected != selected)
    {
        m_images[filePath].isSelected = selected;
        emit selectionChanged();
    }
}

bool ImageManager::isSelected(const QString& filePath) const
{
    if (m_images.contains(filePath))
    {
        return m_images[filePath].isSelected;
    }
    return false;
}

void ImageManager::setAllSelected(bool selected)
{
    bool changed = false;
    for (auto& entry : m_images)
    {
        if (entry.isSelected != selected)
        {
            entry.isSelected = selected;
            changed = true;
        }
    }
    if (changed)
    {
        emit selectionChanged();
    }
}

QStringList ImageManager::selectedPaths() const
{
    QStringList paths;
    for (auto it = m_images.begin(); it != m_images.end(); ++it)
    {
        if (it.value().isSelected)
        {
            paths.append(it.key());
        }
    }
    return paths;
}

int ImageManager::selectedCount() const
{
    int count = 0;
    for (auto& entry : m_images)
    {
        if (entry.isSelected)
        {
            ++count;
        }
    }
    return count;
}

void ImageManager::toggleSelection(const QString& filePath)
{
    if (m_images.contains(filePath))
    {
        m_images[filePath].isSelected = !m_images[filePath].isSelected;
        emit selectionChanged();
    }
}

void ImageManager::setLabel(const QString& filePath, const QString& label)
{
    if (m_images.contains(filePath))
    {
        m_images[filePath].label = label;
        m_images[filePath].isAnnotated = !label.isEmpty();
        emit labelChanged(filePath);
    }
}

QString ImageManager::getLabel(const QString& filePath) const
{
    if (m_images.contains(filePath))
    {
        return m_images[filePath].label;
    }
    return QString();
}

bool ImageManager::hasLabel(const QString& filePath) const
{
    if (m_images.contains(filePath))
    {
        return m_images[filePath].isAnnotated && !m_images[filePath].label.isEmpty();
    }
    return false;
}

void ImageManager::clearLabel(const QString& filePath)
{
    setLabel(filePath, QString());
}

QList<ImageEntrySnapshot> ImageManager::toSnapshot() const
{
    QList<ImageEntrySnapshot> snapshots;
    for (auto it = m_images.begin(); it != m_images.end(); ++it) {
        const ImageEntry& entry = it.value();
        ImageEntrySnapshot snap;
        snap.filePath = entry.filePath;
        snap.originalPath = entry.filePath;  // 引用模式时 originalPath 与 filePath 相同
        snap.fileName = entry.fileName;
        snap.width = entry.width;
        snap.height = entry.height;
        snap.channels = entry.channels;
        snap.format = entry.format;
        snap.fileSize = entry.fileSize;
        snap.isAnnotated = entry.isAnnotated;
        snap.label = entry.label;
        snap.isSelected = entry.isSelected;
        snapshots.append(snap);
    }
    return snapshots;
}

void ImageManager::importFromSnapshot(const QList<ImageEntrySnapshot>& snapshots)
{
    // 清空当前图像列表
    clearImages();

    // 按快照列表重建 ImageEntry
    for (const auto& snap : snapshots) {
        ImageEntry entry;
        entry.filePath = snap.filePath;
        entry.fileName = snap.fileName;
        entry.label = snap.label;
        entry.isAnnotated = snap.isAnnotated;
        entry.isSelected = snap.isSelected;
        entry.width = snap.width;
        entry.height = snap.height;
        entry.channels = snap.channels;
        entry.format = snap.format;
        entry.fileSize = snap.fileSize;

        // 优化缩略图生成：使用 QImageReader 直接加载为缩略图大小
        // 之前使用 QPixmap(snap.filePath) 加载原始大图（可能几MB），
        // 然后 scaled 到 128x128，内存峰值极高（507 张大图会耗尽内存）。
        // 现在 QImageReader::setScaledSize 让 Qt 在解码时直接缩放，避免加载完整原图。
        QImageReader reader(snap.filePath);
        if (reader.canRead()) {
            QSize origSize = reader.size();
            if (origSize.isValid()) {
                // 计算等比缩放后的目标尺寸（最大 128x128）
                QSize targetSize = origSize.scaled(128, 128, Qt::KeepAspectRatio);
                reader.setScaledSize(targetSize);
            }
            QImage thumb = reader.read();
            if (!thumb.isNull()) {
                entry.icon = QPixmap::fromImage(thumb);
            }
        }

        m_images[snap.filePath] = entry;
    }

    // 发出信号通知 UI 刷新
    if (!snapshots.isEmpty()) {
        emit imagesImported(snapshots.size());
    }
}