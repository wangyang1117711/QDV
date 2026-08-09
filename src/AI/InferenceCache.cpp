#include "AI/InferenceCache.h"
#include "Core/Logger.h"
#include <QCryptographicHash>
#include <QDateTime>

using namespace QDV;

InferenceCache::InferenceCache(QObject* parent, int capacity)
    : QObject(parent), m_cache(capacity)
{
}

InferenceCache::~InferenceCache() {
    clear();
}

QString InferenceCache::computeImageFingerprint(const cv::Mat& image) {
    if (image.empty()) return "empty";

    // 缩放到 16x16 灰度
    cv::Mat small, gray;
    cv::resize(image, small, cv::Size(16, 16));
    if (small.channels() == 3) {
        cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = small;
    }

    // 计算 mean 和 sum
    cv::Scalar mean, stdDev;
    cv::meanStdDev(gray, mean, stdDev);
    double sum = cv::sum(gray)[0];

    // 生成指纹字符串
    return QString("m%1_s%2").arg(mean[0], 0, 'f', 2).arg(sum, 0, 'f', 0);
}

QString InferenceCache::generateKey(const QString& modelPath,
                                     qint64 modelMtime,
                                     const QSize& inputSize,
                                     double confThreshold,
                                     const cv::Mat& image) {
    QString fingerprint = computeImageFingerprint(image);
    return QString("%1|%2|%3x%4|%5|%6")
        .arg(modelPath)
        .arg(modelMtime)
        .arg(inputSize.width())
        .arg(inputSize.height())
        .arg(confThreshold, 0, 'f', 4)
        .arg(fingerprint);
}

bool InferenceCache::lookup(const QString& key, QJsonObject& result) {
    QMutexLocker locker(&m_mutex);
    QJsonObject* cached = m_cache.object(key);
    if (cached) {
        result = *cached;
        Logger::debug("InferenceCache: cache hit for " + key);
        return true;
    }
    return false;
}

void InferenceCache::insert(const QString& key, const QJsonObject& result) {
    QMutexLocker locker(&m_mutex);
    m_cache.insert(key, new QJsonObject(result));
    Logger::debug(QString("InferenceCache: inserted, size=%1/%2")
                  .arg(m_cache.size()).arg(m_cache.maxCost()));
}

void InferenceCache::clear() {
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
}

int InferenceCache::size() const {
    QMutexLocker locker(&m_mutex);
    return m_cache.size();
}

int InferenceCache::capacity() const {
    QMutexLocker locker(&m_mutex);
    return m_cache.maxCost();
}

void InferenceCache::setCapacity(int capacity) {
    QMutexLocker locker(&m_mutex);
    m_cache.setMaxCost(capacity);
}
