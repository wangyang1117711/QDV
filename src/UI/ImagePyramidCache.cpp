#include "UI/ImagePyramidCache.h"
#include "Core/Logger.h"
#include <QPainter>

// 档位边界：[0.1, 0.5) | [0.5, 1.0) | [1.0, 2.0) | [2.0, 10.0)
constexpr double ImagePyramidCache::LEVEL_BOUNDS[5];

ImagePyramidCache::ImagePyramidCache(QObject* parent, int capacity)
    : QObject(parent), m_cache(capacity)
{
}

ImagePyramidCache::~ImagePyramidCache() = default;

void ImagePyramidCache::setSourceImage(const cv::Mat& image) {
    QMutexLocker locker(&m_mutex);
    m_sourceImage = image.clone();
    m_cache.clear();  // 源图像变化，清空缓存
}

int ImagePyramidCache::getLevel(double scale) {
    for (int i = 0; i < 4; ++i) {
        if (scale >= LEVEL_BOUNDS[i] && scale < LEVEL_BOUNDS[i + 1]) {
            return i;
        }
    }
    return 3;  // 默认最高档
}

QImage ImagePyramidCache::matToQImage(const cv::Mat& mat) {
    if (mat.empty()) return QImage();

    if (mat.channels() == 3) {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
                      QImage::Format_RGB888).copy();
    } else if (mat.channels() == 1) {
        return QImage(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step),
                      QImage::Format_Grayscale8).copy();
    } else if (mat.channels() == 4) {
        cv::Mat rgba;
        cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
        return QImage(rgba.data, rgba.cols, rgba.rows, static_cast<int>(rgba.step),
                      QImage::Format_RGBA8888).copy();
    }
    return QImage();
}

QImage ImagePyramidCache::renderAtLevel(int level) const {
    if (m_sourceImage.empty()) return QImage();

    // 每档使用代表性缩放比例
    double representativeScale;
    cv::InterpolationFlags interp;

    switch (level) {
        case 0:  // [0.1, 0.5) - 大幅缩小
            representativeScale = 0.25;
            interp = cv::INTER_AREA;
            break;
        case 1:  // [0.5, 1.0) - 轻微缩小
            representativeScale = 0.75;
            interp = cv::INTER_AREA;
            break;
        case 2:  // [1.0, 2.0) - 轻微放大
            representativeScale = 1.5;
            interp = cv::INTER_LINEAR;
            break;
        case 3:  // [2.0, 10.0) - 大幅放大
            representativeScale = 3.0;
            interp = cv::INTER_NEAREST;
            break;
        default:
            return QImage();
    }

    cv::Size newSize(
        static_cast<int>(m_sourceImage.cols * representativeScale),
        static_cast<int>(m_sourceImage.rows * representativeScale));

    if (newSize.width < 1) newSize.width = 1;
    if (newSize.height < 1) newSize.height = 1;

    cv::Mat scaled;
    cv::resize(m_sourceImage, scaled, newSize, 0, 0, interp);

    return matToQImage(scaled);
}

QImage ImagePyramidCache::getScaledImage(const cv::Mat& sourceImage, double scale) {
    QMutexLocker locker(&m_mutex);

    // 如果源图像变化，更新
    if (m_sourceImage.data != sourceImage.data) {
        m_sourceImage = sourceImage.clone();
        m_cache.clear();
    }

    int level = getLevel(scale);

    // 查询缓存
    CacheEntry* cached = m_cache.object(level);
    if (cached) {
        return cached->image;
    }

    // 渲染并缓存
    QImage rendered = renderAtLevel(level);
    if (!rendered.isNull()) {
        CacheEntry* entry = new CacheEntry();
        entry->image = rendered;
        entry->scale = scale;
        entry->level = level;
        m_cache.insert(level, entry);
    }

    return rendered;
}

QPixmap ImagePyramidCache::getScaledPixmap(const cv::Mat& sourceImage, double scale) {
    QImage img = getScaledImage(sourceImage, scale);
    return QPixmap::fromImage(img);
}

void ImagePyramidCache::clear() {
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
    m_sourceImage.release();
}

int ImagePyramidCache::size() const {
    QMutexLocker locker(&m_mutex);
    return m_cache.size();
}
