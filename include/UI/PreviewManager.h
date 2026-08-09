#ifndef QDV_PREVIEW_MANAGER_H
#define QDV_PREVIEW_MANAGER_H

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QDateTime>

// 前置声明：EditViewBridge 在全局命名空间（不在 QDV 内）
class EditViewBridge;

namespace QDV {

class ImageVariableManager;
class VariableManager;

/**
 * @brief 预览管理器（v2.6.0）
 *
 * 功能：
 * 1. 自动实时预览：参数/变量/连线变更 → 300ms 防抖 → 自动执行上游链 + 选中算子
 * 2. 结果写入 ImageVariableManager，QML 端预览窗口监听信号刷新
 * 3. 支持手动开关实时预览（默认开启）
 * 4. 支持指定预览节点（默认跟随当前选中节点）
 *
 * 设计要点：
 * - 防抖：300ms 内多次变更只执行一次，避免频繁执行卡顿
 * - 失败静默：执行失败不弹错误框，仅在日志记录（预览不阻塞编辑）
 * - 异步执行：算子在子线程执行，避免阻塞 UI 主线程（v2.6.1 修复卡死 bug）
 */
class PreviewManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool autoPreviewEnabled READ autoPreviewEnabled WRITE setAutoPreviewEnabled NOTIFY autoPreviewEnabledChanged)
    Q_PROPERTY(QString previewNodeId READ previewNodeId WRITE setPreviewNodeId NOTIFY previewNodeIdChanged)
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)

public:
    /// Snapshot 结构：记录单次预览执行的前后对比
    struct PreviewSnapshot {
        qint64 timestamp;       ///< 时间戳（ms since epoch）
        QString nodeId;         ///< 算子节点 ID
        QImage beforeImage;     ///< 执行前图像
        QImage afterImage;      ///< 执行后图像
        QJsonObject params;     ///< 预览参数
        QString beforeImagePath;///< before 图像临时文件路径（供 QML 显示）
        QString afterImagePath; ///< after 图像临时文件路径（供 QML 显示）
    };

    explicit PreviewManager(QObject* parent = nullptr);
    ~PreviewManager() override;

    /// 注入依赖（由 EditViewBridge 在构造后调用）
    void setBridge(::EditViewBridge* bridge);
    void setImageVariableManager(ImageVariableManager* ivm);
    void setVariableManager(VariableManager* vm);

    // === QML 属性 ===
    bool autoPreviewEnabled() const { return m_autoPreviewEnabled; }
    void setAutoPreviewEnabled(bool enabled);

    QString previewNodeId() const { return m_previewNodeId; }
    void setPreviewNodeId(const QString& nodeId);

    bool isRunning() const { return m_isRunning; }

    // === 触发预览 ===
    /// 请求预览（300ms 防抖；自动模式下由参数/变量变更触发）
    Q_INVOKABLE void requestPreview();

    /// 立即执行预览（跳过防抖）
    Q_INVOKABLE void previewNow();

    // === Snapshot 历史（LRU 50）===
    /// 获取历史快照列表（最新的在前）
    QList<PreviewSnapshot> snapshots() const;

    /// 清空历史快照
    Q_INVOKABLE void clearSnapshots();

    /// 获取最大历史数
    int maxSnapshots() const { return m_maxSnapshots; }
    void setMaxSnapshots(int max) { m_maxSnapshots = max; }

    /// 获取最新快照的 before 图像路径（供 QML 分屏视图显示）
    Q_INVOKABLE QString latestSnapshotBeforePath() const;
    /// 获取最新快照的 after 图像路径（供 QML 分屏视图显示）
    Q_INVOKABLE QString latestSnapshotAfterPath() const;

public slots:
    /// 响应节点参数变更（由 EditViewBridge 转发）
    void onNodeParamsChanged(const QString& nodeId);
    /// 响应变量值变更（由 VariableManager 转发）
    void onVariableChanged(const QString& varName);
    /// 响应节点选中变更（自动跟随选中节点）
    void onNodeSelected(const QString& nodeId);
    /// 响应节点删除（清理对应图像变量）
    void onNodeDeleted(const QString& nodeId);
    /// 响应连线变更
    void onConnectionsChanged();

signals:
    void autoPreviewEnabledChanged();
    void previewNodeIdChanged(const QString& nodeId);
    void isRunningChanged();
    /// 预览完成（成功/失败都发，QML 端更新 UI）
    void previewCompleted(const QString& nodeId, bool success, const QString& outputPath);
    /// 预览失败（带错误信息）
    void previewFailed(const QString& nodeId, const QString& error);
    /// 新快照已添加（QML 端可刷新分屏视图/历史面板）
    void snapshotAdded();
    /// 历史快照已清空
    void snapshotsCleared();

private slots:
    void doPreview();
    /// 异步执行完成后的回调（主线程）
    void onPreviewFinished();

private:
    ::EditViewBridge* m_bridge;   ///< 全局命名空间的 EditViewBridge
    ImageVariableManager* m_ivm;
    VariableManager* m_vm;

    bool m_autoPreviewEnabled;
    QString m_previewNodeId;
    bool m_isRunning;

    QTimer m_debounceTimer;  ///< 300ms 防抖定时器
    QString m_pendingNodeId; ///< 防抖期间待执行的节点 ID

    /// 异步执行 watcher：监听子线程算子执行完成，回主线程处理结果
    /// v2.6.1 修复卡死 bug：将 runSingleOperator 从主线程移到子线程，
    /// 避免 FFT 等耗时算子阻塞 UI 导致界面卡死。
    QFutureWatcher<QVariantMap> m_watcher;
    QString m_runningNodeId;  ///< 当前正在执行的节点 ID（用于结果回写）

    // === Snapshot 历史（LRU 50）===
    QList<PreviewSnapshot> m_snapshots;   ///< 历史快照列表（最新的在前）
    int m_maxSnapshots = 50;              ///< 最大历史数（LRU 淘汰上限）
    QImage m_pendingBeforeImage;          ///< 执行前图像（doPreview 中捕获，onPreviewFinished 中使用）

    /// 添加快照（在预览成功完成时调用）
    void appendSnapshot(const QString& nodeId, const QImage& before,
                        const QImage& after, const QJsonObject& params);
};

} // namespace QDV

#endif // QDV_PREVIEW_MANAGER_H
