#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QPixmap>
#include <QMap>
#include <QSet>
#include <QFile>
#include <QByteArray>
#include "TrainingInference/TrainingProject.h"

struct ImageEntry {
    QString filePath;
    QString fileName;
    QPixmap icon;
    bool isAnnotated = false;
    QString label;
    bool isSelected = false;
    int width = 0;            // 图像宽度(像素)
    int height = 0;           // 图像高度(像素)
    int channels = 0;         // 通道数(1灰度/3彩色/4带透明)
    QString format;           // 格式("PNG"/"JPEG"/"BMP"/"TIFF"/"WEBP")
    qint64 fileSize = 0;      // 文件大小(字节)
};

class ImageManager : public QObject {
    Q_OBJECT

public:
    static ImageManager* instance();

    QStringList importImages(const QStringList& filePaths);
    void removeImage(const QString& filePath);
    void clearImages();
    QPixmap loadPixmap(const QString& filePath);
    QPixmap thumbnail(const QString& filePath, int size = 128);
    QList<ImageEntry> images() const;
    ImageEntry imageInfo(const QString& filePath) const;
    int imageCount() const;
    QStringList allPaths() const;

    // ===== 项目保存/加载快照接口 =====
    QList<ImageEntrySnapshot> toSnapshot() const;
    void importFromSnapshot(const QList<ImageEntrySnapshot>& snapshots);

    // 选择相关功能
    void setSelected(const QString& filePath, bool selected);
    bool isSelected(const QString& filePath) const;
    void setAllSelected(bool selected);
    QStringList selectedPaths() const;
    int selectedCount() const;
    void toggleSelection(const QString& filePath);

    // 标签相关功能
    void setLabel(const QString& filePath, const QString& label);
    QString getLabel(const QString& filePath) const;
    bool hasLabel(const QString& filePath) const;
    void clearLabel(const QString& filePath);

signals:
    void imagesImported(int count);
    void imageRemoved(const QString& filePath);
    void imagesCleared();
    void selectionChanged();
    void labelChanged(const QString& filePath);

private:
    ImageManager(QObject* parent = nullptr);
    QMap<QString, ImageEntry> m_images;
    static ImageManager* s_instance;

    void extractMetadata(ImageEntry& entry);
};