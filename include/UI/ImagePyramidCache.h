#ifndef IMAGEPYRAMIDCACHE_H
#define IMAGEPYRAMIDCACHE_H

#include <QObject>
#include <QString>
#include <QCache>
#include <QMutex>
#include <QPixmap>
#include <QSize>
#include <QImage>
#include <opencv2/opencv.hpp>

/**
 * @brief 图像金字塔缓存
 *
 * 4 档缩放等级：
 * - [0.1, 0.5): INTER_AREA 预渲染
 * - [0.5, 1.0): INTER_AREA 预渲染
 * - [1.0, 2.0): INTER_LINEAR 预渲染
 * - [2.0, 10.0): INTER_NEAREST 预渲染
 *
 * 同档缩放直接复用预渲染缓存图，跨档才重建
 */

class ImagePyramidCache : public QObject {
    Q_OBJECT

public:
    explicit ImagePyramidCache(QObject* parent = nullptr, int capacity = 20);
    ~ImagePyramidCache();

    /**
     * @brief 获取缩放后的图像（带缓存）
     * @param sourceImage 源图像（cv::Mat）
     * @param scale 当前缩放比例
     * @return 缩放后的 QImage
     */
    QImage getScaledImage(const cv::Mat& sourceImage, double scale);

    /**
     * @brief 获取缩放后的 QPixmap（带缓存）
     */
    QPixmap getScaledPixmap(const cv::Mat& sourceImage, double scale);

    /**
     * @brief 清空缓存
     */
    void clear();

    /**
     * @brief 获取缓存条目数
     */
    int size() const;

    /**
     * @brief 设置源图像（重置缓存）
     */
    void setSourceImage(const cv::Mat& image);

    /**
     * @brief 获取档位编号（0-3）
     */
    static int getLevel(double scale);

private:
    struct CacheEntry {
        QImage image;
        double scale;
        int level;
    };

    QCache<int, CacheEntry> m_cache;  // Key: 档位编号 0-3
    mutable QMutex m_mutex;
    cv::Mat m_sourceImage;

    // 按档位预渲染
    QImage renderAtLevel(int level) const;

    // cv::Mat → QImage 转换
    static QImage matToQImage(const cv::Mat& mat);

    // 档位边界
    static constexpr double LEVEL_BOUNDS[5] = {0.1, 0.5, 1.0, 2.0, 10.0};
};

#endif // IMAGEPYRAMIDCACHE_H
