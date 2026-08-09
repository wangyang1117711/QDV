#pragma once

#include <QWidget>
#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QLineEdit>
#include <QListWidget>
#include <QStackedWidget>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include "TrainingInference/TrainingProject.h"
#include "TrainingInference/ProjectSerializer.h"
#include <QProgressDialog>
#include <QPointer>
#include <QMenu>
#include "Monitoring/TrainingInferenceMonitor.h"
#include "Monitoring/ProcessSnapshot.h"
#include "Monitoring/Anomaly.h"

class ImageViewWidget;
class CategoryPanel;
class InferencePanel;
class ScriptEditorWidget;
class ResultPanel;
struct InferenceResult;
class QVBoxLayout;

namespace QDV { class TrainingBridge; }
class TrainingProgressDialog;

class TrainingInferenceView : public QWidget {
    Q_OBJECT

public:
    explicit TrainingInferenceView(QWidget* parent = nullptr);
    ~TrainingInferenceView() override;

signals:
    void viewChanged(const QString& viewName);

private slots:
    void onImportImages();
    void onImportFolder();
    void onOpenModelLibrary();
    void onRunTrainingPipeline();
    void onImagesImported(int count);
    void onImageSelected(int row);
    void onPreviewImage(const QString& filePath);
    void onDeleteSelected();
    void onClearAll();
    void onImageRemoved(const QString& filePath);
    void onImagesCleared();
    void onInferenceRequested(const QString& modelPath, const QStringList& imagePaths);
    void onInferenceCompleted(const QList<InferenceResult>& results);
    void onSelectAllClicked();
    void onInvertSelectionClicked();
    void onAddToCategoryRequested(const QString& categoryId, const QString& categoryName);
    void onSelectionChanged();
    void onItemChanged(QListWidgetItem* item);
    void onTrainingProgress(const QVariantMap& progress);
    void onTrainingCompleted(const QVariantMap& result);
    void onTrainingError(const QString& phase, const QString& message);
    void onTrainingLogOutput(const QString& message);

    // 项目操作
    void onNewProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onLoadProject();
    void onRecentProject_triggered();

    // 序列化器回调
    void onSaveProgress(int percent, const QString& stage);
    void onSaveFinished(const QString& filePath, bool success, const QString& message);
    void onLoadProgress(int percent, const QString& stage);
    void onLoadFinished(const QString& filePath, bool success, const QString& message,
                        const QJsonObject& projectJson, const QStringList& missingImages);

    // 脏标记管理
    void markProjectDirty();
    void updateTitle();

    // 最近项目
    void loadRecentProjects();
    void saveRecentProjects();
    void addRecentProject(const QString& path);
    void updateRecentProjectsMenu();

    // 训练推理监控
    void onMonitorSnapshotReady(const QDV::ProcessSnapshot& snapshot);
    void onAnomalyDetected(const QDV::Anomaly& anomaly);
    void onViewHistory();

private:
    void setupUI();
    void setupToolbar(QVBoxLayout* mainLayout);
    void setupImagePanel(QSplitter* splitter);
    void setupCenterPanel(QSplitter* splitter);
    void setupBottomPanel(QSplitter* splitter);
    void setupMonitoringPanel();
    void rebuildImageList();
    void updateSelectAllButton();
    void updateCategoryPanelButtons();

    // 项目管理辅助方法
    void setupProjectToolbar(QToolBar* toolbar);
    void collectSnapshotsToProject();
    bool confirmDiscardCurrentData(const QString& action);
    void restoreDataFromProject(TrainingProject* project);

    QDV::TrainingBridge* m_trainingBridge;
    TrainingProgressDialog* m_trainingProgressDialog = nullptr;
    QTextEdit* m_consoleLog;

    QToolBar* m_toolbar;
    QSplitter* m_mainSplitter;
    QSplitter* m_centerSplitter;

    QListWidget* m_imageList;
    ImageViewWidget* m_imageView;
    QPushButton* m_selectAllBtn;
    QPushButton* m_invertSelectionBtn;
    QLabel* m_selectionLabel;

    CategoryPanel* m_categoryPanel;
    InferencePanel* m_inferencePanel;

    QStackedWidget* m_centerStack;
    ScriptEditorWidget* m_scriptEditor;
    ResultPanel* m_resultPanel;

    // ===== 项目管理 =====
    TrainingProject* m_project;
    ProjectSerializer* m_serializer;
    QPointer<QProgressDialog> m_progressDialog;
    bool m_isLoadingOrSaving = false;  // 加载/保存项目期间标志，用于暂停Monitor等

    // 工具栏项目操作 Action
    QAction* m_newProjectAction = nullptr;
    QAction* m_saveProjectAction = nullptr;
    QAction* m_loadProjectAction = nullptr;
    QAction* m_saveAsProjectAction = nullptr;
    QMenu* m_recentProjectsMenu = nullptr;

    // 训练/推理工具栏按钮及其状态指示灯
    QAction* m_trainAction = nullptr;
    QAction* m_inferAction = nullptr;
    QLabel* m_trainStatusLight = nullptr;
    QLabel* m_inferStatusLight = nullptr;

    // 状态指示灯样式（空闲=灰、进行中=黄、完成=绿、失败=红）
    void setLightStatus(QLabel* light, const QString& color, const QString& tip);

    // 最近项目列表
    QStringList m_recentProjects;
    static constexpr int MAX_RECENT_PROJECTS = 10;

    // 训练推理监控面板
    QWidget* m_monitoringPanel = nullptr;
    QLabel* m_monitorStatusLabel = nullptr;
    QLabel* m_trainingMetricsLabel = nullptr;
    QLabel* m_cpuLabel = nullptr;
    QLabel* m_memoryLabel = nullptr;
    QLabel* m_gpuLabel = nullptr;
    QLabel* m_runtimeLabel = nullptr;
    QLabel* m_latencyLabel = nullptr;
    QLabel* m_trainingServiceLabel = nullptr;
    QLabel* m_loadedModelsLabel = nullptr;
    QLabel* m_inferenceLatencyLabel = nullptr;
    QLabel* m_latestAnomalyLabel = nullptr;
    QPushButton* m_viewHistoryBtn = nullptr;
    QList<QDV::Anomaly> m_recentAnomalies;
};