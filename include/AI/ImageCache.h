#ifndef IMAGECACHE_H
#define IMAGECACHE_H

#include <QObject>
#include <QString>
#include <QCache>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <opencv2/opencv.hpp>

/**
 * @brief 图像缓存
 *
 * 三层缓存的第三层：缓存解码后的图像（cv::Mat）
 * Key: filePath|lastModified
 * 容量: 100 条
 * 失效条件: 文件修改时间变化
 * 加载方式: QFile + cv::imdecode（兼容中文路径）
 */
class ImageCache : public QObject {
    Q_OBJECT

public:
    explicit ImageCache(QObject* parent = nullptr, int capacity = 100);
    ~ImageCache();

    /**
     * @brief 加载图像（优先从缓存读取）
     * @param filePath 图像文件路径
     * @return cv::Mat 图像（失败返回空 Mat）
     */
    cv::Mat load(const QString& filePath);

    /**
     * @brief 检查缓存中是否有该图像
     * @param filePath 图像文件路径
     * @return true=缓存中存在且有效
     */
    bool contains(const QString& filePath) const;

    /**
     * @brief 清空缓存
     */
    void clear();

    /**
     * @brief 获取缓存条目数
     */
    int size() const;

    /**
     * @brief 获取缓存容量
     */
    int capacity() const;

    /**
     * @brief 设置缓存容量
     */
    void setCapacity(int capacity);

private:
    struct CacheEntry {
        cv::Mat image;
        qint64 mtime;
    };

    QCache<QString, CacheEntry> m_cache;
    mutable QMutex m_mutex;

    /**
     * @brief 从文件加载图像（QFile + cv::imdecode 兼容中文路径）
     */
    static cv::Mat loadFromFile(const QString& filePath);
};

#endif // IMAGECACHE_H
