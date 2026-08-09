#include "AI/ImageCache.h"
#include "Core/Logger.h"
#include <QFile>
#include <QFileInfo>

using namespace QDV;

ImageCache::ImageCache(QObject* parent, int capacity)
    : QObject(parent), m_cache(capacity)
{
}

ImageCache::~ImageCache() {
    clear();
}

cv::Mat ImageCache::loadFromFile(const QString& filePath) {
    // 使用 QFile + cv::imdecode 兼容中文路径
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        Logger::warn("ImageCache: cannot open file: " + filePath);
        return cv::Mat();
    }
    QByteArray data = file.readAll();
    file.close();

    // 内存解码
    cv::Mat image = cv::imdecode(cv::Mat(1, data.size(), CV_8UC1, data.data()), cv::IMREAD_COLOR);
    if (image.empty()) {
        Logger::warn("ImageCache: imdecode failed: " + filePath);
    }
    return image;
}

cv::Mat ImageCache::load(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        Logger::warn("ImageCache: file not found: " + filePath);
        return cv::Mat();
    }

    qint64 currentMtime = fileInfo.lastModified().toMSecsSinceEpoch();
    QString key = filePath;  // Key 就是文件路径，mtime 在 CacheEntry 中比较

    QMutexLocker locker(&m_mutex);

    // 查询缓存
    CacheEntry* cached = m_cache.object(key);
    if (cached && cached->mtime == currentMtime) {
        Logger::debug("ImageCache: cache hit: " + filePath);
        return cached->image.clone();  // 返回副本，避免外部修改
    }

    // 缓存未命中或 mtime 变化，重新加载
    locker.unlock();
    cv::Mat image = loadFromFile(filePath);
    locker.relock();

    if (!image.empty()) {
        CacheEntry* entry = new CacheEntry();
        entry->image = image;
        entry->mtime = currentMtime;
        m_cache.insert(key, entry);
        Logger::debug(QString("ImageCache: loaded and cached: %1 (size=%2/%3)")
                      .arg(filePath).arg(m_cache.size()).arg(m_cache.maxCost()));
    }

    return image;
}

bool ImageCache::contains(const QString& filePath) const {
    QMutexLocker locker(&m_mutex);
    return m_cache.contains(filePath);
}

void ImageCache::clear() {
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
}

int ImageCache::size() const {
    QMutexLocker locker(&m_mutex);
    return m_cache.size();
}

int ImageCache::capacity() const {
    QMutexLocker locker(&m_mutex);
    return m_cache.maxCost();
}

void ImageCache::setCapacity(int capacity) {
    QMutexLocker locker(&m_mutex);
    m_cache.setMaxCost(capacity);
}
