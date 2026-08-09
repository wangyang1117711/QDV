// ============================================================================
// InferenceCache — 推理结果缓存实现（spec v2 阶段六 Task 15.2）
// ============================================================================

#include "Core/InferenceCache.h"
#include "Core/VisionTool.h"      // ToolResult 完整定义
#include "Core/Logger.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>

#include <opencv2/imgcodecs.hpp>

namespace QDV {

// 静态成员初始化
InferenceCache* InferenceCache::s_instance = nullptr;
QMutex          InferenceCache::s_instanceMutex;

// ----------------------------------------------------------------------------
// 单例获取
// ----------------------------------------------------------------------------
InferenceCache* InferenceCache::instance() {
    QMutexLocker locker(&s_instanceMutex);
    if (!s_instance) {
        s_instance = new InferenceCache();
    }
    return s_instance;
}

// ----------------------------------------------------------------------------
// 构造
// ----------------------------------------------------------------------------
InferenceCache::InferenceCache(QObject* parent) : QObject(parent) {
    m_cache.setMaxCost(DEFAULT_MAX_ENTRIES);
}

// ----------------------------------------------------------------------------
// 配置：最大缓存条数
// ----------------------------------------------------------------------------
void InferenceCache::setMaxEntries(int max) {
    if (max <= 0) max = DEFAULT_MAX_ENTRIES;
    QMutexLocker locker(&m_mutex);
    m_maxEntries = max;
    m_cache.setMaxCost(max);
    // QCache::setMaxCost 会自动淘汰超限条目（无需手动 clear）
}

// ----------------------------------------------------------------------------
// 清空缓存
// ----------------------------------------------------------------------------
void InferenceCache::clear() {
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
}

// ----------------------------------------------------------------------------
// 当前缓存条数
// ----------------------------------------------------------------------------
int InferenceCache::size() const {
    QMutexLocker locker(&m_mutex);
    return m_cache.size();
}

// ----------------------------------------------------------------------------
// 查询缓存
// ----------------------------------------------------------------------------
bool InferenceCache::lookup(const cv::Mat& image, const QString& textPrompts,
                             const QVariantMap& roi, const QString& modelType,
                             const QString& modelPath, ToolResult& outResult) {
    // 计算缓存键各组成部分
    const QString imgHash  = imageToHash(image);
    const QString roiHash  = roiToHash(roi);
    const QString cacheKey = buildKey(imgHash, textPrompts, roiHash, modelType, modelPath);

    QMutexLocker locker(&m_mutex);
    const ToolResult* cached = m_cache.object(cacheKey);
    if (cached) {
        // 命中：拷贝出缓存结果
        outResult = *cached;
        ++m_hitCount;
        emit cacheHit(cacheKey);
        return true;
    }
    // 未命中
    ++m_missCount;
    emit cacheMiss(cacheKey);
    return false;
}

// ----------------------------------------------------------------------------
// 写入缓存
// ----------------------------------------------------------------------------
void InferenceCache::insert(const cv::Mat& image, const QString& textPrompts,
                             const QVariantMap& roi, const QString& modelType,
                             const QString& modelPath, const ToolResult& result) {
    // 仅缓存成功结果（失败结果无复用价值）
    if (!result.ok) return;

    const QString imgHash  = imageToHash(image);
    const QString roiHash  = roiToHash(roi);
    const QString cacheKey = buildKey(imgHash, textPrompts, roiHash, modelType, modelPath);

    QMutexLocker locker(&m_mutex);
    // QCache 接管 new 出的对象所有权，超限时自动 delete 最旧条目
    ToolResult* stored = new ToolResult(result);
    if (!m_cache.insert(cacheKey, stored, 1 /*cost*/)) {
        // insert 失败极少见（仅在 cost > maxCost 时），此时释放内存避免泄漏
        delete stored;
        Logger::warn(QStringLiteral("[InferenceCache] insert 失败: cacheKey=%1").arg(cacheKey));
    }
}

// ----------------------------------------------------------------------------
// 重置统计
// ----------------------------------------------------------------------------
void InferenceCache::resetStats() {
    QMutexLocker locker(&m_mutex);
    m_hitCount = 0;
    m_missCount = 0;
}

// ============================================================================
// 内部辅助：缓存键构造与哈希计算
// ============================================================================

QString InferenceCache::buildKey(const QString& imageHash, const QString& textPrompts,
                                  const QString& roiHash, const QString& modelType,
                                  const QString& modelPath) {
    // 用 "|" 分隔避免歧义（提示词/路径中不会出现 "|"）
    return imageHash + QStringLiteral("|")
         + textPrompts + QStringLiteral("|")
         + roiHash + QStringLiteral("|")
         + modelType + QStringLiteral("|")
         + modelPath;
}

// ----------------------------------------------------------------------------
// 图像内容 SHA-256
// cv::imencode(".jpg") 编码后取 SHA-256，比 PNG 快，对缓存键稳定性足够
// ----------------------------------------------------------------------------
QString InferenceCache::imageToHash(const cv::Mat& image) {
    if (image.empty()) return QStringLiteral("empty_image");

    std::vector<uchar> buf;
    // 用 JPG 编码（速度优先）；若编码失败回退到 PNG
    if (!cv::imencode(".jpg", image, buf)) {
        if (!cv::imencode(".png", image, buf)) {
            return QStringLiteral("encode_failed");
        }
    }
    const QByteArray ba(reinterpret_cast<const char*>(buf.data()),
                        static_cast<int>(buf.size()));
    const QByteArray hash = QCryptographicHash::hash(ba, QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

// ----------------------------------------------------------------------------
// ROI 哈希（QVariantMap → JSON 字符串 → SHA-256）
// ----------------------------------------------------------------------------
QString InferenceCache::roiToHash(const QVariantMap& roi) {
    if (roi.isEmpty()) return QStringLiteral("no_roi");

    // QVariantMap → QJsonObject → JSON 字符串
    const QJsonObject obj = QJsonObject::fromVariantMap(roi);
    const QJsonDocument doc(obj);
    const QByteArray jsonBytes = doc.toJson(QJsonDocument::Compact);
    const QByteArray hash = QCryptographicHash::hash(jsonBytes, QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

} // namespace QDV
