#ifndef MODEL_SOURCE_MANAGER_H
#define MODEL_SOURCE_MANAGER_H

/**
 * @file ModelSourceManager.h
 * @brief 模型源路径管理器（v2.0 阶段七 Task 16-17）
 *
 * 设计目标：
 * - 统一管理多个模型源路径（LM Studio 预设路径 / 现有 ONNX 路径 / 训练产物路径）
 * - 单例模式，线程安全（QMutex 互斥）
 * - 加载/保存 JSON 配置（config/model_sources.json）
 * - 递归扫描各源路径发现可用模型（.gguf/.onnx/.pt/.safetensors 文件 + 含模型文件的目录）
 * - QFileSystemWatcher 监控已启用路径，模型增删自动刷新（防抖 500ms）
 * - 路径不存在时标记"未挂载"，定时周期检查，恢复后自动重新监控
 * - 信号通知 UI 刷新（modelListChanged / sourceStatusChanged）
 *
 * 与 ZeroShotPanel 的关系：
 * - ZeroShotPanel 不直接依赖本类（保持 ZeroShotKit CMake 独立性）
 * - 由 ZeroShotDetectView 桥接：本类 → QVariantList → ZeroShotPanel::setModelEntries
 *
 * 预设路径约束（AGENTS.md 存储要求）：
 * - LM Studio 默认路径 F:\models\.lmstudio\models（在 F 盘，非 C 盘）
 * - 与现有路径 D:\models 并存，互不冲突
 */

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QVariantMap>
#include <QVariantList>
#include <QMutex>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QSet>

namespace QDV {

/// 模型来源类型字符串常量（与 JSON 配置 sourceType 字段对应）
/// lmstudio: LM Studio 下载的本地大模型
/// local:    现有 ONNX/零样本模型
/// trained:  训练 YOLO 产物
struct ModelSourceTypes {
    static constexpr const char* LMSTUDIO = "lmstudio";
    static constexpr const char* LOCAL    = "local";
    static constexpr const char* TRAINED  = "trained";
};

/// 单个模型条目：扫描后发现的一个可用模型
struct ModelEntry {
    QString     name;          ///< 模型显示名（文件名或目录名）
    QString     path;          ///< 模型完整路径（文件路径或目录路径）
    QString     sourceType;    ///< 来源类型：lmstudio/local/trained
    qint64      size = 0;      ///< 模型大小（字节；目录为 0）
    QDateTime   modifiedTime;  ///< 最后修改时间
    QString     parentPath;    ///< 所属源根路径（用于分组显示）

    /// 序列化为 QVariantMap（UI/JSON 友好）
    QVariantMap toMap() const;
    /// 从 QVariantMap 反序列化
    static ModelEntry fromMap(const QVariantMap& m);
};

/// 单个模型源路径配置项
struct ModelSource {
    QString     path;          ///< 源根路径（如 F:\models\.lmstudio\models）
    QString     sourceType;    ///< 来源类型：lmstudio/local/trained
    bool        enabled = true;///< 是否启用扫描与监控
    QString     description;   ///< 人类可读说明

    /// 序列化为 QVariantMap
    QVariantMap toMap() const;
    /// 从 QVariantMap 反序列化
    static ModelSource fromMap(const QVariantMap& m);
};

/// 源路径挂载状态
struct ModelSourceStatus {
    static constexpr const char* MOUNTED   = "mounted";     ///< 已挂载（路径存在且有模型）
    static constexpr const char* EMPTY     = "empty";       ///< 已挂载但无模型
    static constexpr const char* UNMOUNTED = "unmounted";   ///< 未挂载（路径不存在）
};

/**
 * @brief 模型源管理器（单例，线程安全）
 *
 * 加载策略：
 * - 默认从 <applicationDirPath>/config/model_sources.json 加载
 * - 文件不存在/解析失败 → 回退到内置默认（LM Studio 预设 + D:\models + D:\QDV\trained）
 *
 * 扫描策略：
 * - 递归扫描已启用路径，识别模型文件：.gguf/.onnx/.pt/.safetensors/.bin/.pth
 * - LM Studio 目录结构通常为 publisher/model-name/model-file.gguf，递归扫描
 * - 含模型文件的目录（如 D:\QDV\trained\rust_v1\ 含 model.onnx）也作为模型条目
 *
 * 监控策略：
 * - QFileSystemWatcher 监控已启用且存在的路径（含子目录）
 * - 文件/目录变化触发 rescan()，防抖 500ms（多次变化合并为一次扫描）
 * - 路径不存在时停止监控，标记"未挂载"
 * - 定时器周期检查未挂载路径（默认 30s），恢复后自动重新监控
 */
class ModelSourceManager : public QObject {
    Q_OBJECT

public:
    /// 单例获取
    static ModelSourceManager* instance();

    /// 防抖延迟（毫秒）：短时间内多次文件系统变化合并为一次扫描
    static constexpr int DEBOUNCE_MS = 500;

    /// 未挂载路径检查周期（毫秒）
    static constexpr int UNMOUNTED_CHECK_MS = 30000;

    // ------------------------------------------------------------------
    // 加载 / 保存
    // ------------------------------------------------------------------

    /// 默认配置路径：<applicationDirPath>/config/model_sources.json
    static QString defaultConfigPath();

    /// 从 JSON 文件加载（成功返回 true）。失败时回退到 builtinDefaults()
    /// @param path  绝对或相对路径；为空时使用 defaultConfigPath()
    /// @param outError  失败时写入原因
    bool loadFromJson(const QString& path = QString(), QString* outError = nullptr);

    /// 保存到 JSON 文件（成功返回 true）
    /// @param path  为空时使用 defaultConfigPath()
    bool saveToJson(const QString& path = QString());

    // ------------------------------------------------------------------
    // 源路径管理
    // ------------------------------------------------------------------

    /// 内置默认源路径（LM Studio 预设 + D:\models + D:\QDV\trained）
    static QList<ModelSource> builtinDefaults();

    /// 当前所有源路径（含未启用）
    QList<ModelSource> sources() const;

    /// 当前所有已启用的源路径
    QList<ModelSource> enabledSources() const;

    /// 添加源路径（path 重复时更新 sourceType/description）
    void addSource(const QString& path, const QString& sourceType,
                   const QString& description = QString());

    /// 移除源路径（同时移除该路径下的所有模型条目）
    void removeSource(const QString& path);

    /// 启用/禁用源路径
    void setSourceEnabled(const QString& path, bool enabled);

    // ------------------------------------------------------------------
    // 模型清单查询
    // ------------------------------------------------------------------

    /// 所有源合并后的模型清单（仅已启用源）
    QList<ModelEntry> allModels() const;

    /// 按来源类型筛选模型清单
    QList<ModelEntry> modelsBySource(const QString& sourceType) const;

    /// 当前清单中新增的模型路径集合（与上次清单对比；首次扫描全部算新增）
    QSet<QString> newModelPaths() const { return m_newModelPaths; }

    /// 模型清单作为 QVariantList（UI 友好，含 isNew 标记）
    QVariantList allModelsAsVariant() const;

    /// LM Studio 预设路径是否可用（存在且至少有 1 个模型）
    bool isLmStudioAvailable() const;

    // ------------------------------------------------------------------
    // 扫描
    // ------------------------------------------------------------------

    /// 重新扫描所有已启用路径，刷新模型清单
    /// @return 本次扫描发现的模型数量
    int rescan();

    // ------------------------------------------------------------------
    // 文件系统监控
    // ------------------------------------------------------------------

    /// 启动文件系统监控（监控所有已启用且存在的路径）
    void startWatching();

    /// 停止文件系统监控
    void stopWatching();

    /// 监控是否在运行
    bool isWatching() const;

    /// 当前各源路径状态（path → status: mounted/empty/unmounted）
    QMap<QString, QString> sourceStatuses() const { return m_sourceStatuses; }

    /// 当前已加载的配置文件路径
    QString loadedPath() const { return m_loadedPath; }

signals:
    /// 模型清单变化（扫描完成后发射）
    /// @param models  最新模型清单（QVariantList 形式）
    void modelListChanged(const QVariantList& models);

    /// 源路径状态变化（mounted/unmounted/empty）
    /// @param path    源路径
    /// @param status  新状态
    void sourceStatusChanged(const QString& path, const QString& status);

    /// 监控到新增模型（与上次清单对比）
    /// @param newPaths  新增模型路径集合
    void newModelsDetected(const QSet<QString>& newPaths);

private:
    ModelSourceManager(QObject* parent = nullptr);
    ~ModelSourceManager() override = default;

    // 线程安全：所有读写加锁
    mutable QMutex m_mutex;

    QList<ModelSource>   m_sources;             ///< 源路径配置
    QList<ModelEntry>    m_models;              ///< 当前模型清单
    QSet<QString>        m_newModelPaths;       ///< 本次扫描新增的模型路径
    QSet<QString>        m_lastModelPaths;      ///< 上次扫描的模型路径集合（用于对比新增）
    bool                 m_hasLastSnapshot = false; ///< 是否已有上次快照（首次扫描全部算新增）

    QMap<QString, QString> m_sourceStatuses;    ///< 各源路径当前状态

    QString m_loadedPath;                       ///< 已加载的配置文件路径

    // 文件系统监控
    QFileSystemWatcher* m_watcher = nullptr;    ///< 文件系统监控器
    QTimer*             m_debounceTimer = nullptr;  ///< 防抖定时器
    QTimer*             m_unmountedCheckTimer = nullptr;  ///< 未挂载路径周期检查
    bool                m_watching = false;     ///< 监控是否运行

    // 内部辅助
    void setupWatcher();                        ///< 重新建立 watcher 监控路径
    void scanSource(const ModelSource& source, QList<ModelEntry>& outEntries);  ///< 扫描单个源
    void updateSourceStatus(const QString& path, const QString& status);  ///< 更新状态并发信号
    void scheduleDebouncedRescan();             ///< 触发防抖扫描
    bool isModelFile(const QString& fileName) const;  ///< 判断是否为模型文件
    QList<ModelEntry> scanDirectory(const QString& rootPath, const QString& sourceType,
                                    const QString& sourceRoot) const;  ///< 递归扫描目录

    // 防抖扫描触发
    void onFileChanged(const QString& path);
    void onDirectoryChanged(const QString& path);
    void onDebounceTimeout();
    void onUnmountedCheckTimeout();

    static ModelSourceManager* s_instance;
    static QMutex              s_instanceMutex;
};

} // namespace QDV

#endif // MODEL_SOURCE_MANAGER_H
