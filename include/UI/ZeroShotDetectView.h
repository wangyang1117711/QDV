#ifndef ZEROSHOT_DETECT_VIEW_H
#define ZEROSHOT_DETECT_VIEW_H

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVariantMap>

#include <opencv2/core/mat.hpp>

// 引入 ZeroShotKit 门面与类型（zsu::Kit / zsu::ZeroShotResult / zsu::ZeroShotModelType）
#include "ZeroShotKit/ZeroShotKit.h"

// 前置声明，避免头文件强依赖 UI 实现头
namespace QDVMini { class ZeroShotPanel; class ZeroShotResultPanel; }
namespace QDV { class DataCollectionWizard; class TrainingBridge; }

class QSplitter;
class QLabel;
class QPushButton;
class QProgressBar;
class QTextEdit;
class QShowEvent;

// ============================================================================
// 零样本检测视图
// 作为 CentralWindow 第 9 个视图（索引 8），与 EditView 同级。
// 复用 ZeroShotKit 的 QWidget UI（ZeroShotPanel + ZeroShotResultPanel），
// 通过 zsu::Kit 门面承载零样本推理能力。
// 文件组织规范与编辑模块一致：头文件在 include/UI/，源文件在 src/UI/。
// ============================================================================
class ZeroShotDetectView : public QWidget {
    Q_OBJECT
public:
    explicit ZeroShotDetectView(QWidget* parent = nullptr);
    ~ZeroShotDetectView() override;

signals:
    // 供 CentralWindow 首页统计卡片（与 EditView::toolCountChanged 同级标准）
    void zeroShotResultReady(int resultCount);
    void zeroShotErrorOccurred(const QString& message);

private slots:
    // UI 面板信号 → 业务动作
    void onModelLoadRequested(zsu::ZeroShotModelType type, const QString& path);
    void onInferenceRequested();          // 单张推理
    void onInferenceAllRequested();       // 批量推理
    void onStopRequested();
    void onAddNormalSampleRequested();
    void onRemoveLastNormalSampleRequested();
    void onClearNormalSamplesRequested();
    void onHumanReviewToggled(bool enabled);
    void onSettingsChanged();             // 阈值/提示词同步到 Kit

    // v2.0 阶段二 Task 7：共享提示词库桥接
    void onCustomTargetTypeRequested(const QVariantMap& entry);  // 自定义类别保存请求
    void onTargetTypeSelectionChanged(const QStringList& selectedPrompts);  // 选择变化记录 recent

    // v2.0 阶段五 Task 13：训练专用模型入口桥接
    void onSpecializedTrainingRequested(const QVariantMap& entry);  // 启动数据收集向导
    void onDatasetExported(const QVariantMap& trainParams);         // 向导完成 → 启动训练
    void onSpecializedTrainingCompleted(const QVariantMap& result); // 训练完成 → 标记专用模型

    // v2.0 阶段七 Task 18：模型源监控桥接
    void onModelSelectionChanged(const QString& path, const QString& sourceType);  // 用户选中具体模型

    // v2.0 阶段六 Task 14：结果导出
    void onExportResultsRequested();          // 弹出 ResultExportDialog
    // v2.0 阶段六 Task 15：批量推理（选目录后立即推理）
    void onBatchInferRequested();

    // Kit 信号 → 结果面板
    void onInferenceCompleted(const zsu::ZeroShotResult& result);
    void onBatchCompleted(const QList<zsu::ZeroShotResult>& results);
    void onProgressUpdated(int current, int total);
    void onErrorOccurred(const QString& message);

private:
    void setupUI();                       // 三栏布局 + 工具栏
    void connectSignals();                // 信号槽连接
    void loadImageForInference();         // QFileDialog 选单张
    void loadBatchImages();               // QFileDialog 选批量
    void updateModelStatus();             // 同步面板状态
    void updateNotesView();               // 刷新右栏模型注意事项
    // 中文路径安全读取图像（cv::imread 不支持中文路径，用 QFile + cv::imdecode）
    cv::Mat readImageChineseSafe(const QString& path);

    // v2.0 阶段二 Task 7：从 PromptLibrary 加载条目并刷新 ZeroShotPanel
    void refreshTargetTypeEntries();

    // v2.0 阶段七 Task 18：从 ModelSourceManager 加载模型清单并刷新 ZeroShotPanel
    void refreshModelEntries();

    // v2.0 阶段五 Task 13：启动数据收集向导 → 训练 → 标记专用模型
    void launchDataCollectionWizard(const QVariantMap& entry);
    // v2.0 阶段五 Task 13：训练完成后根据 onnxPath 推断算子 type 并标记到 PromptLibrary
    void markSpecializedModelAfterTraining(const QString& prompt, const QString& onnxPath,
                                            const QString& modelName);

    // v1.1.0 修复：QStackedWidget 中首次显示时，子控件尚未完成 layout，
    // 导致 QSplitter 初始尺寸过小。在 showEvent 中延迟刷新 splitter sizes。
    void showEvent(QShowEvent* event) override;
    void refreshSplitterSizes();
    bool m_firstShow = true;              // 标记首次显示

    zsu::Kit*                          m_kit          = nullptr;
    QDVMini::ZeroShotPanel*            m_panel        = nullptr;
    QDVMini::ZeroShotResultPanel*      m_resultPanel  = nullptr;

    QSplitter*    m_splitter     = nullptr;
    QLabel*       m_statusLabel  = nullptr;
    QProgressBar* m_progressBar  = nullptr;
    QPushButton*  m_loadImageBtn = nullptr;
    QPushButton*  m_batchBtn     = nullptr;
    QPushButton*  m_stopBtn      = nullptr;
    QTextEdit*    m_notesView    = nullptr;   // 右栏模型注意事项显示
    QLabel*       m_banner       = nullptr;   // v2.0 阶段三 Task 9：定位调整提示横幅

    // v2.0 阶段六 Task 14：导出结果按钮
    QPushButton*  m_exportBtn    = nullptr;
    // v2.0 阶段六 Task 15：批量推理按钮（选目录后立即推理）
    QPushButton*  m_batchInferBtn = nullptr;

    QString     m_currentImagePath;        // 当前待推理图像
    QStringList m_batchImagePaths;         // 批量推理图像列表

    // v2.0 阶段六 Task 14：缓存最近一次单图推理结果与原图，供导出对话框使用
    zsu::ZeroShotResult m_lastResult;
    cv::Mat     m_lastImage;                // 最近一次推理的原图
    bool        m_hasLastResult = false;   // 是否有可导出的最近结果

    // v2.0 阶段五 Task 13：第二层 训练兑底流程
    QDV::TrainingBridge* m_trainingBridge = nullptr;  // 训练桥接（自有，用于第二层流程）
    QString m_pendingSpecializedPrompt;               // 训练中的类别 prompt（完成后用于标记）
};

#endif // ZEROSHOT_DETECT_VIEW_H
