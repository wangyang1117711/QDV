#ifndef INFERENCE_CACHE_H
#define INFERENCE_CACHE_H

// ============================================================================
// InferenceCache — 推理结果缓存（spec v2 阶段六 Task 15.2）
//
// 设计目标：
// - 跳过相同输入的重复推理（同图 + 同提示词 + 同 ROI + 同模型）
// - 缓存键：图像内容 SHA-256 + 提示词 + ROI 哈希 + 模型类型 + 模型路径
// - 缓存值：ToolResult（含 data/ports/score/elapsedMs/overlayImage）
// - 用 QCache 实现，限制最大条数（默认 100），LRU 自动淘汰
// - 线程安全（QMutex 互斥）
//
// 集成点：
// - ZeroShotDetectTool::execute：每次推理前查缓存，未命中再推理，推理后写缓存
// - 也可在 ZeroShotDetectView::onInferenceRequested 中集成（调试模式）
//
// 哈希策略：
// - 图像内容：cv::imencode(".jpg") → QCryptographicHash::Sha256
//   （JPG 编码比 PNG 快，且对缓存键稳定性足够）
// - ROI：QVariantMap 序列化为 JSON 字符串后哈希
// - 提示词 + 模型类型 + 模型路径：直接拼接进缓存键
// ============================================================================

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QJsonObject>
#include <QCache>
#include <QMutex>

#include <opencv2/core/mat.hpp>

// ToolResult 完整定义（QCache<QString, ToolResult> 需要完整类型 + 不抛异常析构）
#include "Core/VisionTool.h"

namespace QDV {

class InferenceCache : public QObject {
    Q_OBJECT
public:
    /// 单例获取
    static InferenceCache* instance();

    /// 默认最大缓存条数
    static constexpr int DEFAULT_MAX_ENTRIES = 100;

    // ------------------------------------------------------------------
    // 配置
    // ------------------------------------------------------------------

    /// 设置最大缓存条数（清空现有缓存）
    void setMaxEntries(int max);

    /// 当前最大缓存条数
    int maxEntries() const { return m_maxEntries; }

    /// 清空缓存
    void clear();

    /// 当前缓存条数
    int size() const;

    // ------------------------------------------------------------------
    // 查询 / 写入
    // ------------------------------------------------------------------

    /// 查询缓存
    /// @param image       输入图像（用于计算内容哈希）
    /// @param textPrompts 提示词串（如 "scratch . dent"）
    /// @param roi         ROI（QVariantMap，与 VisionTool::roi() 一致）
    /// @param modelType   模型类型字符串（如 "AnomalyCLIP"）
    /// @param modelPath   模型路径
    /// @param outResult   命中时写入缓存结果
    /// @return 命中返回 true，未命中返回 false
    bool lookup(const cv::Mat& image, const QString& textPrompts,
                const QVariantMap& roi, const QString& modelType,
                const QString& modelPath, ToolResult& outResult);

    /// 写入缓存
    /// @param image       输入图像
    /// @param textPrompts 提示词串
    /// @param roi         ROI
    /// @param modelType   模型类型字符串
    /// @param modelPath   模型路径
    /// @param result      推理结果（缓存副本）
    void insert(const cv::Mat& image, const QString& textPrompts,
                const QVariantMap& roi, const QString& modelType,
                const QString& modelPath, const ToolResult& result);

    // ------------------------------------------------------------------
    // 统计
    // ------------------------------------------------------------------

    /// 累计命中次数（自实例创建起）
    qint64 hitCount() const { return m_hitCount; }

    /// 累计未命中次数（自实例创建起）
    qint64 missCount() const { return m_missCount; }

    /// 重置统计计数器（不影响缓存内容）
    void resetStats();

signals:
    /// 缓存命中（调试/监控用）
    void cacheHit(const QString& cacheKey);
    /// 缓存未命中（调试/监控用）
    void cacheMiss(const QString& cacheKey);
    /// 缓存条目被淘汰（超过 maxEntries 时）
    void cacheEvicted();

private:
    InferenceCache(QObject* parent = nullptr);

    // 构造缓存键：imageHash + "|" + prompts + "|" + roiHash + "|" + modelType + "|" + modelPath
    static QString buildKey(const QString& imageHash, const QString& textPrompts,
                            const QString& roiHash, const QString& modelType,
                            const QString& modelPath);

    /// 计算图像内容 SHA-256（cv::imencode + QCryptographicHash）
    static QString imageToHash(const cv::Mat& image);

    /// 计算 ROI 哈希（QVariantMap → JSON 字符串 → SHA-256）
    static QString roiToHash(const QVariantMap& roi);

    mutable QMutex m_mutex;
    QCache<QString, ToolResult> m_cache;  // QCache 自动 LRU 淘汰
    int m_maxEntries = DEFAULT_MAX_ENTRIES;

    // 统计计数器（原子性由 m_mutex 保证）
    qint64 m_hitCount  = 0;
    qint64 m_missCount = 0;

    static InferenceCache* s_instance;
    static QMutex          s_instanceMutex;
};

} // namespace QDV

#endif // INFERENCE_CACHE_H
