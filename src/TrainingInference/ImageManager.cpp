#include "TrainingInference/ImageManager.h"
#include "Core/Logger.h"
#include <QPixmap>
#include <QFileInfo>
#include <QDir>

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

        m_images[filePath] = entry;
        imported.append(filePath);
    }

    if (!imported.isEmpty())
    {
        emit imagesImported(imported.size());
    }

    return imported;
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