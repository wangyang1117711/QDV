#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QPixmap>
#include <QMap>

struct ImageEntry {
    QString filePath;
    QString fileName;
    QPixmap icon;
    bool isAnnotated = false;
    QString label;
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

signals:
    void imagesImported(int count);
    void imageRemoved(const QString& filePath);
    void imagesCleared();

private:
    ImageManager(QObject* parent = nullptr);
    QMap<QString, ImageEntry> m_images;
    static ImageManager* s_instance;
};