#include "TrainingInference/TrainingInferenceView.h"
#include "TrainingInference/ImageViewWidget.h"
#include "TrainingInference/CategoryPanel.h"
#include "TrainingInference/InferencePanel.h"
#include "TrainingInference/ScriptEditorWidget.h"
#include "TrainingInference/ResultPanel.h"
#include "TrainingInference/ImageManager.h"
#include "TrainingInference/CategoryManager.h"
#include "TrainingInference/ExportManager.h"
#include "TrainingInference/TrainingProgressDialog.h"
#include "TrainingInference/ModelLibraryDialog.h"
#include "AI/InferenceEngine.h"
#include "AI/ModelManager.h"
#include "AI/ImageCache.h"
#include "AI/TrainingBridge.h"
#include "Core/Logger.h"
#include "OperatorLibrary/OperatorLibraryController.h"  // spec D：训练产物自动注册为算子
#include "Monitoring/TrainingInferenceMonitor.h"
#include "Monitoring/ProcessSnapshot.h"
#include "Monitoring/Anomaly.h"
#include <algorithm>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QFileDialog>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QCoreApplication>
#include <QStackedWidget>
#include <QGroupBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QProgressBar>
#include <QDir>
#include <QFileInfo>
#include <QCheckBox>
#include <QPushButton>
#include <QWidget>
#include <QSignalBlocker>
#include <QSet>
#include <QAbstractItemView>
#include <QTime>
#include <QDialog>
#include <QFormLayout>
#include <QJsonArray>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QTimer>
#include <QRadioButton>
#include <QSettings>
#include <QJsonDocument>
#include <QDateTime>

TrainingInferenceView::TrainingInferenceView(QWidget* parent) : QWidget(parent)
{
    m_trainingBridge = new QDV::TrainingBridge(this);
    setupUI();

    connect(ImageManager::instance(), &ImageManager::imagesImported,
            this, &TrainingInferenceView::onImagesImported);
    connect(ImageManager::instance(), &ImageManager::imageRemoved,
            this, &TrainingInferenceView::onImageRemoved);
    connect(ImageManager::instance(), &ImageManager::imagesCleared,
            this, &TrainingInferenceView::onImagesCleared);
    connect(ImageManager::instance(), &ImageManager::selectionChanged,
            this, &TrainingInferenceView::onSelectionChanged);
    connect(m_inferencePanel, &InferencePanel::inferenceRequested,
            this, &TrainingInferenceView::onInferenceRequested);
    connect(m_inferencePanel, &InferencePanel::stopInferenceRequested,
            this, [this]() {
        // 停止推理的处理
        m_inferencePanel->setStatus("推理已停止");
    });
    connect(m_categoryPanel, &CategoryPanel::addToCategoryRequested,
            this, &TrainingInferenceView::onAddToCategoryRequested);
    
    // 连接训练桥接信号
    connect(m_trainingBridge, &QDV::TrainingBridge::trainingProgress,
            this, &TrainingInferenceView::onTrainingProgress);
    connect(m_trainingBridge, &QDV::TrainingBridge::trainingCompleted,
            this, &TrainingInferenceView::onTrainingCompleted);
    connect(m_trainingBridge, &QDV::TrainingBridge::trainingError,
            this, &TrainingInferenceView::onTrainingError);
    connect(m_trainingBridge, &QDV::TrainingBridge::logOutput,
            this, &TrainingInferenceView::onTrainingLogOutput);
    
    // 连接默认模型加载失败的信号
    connect(ModelManager::instance(), &ModelManager::defaultModelLoadFailed,
            this, [this](const QString& error) {
        QMessageBox::warning(this, "模型加载错误", error);
    });
    
    // 初始更新按钮状态
    updateCategoryPanelButtons();

    // ===== 初始化项目管理 =====
    m_project = new TrainingProject(this);
    m_serializer = new ProjectSerializer(this);

    // 连接序列化器信号
    connect(m_serializer, &ProjectSerializer::saveProgress,
            this, &TrainingInferenceView::onSaveProgress);
    connect(m_serializer, &ProjectSerializer::saveFinished,
            this, &TrainingInferenceView::onSaveFinished);
    connect(m_serializer, &ProjectSerializer::loadProgress,
            this, &TrainingInferenceView::onLoadProgress);
    connect(m_serializer, &ProjectSerializer::loadFinished,
            this, &TrainingInferenceView::onLoadFinished);

    // 连接脏标记信号
    connect(ImageManager::instance(), &ImageManager::imagesImported,
            this, &TrainingInferenceView::markProjectDirty);
    connect(ImageManager::instance(), &ImageManager::imageRemoved,
            this, &TrainingInferenceView::markProjectDirty);
    connect(ImageManager::instance(), &ImageManager::imagesCleared,
            this, &TrainingInferenceView::markProjectDirty);
    connect(ImageManager::instance(), &ImageManager::labelChanged,
            this, &TrainingInferenceView::markProjectDirty);
    connect(CategoryManager::instance(), &CategoryManager::categoryCreated,
            this, &TrainingInferenceView::markProjectDirty);
    connect(CategoryManager::instance(), &CategoryManager::categoryUpdated,
            this, &TrainingInferenceView::markProjectDirty);
    connect(CategoryManager::instance(), &CategoryManager::categoryDeleted,
            this, &TrainingInferenceView::markProjectDirty);

    // 加载最近项目列表
    loadRecentProjects();

    // 初始更新标题
    updateTitle();

    // 连接训练推理监控器信号并启动监控
    auto* monitor = QDV::TrainingInferenceMonitor::instance();
    connect(monitor, &QDV::TrainingInferenceMonitor::snapshotReady,
            this, &TrainingInferenceView::onMonitorSnapshotReady);
    connect(monitor, &QDV::TrainingInferenceMonitor::anomalyDetected,
            this, &TrainingInferenceView::onAnomalyDetected);
    monitor->start();
}

TrainingInferenceView::~TrainingInferenceView()
{
    // 停止监控器以减少开销（单例生命周期由程序管理，但可显式停止定时采样）
    QDV::TrainingInferenceMonitor::instance()->stop();
}

void TrainingInferenceView::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setupToolbar(mainLayout);
    setupMonitoringPanel();
    mainLayout->addWidget(m_monitoringPanel);

    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->setHandleWidth(3);
    m_mainSplitter->setStyleSheet("QSplitter::handle { background-color: #555; }");

    setupImagePanel(m_mainSplitter);

    m_centerSplitter = new QSplitter(Qt::Vertical);
    m_centerSplitter->setHandleWidth(3);
    m_centerSplitter->setStyleSheet("QSplitter::handle { background-color: #555; }");

    setupCenterPanel(m_centerSplitter);
    setupBottomPanel(m_centerSplitter);

    m_mainSplitter->addWidget(m_centerSplitter);

    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 3);
    m_mainSplitter->setSizes({280, 800});

    // v5.0：设置中心/底部垂直 Splitter 初始比例（中心占 70%，底部训练日志占 30%）
    // 之前未调用 setSizes，导致 Qt 默认均分，底部训练日志与中心面板比例失衡
    QTimer::singleShot(0, this, [this]() {
        if (m_centerSplitter && m_centerSplitter->count() >= 2) {
            int totalH = m_centerSplitter->height();
            if (totalH > 100) {
                m_centerSplitter->setSizes({static_cast<int>(totalH * 0.7), static_cast<int>(totalH * 0.3)});
            }
        }
    });

    mainLayout->addWidget(m_mainSplitter, 1);
}

void TrainingInferenceView::setupToolbar(QVBoxLayout* mainLayout)
{
    m_toolbar = new QToolBar();
    m_toolbar->setStyleSheet(
        "QToolBar { background-color: #2d2d2d; border-bottom: 1px solid #444; "
        "padding: 3px 12px; spacing: 6px; }"
        "QToolBar QToolButton { background: transparent; border: 1px solid transparent; "
        "border-radius: 4px; padding: 4px 14px; color: #e0e0e0; font-size: 12px; }"
        "QToolBar QToolButton:hover { background-color: #555; border-color: #555; }"
        "QToolBar QToolButton:disabled { color: #666; }");

    // ===== 项目操作按钮（最左侧）=====
    m_newProjectAction = m_toolbar->addAction("新建项目");
    m_newProjectAction->setShortcut(QKeySequence::New);  // Ctrl+N
    connect(m_newProjectAction, &QAction::triggered, this, &TrainingInferenceView::onNewProject);

    m_saveProjectAction = m_toolbar->addAction("保存项目");
    m_saveProjectAction->setShortcut(QKeySequence::Save);  // Ctrl+S
    connect(m_saveProjectAction, &QAction::triggered, this, &TrainingInferenceView::onSaveProject);

    m_saveAsProjectAction = m_toolbar->addAction("另存为");
    connect(m_saveAsProjectAction, &QAction::triggered, this, &TrainingInferenceView::onSaveProjectAs);

    m_loadProjectAction = m_toolbar->addAction("加载项目");
    m_loadProjectAction->setShortcut(QKeySequence::Open);  // Ctrl+O
    connect(m_loadProjectAction, &QAction::triggered, this, &TrainingInferenceView::onLoadProject);

    // 最近项目下拉菜单
    m_recentProjectsMenu = new QMenu(this);
    QAction* recentAction = m_toolbar->addAction("最近项目");
    recentAction->setMenu(m_recentProjectsMenu);
    connect(recentAction, &QAction::triggered, this, &TrainingInferenceView::onRecentProject_triggered);
    updateRecentProjectsMenu();

    m_toolbar->addSeparator();

    QAction* importAction = m_toolbar->addAction("导入图像");
    connect(importAction, &QAction::triggered, this, &TrainingInferenceView::onImportImages);

    QAction* importFolderAction = m_toolbar->addAction("导入文件夹");
    connect(importFolderAction, &QAction::triggered, this, &TrainingInferenceView::onImportFolder);

    m_toolbar->addSeparator();

    QAction* modelLibAction = m_toolbar->addAction("模型库");
    modelLibAction->setToolTip("打开模型库管理对话框，查看/导入/删除模型");
    connect(modelLibAction, &QAction::triggered, this, &TrainingInferenceView::onOpenModelLibrary);

    // —— 训练按钮 ——
    m_trainAction = m_toolbar->addAction("启动训练");
    m_trainAction->setToolTip("导入图像并标注后开始训练，建议每类至少 50-100 张");
    connect(m_trainAction, &QAction::triggered, this, &TrainingInferenceView::onRunTrainingPipeline);

    // 训练状态指示灯
    m_trainStatusLight = new QLabel();
    m_trainStatusLight->setFixedSize(12, 12);
    m_trainStatusLight->setToolTip("训练状态：空闲");
    m_toolbar->addWidget(m_trainStatusLight);
    setLightStatus(m_trainStatusLight, "gray", "空闲");

    // —— 推理按钮 ——
    m_inferAction = m_toolbar->addAction("快速推理");
    m_inferAction->setToolTip("选择模型后对导入图像执行推理，支持批量推理");
    connect(m_inferAction, &QAction::triggered, [this]() {
        // 推理按钮点击后立即锁定状态
        m_inferAction->setEnabled(false);
        setLightStatus(m_inferStatusLight, "yellow", "推理中");

        QString modelPath = m_inferencePanel->currentModelPath();
        if (modelPath.isEmpty()) {
            modelPath = ModelManager::instance()->getDefaultModelPath();
        }

        if (modelPath.isEmpty()) {
            QMessageBox::warning(this, "提示", "未找到可用的模型");
            m_inferAction->setEnabled(true);
            setLightStatus(m_inferStatusLight, "gray", "空闲");
            return;
        }

        QStringList paths = ImageManager::instance()->selectedPaths();
        if (paths.isEmpty()) {
            paths = ImageManager::instance()->allPaths();
        }

        // 同步切换推理面板按钮状态（与面板"开始推理"按钮保持一致）
        m_inferencePanel->setStatus("推理中...");
        m_inferencePanel->resetButtons();  // 先 reset 确保一致
        // 通过面板信号触发推理
        emit m_inferencePanel->inferenceRequested(modelPath, paths);
    });

    // 推理状态指示灯
    m_inferStatusLight = new QLabel();
    m_inferStatusLight->setFixedSize(12, 12);
    m_inferStatusLight->setToolTip("推理状态：空闲");
    m_toolbar->addWidget(m_inferStatusLight);
    setLightStatus(m_inferStatusLight, "gray", "空闲");

    m_toolbar->addSeparator();

    QAction* deleteAction = m_toolbar->addAction("删除选中");
    connect(deleteAction, &QAction::triggered, this, &TrainingInferenceView::onDeleteSelected);

    QAction* clearAction = m_toolbar->addAction("清空列表");
    connect(clearAction, &QAction::triggered, this, &TrainingInferenceView::onClearAll);

    mainLayout->addWidget(m_toolbar);
}

void TrainingInferenceView::setupImagePanel(QSplitter* splitter)
{
    QWidget* imagePanel = new QWidget();
    QVBoxLayout* imageLayout = new QVBoxLayout(imagePanel);
    imageLayout->setContentsMargins(8, 8, 8, 8);
    imageLayout->setSpacing(8);

    // 顶部标题区域，包含全选、反选按钮和选择状态
    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* imageListLabel = new QLabel("图像列表");
    imageListLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e0e0e0;");
    
    m_selectAllBtn = new QPushButton("全选");
    m_selectAllBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #3d3d3d;
            color: #e0e0e0;
            border: 1px solid #555;
            border-radius: 3px;
            padding: 4px 12px;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: #555;
        }
        QPushButton:pressed {
            background-color: #2a2a2a;
        }
    )");
    connect(m_selectAllBtn, &QPushButton::clicked, this, &TrainingInferenceView::onSelectAllClicked);
    
    m_invertSelectionBtn = new QPushButton("反选");
    m_invertSelectionBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #3d3d3d;
            color: #e0e0e0;
            border: 1px solid #555;
            border-radius: 3px;
            padding: 4px 12px;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: #555;
        }
        QPushButton:pressed {
            background-color: #2a2a2a;
        }
    )");
    connect(m_invertSelectionBtn, &QPushButton::clicked, this, &TrainingInferenceView::onInvertSelectionClicked);
    
    m_selectionLabel = new QLabel("已选: 0");
    m_selectionLabel->setStyleSheet("font-size: 12px; color: #aaa;");
    
    headerLayout->addWidget(imageListLabel);
    headerLayout->addSpacing(8);
    headerLayout->addWidget(m_selectAllBtn);
    headerLayout->addSpacing(6);
    headerLayout->addWidget(m_invertSelectionBtn);
    headerLayout->addSpacing(8);
    headerLayout->addWidget(m_selectionLabel);
    headerLayout->addStretch();
    
    imageLayout->addLayout(headerLayout);

    m_imageList = new QListWidget();
    m_imageList->setIconSize(QSize(128, 128));
    // 多选支持: Ctrl/Shift 连续选择, 与行内 CheckBox 状态保持双向同步
    m_imageList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // 大量图像优化：统一 item 尺寸可显著降低 QListWidget 布局计算开销
    m_imageList->setUniformItemSizes(true);
    m_imageList->setResizeMode(QListView::Adjust);
    m_imageList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_imageList->setStyleSheet(R"(
        QListWidget {
            background-color: #252525;
            border: 1px solid #444;
            border-radius: 4px;
            color: #e0e0e0;
        }
        QListWidget::item {
            padding: 8px;
            border-bottom: 1px solid #333;
        }
        QListWidget::item:selected {
            background-color: #7C4DFF;
        }
        QListWidget::item:hover {
            background-color: #333;
        }
    )");
    connect(m_imageList, &QListWidget::currentRowChanged,
            this, &TrainingInferenceView::onImageSelected);
    connect(m_imageList, &QListWidget::itemChanged,
            this, &TrainingInferenceView::onItemChanged);
    imageLayout->addWidget(m_imageList, 1);

    m_imageView = new ImageViewWidget();
    m_imageView->setMinimumHeight(200);
    imageLayout->addWidget(m_imageView);

    splitter->addWidget(imagePanel);
}

void TrainingInferenceView::setupCenterPanel(QSplitter* splitter)
{
    QWidget* topPanel = new QWidget();
    QHBoxLayout* topLayout = new QHBoxLayout(topPanel);
    topLayout->setContentsMargins(4, 8, 4, 4);
    topLayout->setSpacing(8);

    m_categoryPanel = new CategoryPanel();
    m_categoryPanel->setMinimumWidth(180);   // v5.0：fixedWidth→minimumWidth，允许拉伸
    m_categoryPanel->setMaximumWidth(320);
    topLayout->addWidget(m_categoryPanel);

    m_inferencePanel = new InferencePanel();
    m_inferencePanel->setMinimumWidth(180);   // v5.0：fixedWidth→minimumWidth，允许拉伸
    m_inferencePanel->setMaximumWidth(320);
    topLayout->addWidget(m_inferencePanel);

    m_centerStack = new QStackedWidget();
    m_scriptEditor = new ScriptEditorWidget();
    m_resultPanel = new ResultPanel();
    m_centerStack->addWidget(m_scriptEditor);
    m_centerStack->addWidget(m_resultPanel);
    m_centerStack->setCurrentIndex(0);

    topLayout->addWidget(m_centerStack, 1);

    splitter->addWidget(topPanel);
}

void TrainingInferenceView::setupBottomPanel(QSplitter* splitter)
{
    QWidget* bottomPanel = new QWidget();
    QVBoxLayout* bottomLayout = new QVBoxLayout(bottomPanel);
    bottomLayout->setContentsMargins(8, 4, 8, 8);

    QLabel* consoleLabel = new QLabel("训练日志");
    consoleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #e0e0e0;");
    bottomLayout->addWidget(consoleLabel);

    m_consoleLog = new QTextEdit();
    m_consoleLog->setReadOnly(true);
    m_consoleLog->setStyleSheet(R"(
        QTextEdit {
            background-color: #1a1a1a;
            border: 1px solid #444;
            border-radius: 4px;
            color: #aaa;
            font-family: 'Consolas', monospace;
            font-size: 12px;
            padding: 8px;
        }
    )");
    bottomLayout->addWidget(m_consoleLog);

    splitter->addWidget(bottomPanel);
}

void TrainingInferenceView::setupMonitoringPanel()
{
    // 监控条：固定高度，深色背景，位于工具栏下方
    m_monitoringPanel = new QWidget();
    m_monitoringPanel->setFixedHeight(90);
    m_monitoringPanel->setStyleSheet("background-color: #252525; border-bottom: 1px solid #444;");

    QHBoxLayout* layout = new QHBoxLayout(m_monitoringPanel);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(16);

    // 左侧：标题 + 状态
    QVBoxLayout* leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(4);
    QLabel* titleLabel = new QLabel("训练/推理监控");
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #ffffff;");
    m_monitorStatusLabel = new QLabel("就绪");
    m_monitorStatusLabel->setStyleSheet("font-size: 12px; color: #aaa;");
    leftLayout->addWidget(m_monitorStatusLabel);

    m_trainingMetricsLabel = new QLabel("");
    m_trainingMetricsLabel->setStyleSheet("font-size: 11px; color: #888;");
    leftLayout->addWidget(m_trainingMetricsLabel);
    leftLayout->addStretch();
    layout->addLayout(leftLayout);

    // 中间：指标标签
    QHBoxLayout* metricsLayout = new QHBoxLayout();
    metricsLayout->setSpacing(12);

    auto createMetricLabel = [](const QString& text) -> QLabel* {
        QLabel* label = new QLabel(text);
        label->setStyleSheet("font-size: 12px; color: #aaa;");
        return label;
    };

    m_cpuLabel = createMetricLabel("CPU: --");
    m_memoryLabel = createMetricLabel("内存: --");
    m_gpuLabel = createMetricLabel("GPU: --");
    m_runtimeLabel = createMetricLabel("已运行: -- / 剩余: --");
    m_latencyLabel = createMetricLabel("服务延迟: --");
    m_trainingServiceLabel = createMetricLabel("训练服务: 离线");
    m_loadedModelsLabel = createMetricLabel("已加载模型: --");
    m_inferenceLatencyLabel = createMetricLabel("最近推理延迟: --");

    metricsLayout->addWidget(m_cpuLabel);
    metricsLayout->addWidget(m_memoryLabel);
    metricsLayout->addWidget(m_gpuLabel);
    metricsLayout->addWidget(m_runtimeLabel);
    metricsLayout->addWidget(m_latencyLabel);
    metricsLayout->addWidget(m_trainingServiceLabel);
    metricsLayout->addWidget(m_loadedModelsLabel);
    metricsLayout->addWidget(m_inferenceLatencyLabel);
    metricsLayout->addStretch();
    layout->addLayout(metricsLayout, 1);

    // 右侧：查看历史按钮 + 最近异常
    QVBoxLayout* rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(6);

    m_viewHistoryBtn = new QPushButton("查看历史");
    m_viewHistoryBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #3d3d3d;
            color: #e0e0e0;
            border: 1px solid #555;
            border-radius: 3px;
            padding: 4px 12px;
            font-size: 12px;
        }
        QPushButton:hover { background-color: #555; }
        QPushButton:pressed { background-color: #2a2a2a; }
    )");
    connect(m_viewHistoryBtn, &QPushButton::clicked, this, &TrainingInferenceView::onViewHistory);

    m_latestAnomalyLabel = new QLabel("最近异常: 无");
    m_latestAnomalyLabel->setStyleSheet("font-size: 12px; color: #aaa;");
    m_latestAnomalyLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    rightLayout->addWidget(m_viewHistoryBtn, 0, Qt::AlignRight);
    rightLayout->addWidget(m_latestAnomalyLabel, 0, Qt::AlignRight);
    layout->addLayout(rightLayout);
}

void TrainingInferenceView::onImportImages()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, "选择图像", QString(),
        "Images (*.png *.jpg *.jpeg *.bmp *.tiff *.webp)");
    if (files.isEmpty()) return;

    QStringList imported = ImageManager::instance()->importImages(files);
}

void TrainingInferenceView::onImportFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择文件夹");
    if (dir.isEmpty()) return;

    QStringList filters = {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tiff", "*.webp"};
    QDir directory(dir);
    QStringList files;
    for (const QString& filter : filters)
    {
        QFileInfoList entries = directory.entryInfoList({filter}, QDir::Files);
        for (const QFileInfo& fi : entries)
        {
            files.append(fi.absoluteFilePath());
        }
    }

    QStringList imported = ImageManager::instance()->importImages(files);
}

void TrainingInferenceView::onOpenModelLibrary()
{
    ModelLibraryDialog dialog(this);
    // 如果当前已有选中模型，同步到对话框
    dialog.setCurrentModelPath(m_inferencePanel->currentModelPath());

    if (dialog.exec() == QDialog::Accepted) {
        QString selectedPath = dialog.selectedModelPath();
        if (!selectedPath.isEmpty()) {
            // 内置模型路径可能是相对路径，转换为绝对路径便于后续加载
            if (QFileInfo(selectedPath).isRelative()) {
                selectedPath = QDir::current().absoluteFilePath(selectedPath);
            }
            m_inferencePanel->setCurrentModelPath(selectedPath);
            QDV::Logger::info(QString("从模型库选择模型: %1").arg(selectedPath));
        }
    }
}

void TrainingInferenceView::onRunTrainingPipeline()
{
    auto images = ImageManager::instance()->images();
    if (images.isEmpty())
    {
        QMessageBox::information(this, "提示", "请先导入训练图像");
        return;
    }
    
    // 收集标注数据
    QList<QPair<QString, QString>> imageLabelPairs;
    QSet<QString> uniqueLabels;
    int annotatedCount = 0;
    for (const auto& entry : images) {
        if (entry.isAnnotated && !entry.label.isEmpty()) {
            imageLabelPairs.append(qMakePair(entry.filePath, entry.label));
            uniqueLabels.insert(entry.label);
            annotatedCount++;
        }
    }
    
    if (annotatedCount == 0) {
        QMessageBox::information(this, "提示", "请先为一些图像添加类别标签再开始训练");
        return;
    }
    
    // 修复: QSet 迭代顺序不确定，排序后保证每次训练标签顺序一致
    QStringList allLabels = uniqueLabels.values();
    std::sort(allLabels.begin(), allLabels.end());
    if (allLabels.size() < 2) {
        QMessageBox::information(this, "提示", "训练至少需要 2 个类别，请添加更多类别的标注数据");
        return;
    }
    
    // 检查每类样本数
    QMap<QString, int> labelCounts;
    for (const auto& pair : imageLabelPairs) {
        labelCounts[pair.second]++;
    }
    bool hasSmallCategory = false;
    QString smallCategoryWarning;
    for (const auto& label : allLabels) {
        if (labelCounts[label] < 5) {
            hasSmallCategory = true;
            smallCategoryWarning += QString("  - %1: %2 张\n").arg(label).arg(labelCounts[label]);
        }
    }
    if (hasSmallCategory) {
        auto reply = QMessageBox::question(this, "样本数量警告", 
            QString("以下类别样本数量较少（建议每类至少 5 张）：\n%1\n是否继续训练？").arg(smallCategoryWarning),
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }
    
    // 确定 Python 路径（优先使用虚拟环境）
    QString pythonPath = "E:/anchor/Trae/QDV/training/venv/Scripts/python.exe";
    if (!QFile::exists(pythonPath)) {
        pythonPath = "python";  // 回退到系统 Python
    }

    // 检查 Python 环境
    QString error;
    if (!QDV::TrainingBridge::checkPythonEnvironment(pythonPath, error)) {
        QMessageBox::critical(this, "环境检查失败", 
            QString("训练需要 Python 环境：\n%1\n\n请先安装 Python 3.8+").arg(error));
        return;
    }
    
    // 训练配置对话框
    QDialog configDialog(this);
    configDialog.setWindowTitle("训练配置");
    QFormLayout* formLayout = new QFormLayout(&configDialog);
    
    QComboBox* modelTypeCombo = new QComboBox();
    modelTypeCombo->addItems({"resnet18", "resnet50", "efficientnet_b0", "mobilenet_v3_small"});
    formLayout->addRow("模型类型:", modelTypeCombo);
    
    QSpinBox* epochSpin = new QSpinBox();
    epochSpin->setRange(1, 200);
    epochSpin->setValue(20);
    formLayout->addRow("训练轮数:", epochSpin);
    
    QSpinBox* batchSpin = new QSpinBox();
    batchSpin->setRange(1, 128);
    batchSpin->setValue(8);
    formLayout->addRow("批次大小:", batchSpin);
    
    QDoubleSpinBox* lrSpin = new QDoubleSpinBox();
    lrSpin->setRange(0.00001, 0.1);
    lrSpin->setDecimals(5);
    lrSpin->setValue(0.001);
    lrSpin->setSingleStep(0.0005);
    formLayout->addRow("学习率:", lrSpin);
    
    QDoubleSpinBox* valSplitSpin = new QDoubleSpinBox();
    valSplitSpin->setRange(0.05, 0.5);
    valSplitSpin->setDecimals(2);
    valSplitSpin->setValue(0.2);
    valSplitSpin->setSingleStep(0.05);
    formLayout->addRow("验证集比例:", valSplitSpin);
    
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, &configDialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &configDialog, &QDialog::reject);
    formLayout->addRow(buttonBox);
    
    if (configDialog.exec() != QDialog::Accepted) {
        return;
    }
    
    // 生成数据集清单
    QString manifestPath = QDir::temp().absoluteFilePath("qdv_train_manifest.json");
    QString finalManifestPath = QDV::TrainingBridge::generateDatasetManifest(
        imageLabelPairs, allLabels, valSplitSpin->value(), manifestPath);
    if (finalManifestPath.isEmpty()) {
        QMessageBox::critical(this, "错误", "无法生成训练数据集清单");
        return;
    }
    
    // 清空控制台，启动训练
    m_consoleLog->clear();
    m_consoleLog->append(QString("[%1] 开始训练...").arg(QTime::currentTime().toString("hh:mm:ss")));
    m_consoleLog->append(QString("模型: %1, 轮数: %2, 批次: %3, 学习率: %4")
        .arg(modelTypeCombo->currentText()).arg(epochSpin->value())
        .arg(batchSpin->value()).arg(lrSpin->value()));
    m_consoleLog->append(QString("训练样本: %1 张, 类别: %2 个")
        .arg(annotatedCount).arg(allLabels.size()));
    
    QString outputDir = "E:/anchor/Trae/QDV/models";
    m_trainingBridge->startTraining(
        finalManifestPath,
        outputDir,
        modelTypeCombo->currentText(),
        epochSpin->value(),
        batchSpin->value(),
        lrSpin->value(),
        pythonPath  // 使用虚拟环境 Python
    );

    // 训练已启动：锁定训练按钮，禁用推理按钮，指示灯转黄
    m_trainAction->setEnabled(false);
    m_inferAction->setEnabled(false);
    setLightStatus(m_trainStatusLight, "yellow", "训练中");
    setLightStatus(m_inferStatusLight, "gray", "空闲");

    // --- 显示训练进度对话框 ---
    // 先关闭旧对话框（如果存在）
    if (m_trainingProgressDialog) {
        m_trainingProgressDialog->close();
        m_trainingProgressDialog->deleteLater();
        m_trainingProgressDialog = nullptr;
    }

    m_trainingProgressDialog = new TrainingProgressDialog(epochSpin->value(), this);

    // 连接训练进度信号 -> 对话框更新
    connect(m_trainingBridge, &QDV::TrainingBridge::trainingProgress,
            m_trainingProgressDialog, &TrainingProgressDialog::updateProgress);

    // 连接取消按钮 -> 取消训练
    connect(m_trainingProgressDialog, &TrainingProgressDialog::cancelled,
            this, [this]() {
        m_trainingBridge->cancelTraining();
    });

    // 训练完成时关闭对话框（由 onTrainingCompleted 处理）
    // 训练失败时也关闭对话框
    connect(m_trainingBridge, &QDV::TrainingBridge::trainingError,
            m_trainingProgressDialog, [this](const QString& phase, const QString& message) {
        Q_UNUSED(phase);
        Q_UNUSED(message);
        if (m_trainingProgressDialog) {
            m_trainingProgressDialog->close();
            m_trainingProgressDialog->deleteLater();
            m_trainingProgressDialog = nullptr;
        }
    });

    m_trainingProgressDialog->show();
}

void TrainingInferenceView::onTrainingProgress(const QVariantMap& map)
{
    int epoch = map["epoch"].toInt();
    int total = map["totalEpochs"].toInt();
    double trainAcc = map["trainAccuracy"].toDouble();
    double valAcc = map["valAccuracy"].toDouble();
    m_consoleLog->append(QString("[Epoch %1/%2] Train Acc: %3%, Val Acc: %4%")
        .arg(epoch).arg(total)
        .arg(QString::number(trainAcc * 100, 'f', 1))
        .arg(QString::number(valAcc * 100, 'f', 1)));
}

void TrainingInferenceView::onTrainingCompleted(const QVariantMap& map)
{
    // --- 关闭进度对话框 ---
    if (m_trainingProgressDialog) {
        m_trainingProgressDialog->close();
        m_trainingProgressDialog->deleteLater();
        m_trainingProgressDialog = nullptr;
    }

    // 恢复训练/推理按钮状态
    m_trainAction->setEnabled(true);
    m_inferAction->setEnabled(true);

    bool success = map["success"].toBool();
    if (success) {
        // 训练成功：指示灯转绿
        setLightStatus(m_trainStatusLight, "green", "完成");

        QString onnxPath = map["onnxPath"].toString();
        QString labelsPath = map["labelsPath"].toString();
        double totalTime = map["totalTimeSeconds"].toDouble();

        m_consoleLog->append(QString("[%1] 训练完成！").arg(QTime::currentTime().toString("hh:mm:ss")));
        m_consoleLog->append(QString("总耗时: %1 秒").arg(QString::number(totalTime, 'f', 1)));
        m_consoleLog->append("模型保存: " + onnxPath);

        // --- 注册训练得到的模型到模型库 ---
        // 使用 ONNX 文件基本名作为模型名称，确保与指定输出路径 model.onnx 一致
        QString modelName = QFileInfo(onnxPath).completeBaseName();
        if (modelName.isEmpty()) {
            modelName = map.value("modelName", "trained_model").toString();
        }

        // 监听 modelRegistered 信号获取 ModelManager 内部最终确定的模型名和 ONNX 路径
        // （当目标名称冲突时，ModelManager 会自动追加时间戳后缀，最终路径与原始 onnxPath 不同）
        QString finalModelName;
        QString finalOnnxPath;
        QMetaObject::Connection regConn = connect(
            ModelManager::instance(), &ModelManager::modelRegistered,
            this, [&finalModelName, &finalOnnxPath](const QString& name, const QString& path) {
                finalModelName = name;
                finalOnnxPath  = path;
            });

        bool registered = ModelManager::instance()->registerTrainedModel(
            onnxPath, labelsPath, modelName);

        disconnect(regConn);  // 一次性监听，调用后立即断开

        if (registered) {
            m_consoleLog->append(QString("模型已注册到模型库: %1").arg(modelName));
            QMessageBox::information(this, "训练完成",
                QString("模型训练成功！\n\n"
                        "模型已保存并注册到模型库。\n\n"
                        "模型名称: %1\n"
                        "ONNX路径: %2").arg(modelName, onnxPath));

            // --- spec D Task 11：训练产物自动注册为算子 ---
            // 信号链：TrainingBridge::trainingCompleted → ModelManager::registerTrainedModel
            //         → OperatorLibraryController::registerTrainedModelAsOperator
            // 优先使用 ModelManager 内部最终确定的路径（含命名冲突时的时间戳消歧）
            QString opModelName  = finalModelName.isEmpty()  ? modelName  : finalModelName;
            QString opOnnxPath   = finalOnnxPath.isEmpty()   ? onnxPath   : finalOnnxPath;
            bool opRegistered = false;
            try {
                opRegistered = QDV::OperatorLibrary::OperatorLibraryController::instance()
                    ->registerTrainedModelAsOperator(opModelName, opOnnxPath, labelsPath, opModelName);
            } catch (...) {
                opRegistered = false;  // 防御性：算子注册异常不影响主流程
            }
            if (opRegistered) {
                m_consoleLog->append(QString("已自动注册算子: YOLO: %1（拖入方案即可使用）").arg(opModelName));
            } else {
                // 算子注册失败不阻断主流程：训练产物已入库 ModelManager，用户仍可手动拖 YoloDetect 算子并填 modelPath
                m_consoleLog->append("提示: 训练产物算子自动注册失败，不影响训练结果（可手动使用 YoloDetect 算子加载该模型）");
                qWarning() << "[TrainingInferenceView] registerTrainedModelAsOperator failed for model:" << opModelName;
            }
        } else {
            m_consoleLog->append("警告: 模型注册失败，但文件已保存");
            QMessageBox::warning(this, "训练完成",
                QString("模型训练成功，但注册到模型库时出现问题。\n\n"
                        "模型已保存到：\n%1\n\n"
                        "请手动检查模型库状态。").arg(onnxPath));
        }

        // 刷新推理面板的模型列表
        m_inferencePanel->refreshModelList();
    } else {
        // 训练失败：指示灯转红
        setLightStatus(m_trainStatusLight, "red", "失败");

        QString error = map["errorMessage"].toString();
        m_consoleLog->append(QString("[错误] 训练失败: %1").arg(error));
        QMessageBox::critical(this, "训练失败", error);
    }
}

void TrainingInferenceView::onTrainingError(const QString& phase, const QString& message)
{
    m_consoleLog->append(QString("[错误] %1: %2").arg(phase, message));

    // 训练错误：恢复按钮，指示灯转红
    m_trainAction->setEnabled(true);
    m_inferAction->setEnabled(true);
    setLightStatus(m_trainStatusLight, "red", "错误");
}

void TrainingInferenceView::onTrainingLogOutput(const QString& message)
{
    m_consoleLog->append(message);
}

// 设置状态指示灯颜色和提示文本
// color: "gray"(空闲) / "yellow"(进行中) / "green"(成功) / "red"(失败)
void TrainingInferenceView::setLightStatus(QLabel* light, const QString& color, const QString& tip)
{
    if (!light) return;

    QString bgColor;
    QString borderColor;
    if (color == "yellow") {
        bgColor = "#FFD700";
        borderColor = "#B8860B";
    } else if (color == "green") {
        bgColor = "#4CAF50";
        borderColor = "#2E7D32";
    } else if (color == "red") {
        bgColor = "#F44336";
        borderColor = "#C62828";
    } else {
        bgColor = "#888";
        borderColor = "#555";
    }

    light->setStyleSheet(QString(
        "QLabel { background-color: %1; border: 1px solid %2; border-radius: 6px; }"
    ).arg(bgColor, borderColor));

    // 更新 tooltip（自动识别训练/推理）
    QString prefix = (light == m_trainStatusLight) ? "训练" : "推理";
    light->setToolTip(QString("%1状态：%2").arg(prefix, tip));
}

void TrainingInferenceView::onImagesImported(int count)
{
    Q_UNUSED(count);
    rebuildImageList();
}

void TrainingInferenceView::rebuildImageList()
{
    QDV::Logger::debug(QString("[DBG-R1] rebuildImageList ENTRY: listCount=%1 dataCount=%2 currentRow=%3")
              .arg(m_imageList->count())
              .arg(ImageManager::instance()->imageCount())
              .arg(m_imageList->currentRow()));

    int previousRow = m_imageList->currentRow();
    QString previousPath;
    if (previousRow >= 0)
    {
        QListWidgetItem* prevItem = m_imageList->item(previousRow);
        if (prevItem) previousPath = prevItem->toolTip();
    }

    disconnect(m_imageList, &QListWidget::currentRowChanged,
               this, &TrainingInferenceView::onImageSelected);
    disconnect(m_imageList, &QListWidget::itemChanged,
               this, &TrainingInferenceView::onItemChanged);

    QDV::Logger::debug("[DBG-R2] rebuildImageList: disconnect done, cleaning old widgets");

    // 关键修复：手动移除并立即销毁所有旧 item widget，避免 QListWidget::clear() 的
    // 延迟删除与后续新建 widget 交错，导致 Qt 内部事件处理时访问已释放内存，
    // 触发 ACCESS_VIOLATION 崩溃。
    for (int i = m_imageList->count() - 1; i >= 0; --i) {
        QListWidgetItem* item = m_imageList->item(i);
        QWidget* w = m_imageList->itemWidget(item);
        if (w) {
            m_imageList->removeItemWidget(item);
            delete w;  // 立即删除，避免延迟删除队列堆积
        }
    }
    m_imageList->clear();

    // 关键修复：移除 processEvents 和 sendPostedEvents 调用。
    // 这些调用会导致事件循环递归，在 rebuildImageList 执行期间处理其他 pending 事件
    // （如 QTimer::singleShot(0) 回调、Monitor 的 onTick），从而访问到正在被重建的
    // QListWidget 状态，导致 ACCESS_VIOLATION 崩溃。
    // 旧 widget 已通过上面的 delete 立即销毁，无需等待 DeferredDelete。

    QDV::Logger::debug(QString("[DBG-R3] rebuildImageList after clear: listCount=%1")
              .arg(m_imageList->count()));

    auto images = ImageManager::instance()->images();
    int restoreRow = -1;

    // 创建期间禁用 viewport 更新，避免每个 item 都触发重绘/布局
    // 507 个 item 会在统一重绘时一次性渲染，避免逐项重绘的开销
    const int kBatchSize = 100;
    m_imageList->setUpdatesEnabled(false);

    for (int i = 0; i < images.size(); ++i)
    {
        const auto& entry = images[i];

        // 关键修复：移除批次边界处的 processEvents 调用。
        // 507 个 QListWidgetItem 的创建是纯内存操作，不会阻塞事件循环。
        // processEvents 在此处会导致递归事件处理，是崩溃的主要触发点。
        // 原方案每个图像创建 QWidget + QHBoxLayout + QCheckBox + QLabel 等多层控件，
        // 507 张图像同时存在时极易触发 Qt 内部内存/事件循环崩溃。
        QListWidgetItem* item = new QListWidgetItem();
        item->setToolTip(entry.filePath);
        item->setSizeHint(QSize(0, 80));
        item->setCheckState(entry.isSelected ? Qt::Checked : Qt::Unchecked);

        // 缩略图
        if (!entry.icon.isNull()) {
            QPixmap pixmap = entry.icon.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            item->setIcon(QIcon(pixmap));
        }

        // 文本：文件名 + 类别标签
        QString text = entry.fileName;
        if (entry.isAnnotated && !entry.label.isEmpty()) {
            text += QString("\n类别: %1").arg(entry.label);
            item->setForeground(QBrush(QColor("#7C4DFF")));
        }
        item->setText(text);

        m_imageList->addItem(item);

        if (entry.filePath == previousPath) restoreRow = i;
    }

    QDV::Logger::debug(QString("[DBG-R4] rebuildImageList after rebuild: listCount=%1 restoreRow=%2")
              .arg(m_imageList->count()).arg(restoreRow));

    // 重新启用 viewport 更新，触发一次统一重绘
    m_imageList->setUpdatesEnabled(true);

    connect(m_imageList, &QListWidget::currentRowChanged,
            this, &TrainingInferenceView::onImageSelected);
    connect(m_imageList, &QListWidget::itemChanged,
            this, &TrainingInferenceView::onItemChanged);

    QDV::Logger::debug("[DBG-R5] rebuildImageList: connect done, restoring selection");

    if (restoreRow >= 0)
    {
        m_imageList->setCurrentRow(restoreRow);
    }
    else if (images.isEmpty())
    {
        m_imageView->clearImage();
    }
    else if (previousRow >= 0)
    {
        int newRow = qMin(previousRow, images.size() - 1);
        m_imageList->setCurrentRow(newRow);
    }

    QDV::Logger::debug(QString("[DBG-R6] rebuildImageList EXIT: listCount=%1 currentRow=%2")
              .arg(m_imageList->count()).arg(m_imageList->currentRow()));
    
    updateSelectAllButton();
    updateCategoryPanelButtons();
}

void TrainingInferenceView::onDeleteSelected()
{
    QDV::Logger::debug(QString("[DBG-A] onDeleteSelected ENTRY: currentRow=%1 listCount=%2 dataCount=%3")
              .arg(m_imageList->currentRow()).arg(m_imageList->count())
              .arg(ImageManager::instance()->imageCount()));

    // 删除所有选中的图像
    QStringList selected = ImageManager::instance()->selectedPaths();
    if (selected.isEmpty()) {
        QDV::Logger::debug("[DBG-A] No images selected, nothing to delete");
        return;
    }
    
    for (const QString& path : selected)
    {
        QDV::Logger::debug(QString("[DBG-B] onDeleteSelected removing file=%1")
                  .arg(QFileInfo(path).fileName()));
        ImageManager::instance()->removeImage(path);
    }
    
    rebuildImageList();
    
    m_imageView->clearImage();

    QDV::Logger::debug(QString("[DBG-D] onDeleteSelected EXIT: listCount=%1 dataCount=%2")
              .arg(m_imageList->count()).arg(ImageManager::instance()->imageCount()));
}

void TrainingInferenceView::onClearAll()
{
    QDV::Logger::debug(QString("[DBG-E] onClearAll ENTRY: listCount=%1 dataCount=%2")
              .arg(m_imageList->count()).arg(ImageManager::instance()->imageCount()));

    auto images = ImageManager::instance()->images();
    if (images.isEmpty()) {
        QDV::Logger::debug("[DBG-E] onClearAll: data empty, returning");
        return;
    }

    QDV::Logger::debug("[DBG-F] onClearAll: clearing data");

    // 先从数据层清空
    ImageManager::instance()->clearImages();

    QDV::Logger::debug(QString("[DBG-G] onClearAll after clearImages: dataCount=%1")
              .arg(ImageManager::instance()->imageCount()));

    // 直接清空列表
    m_imageList->clear();
    m_imageView->clearImage();

    QDV::Logger::debug(QString("[DBG-H] onClearAll after m_imageList->clear(): listCount=%1")
              .arg(m_imageList->count()));

    QDV::Logger::debug(QString("[DBG-I] onClearAll EXIT: listCount=%1 dataCount=%2")
              .arg(m_imageList->count()).arg(ImageManager::instance()->imageCount()));
}

void TrainingInferenceView::onImageRemoved(const QString& filePath)
{
    QDV::Logger::debug(QString("[DBG-RM] onImageRemoved signal: file=%1")
              .arg(QFileInfo(filePath).fileName()));
    rebuildImageList();
    updateSelectAllButton();
}

void TrainingInferenceView::onImagesCleared()
{
    QDV::Logger::debug("[DBG-CL] onImagesCleared signal received");
    m_imageList->clear();
    m_imageView->clearImage();
    updateSelectAllButton();
}

void TrainingInferenceView::onImageSelected(int row)
{
    if (row < 0)
    {
        m_imageView->clearImage();
        return;
    }

    QListWidgetItem* item = m_imageList->item(row);
    if (!item) return;

    QString path = item->toolTip();
    QPixmap pix = ImageManager::instance()->loadPixmap(path);
    if (!pix.isNull())
    {
        m_imageView->setImage(pix.toImage());
    }
}

void TrainingInferenceView::onPreviewImage(const QString& filePath)
{
    QPixmap pix = ImageManager::instance()->loadPixmap(filePath);
    if (!pix.isNull())
    {
        m_imageView->setImage(pix.toImage());
    }
}

void TrainingInferenceView::onInferenceRequested(const QString& modelPath, const QStringList& imagePaths)
{
    // 获取目标模型路径：优先使用传入参数，否则使用推理面板当前模型
    QString selectedModelPath = modelPath;
    if (selectedModelPath.isEmpty()) {
        selectedModelPath = m_inferencePanel->currentModelPath();
    }

    if (selectedModelPath.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择模型");
        m_inferencePanel->resetButtons();
        m_inferAction->setEnabled(true);
        setLightStatus(m_inferStatusLight, "red", "失败");
        return;
    }

    if (!QFileInfo::exists(selectedModelPath)) {
        QString err = QString("模型文件不存在：%1").arg(selectedModelPath);
        QDV::Logger::error(err);
        QMessageBox::warning(this, "模型加载错误", err);
        m_inferencePanel->resetButtons();
        m_inferAction->setEnabled(true);
        setLightStatus(m_inferStatusLight, "red", "失败");
        return;
    }

    // 训练推理模块当前仅支持 .onnx 格式
    if (!selectedModelPath.endsWith(".onnx", Qt::CaseInsensitive)) {
        QString err = QString("不支持的模型格式：%1\n训练推理模块当前仅支持 .onnx 模型。").arg(selectedModelPath);
        QDV::Logger::error(err);
        QMessageBox::warning(this, "模型格式错误", err);
        m_inferencePanel->resetButtons();
        m_inferAction->setEnabled(true);
        setLightStatus(m_inferStatusLight, "red", "失败");
        return;
    }

    QStringList paths = imagePaths.isEmpty()
        ? ImageManager::instance()->allPaths() : imagePaths;

    if (paths.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先导入图像");
        m_inferencePanel->resetButtons();
        m_inferAction->setEnabled(true);
        setLightStatus(m_inferStatusLight, "red", "失败");
        return;
    }

    ModelManager* mm = ModelManager::instance();
    QSize inputSize = mm->autoDetectInputSize(selectedModelPath);
    QString modelType = mm->autoDetectModelType(selectedModelPath);

    // 将模型加载到 ModelManager 缓存（以路径作为唯一标识）
    if (!mm->loadModel(selectedModelPath, selectedModelPath, inputSize)) {
        QString detail = mm->lastLoadError();
        QString err = QString("模型加载失败：%1\n\n原因：%2\n\n建议：\n1. 确认模型为 .onnx 格式\n2. 部分 ONNX 模型因 OpenCV DNN 算子兼容性无法加载，可尝试使用 ONNX Runtime 后端")
            .arg(selectedModelPath)
            .arg(detail.isEmpty() ? "未知错误" : detail);
        QDV::Logger::error(err);
        QMessageBox::warning(this, "模型加载错误", err);
        m_inferencePanel->resetButtons();
        m_inferAction->setEnabled(true);
        setLightStatus(m_inferStatusLight, "red", "失败");
        return;
    }

    InferenceEngine* engine = mm->getEngine(selectedModelPath);
    if (!engine) {
        QMessageBox::warning(this, "模型加载错误", "无法获取推理引擎实例");
        m_inferencePanel->resetButtons();
        m_inferAction->setEnabled(true);
        setLightStatus(m_inferStatusLight, "red", "失败");
        return;
    }

    m_inferencePanel->setStatus(QString("正在对 %1 张图像进行推理...").arg(paths.size()));
    m_inferencePanel->setProgress(0, paths.size(), "推理中...");

    ImageCache imageCache;  // 本地图像缓存，兼容中文路径
    QList<InferenceResult> results;
    int successCount = 0;
    int processedCount = 0;

    for (const QString& path : paths) {
        ++processedCount;
        cv::Mat image = imageCache.load(path);
        if (image.empty()) {
            InferenceResult errResult;
            errResult.imagePath = path;
            errResult.category = "图像读取失败";
            errResult.confidence = 0.0;
            QJsonObject raw;
            raw["error"] = "无法读取图像文件";
            errResult.rawOutput = raw;
            results.append(errResult);
            QDV::Logger::warn("推理跳过，无法读取图像: " + path);
            continue;
        }

        QJsonObject result;
        bool ok = false;
        try {
            ok = engine->infer(image, result);
        } catch (const std::exception& e) {
            QDV::Logger::error(QString("推理异常 [%1]: %2").arg(path).arg(e.what()));
        }

        if (!ok) {
            InferenceResult errResult;
            errResult.imagePath = path;
            errResult.category = "推理失败";
            errResult.confidence = 0.0;
            QJsonObject raw;
            raw["error"] = "模型推理失败";
            errResult.rawOutput = raw;
            results.append(errResult);
            QDV::Logger::warn("推理失败: " + path);
            continue;
        }

        InferenceResult r;
        r.imagePath = path;
        r.rawOutput = result;

        if (modelType == "yolo") {
            // 目标检测：解析检测框，取置信度最高的类别作为代表
            QJsonArray detections = result["detections"].toArray();
            if (!detections.isEmpty()) {
                double maxConf = 0.0;
                QString bestLabel;
                for (const QJsonValue& v : detections) {
                    QJsonObject det = v.toObject();
                    double conf = det["confidence"].toDouble();
                    if (conf > maxConf) {
                        maxConf = conf;
                        bestLabel = det["class_name"].toString();
                    }
                }
                r.category = bestLabel.isEmpty()
                    ? QString("检测到 %1 个目标").arg(detections.size())
                    : bestLabel;
                r.confidence = maxConf;
            } else {
                r.category = "未检测到目标";
                r.confidence = 0.0;
            }
        } else {
            // 图像分类：读取类别名称与置信度
            QString category = result["category_name"].toString();
            if (category.isEmpty()) {
                category = result["category"].toString();
            }
            if (category.isEmpty()) {
                category = "未知";
            }
            r.category = category;
            r.confidence = result["confidence"].toDouble();
        }

        results.append(r);
        ++successCount;

        // 每处理 1 张图像更新一次进度条，避免 UI 长时间无响应
        m_inferencePanel->setProgress(processedCount, paths.size(), "推理中...");
    }

    if (successCount == 0 && !results.isEmpty()) {
        QMessageBox::warning(this, "推理结果", "所有图像推理均失败，请检查模型与图像是否匹配。");
    }

    onInferenceCompleted(results);
}

void TrainingInferenceView::onInferenceCompleted(const QList<InferenceResult>& results)
{
    m_resultPanel->setResults(results);
    m_centerStack->setCurrentIndex(1);

    // 推理完成：进度条置满并给出完成反馈
    bool allFailed = !results.isEmpty() && std::all_of(results.begin(), results.end(),
        [](const InferenceResult& r) { return r.confidence <= 0.0; });

    // 恢复推理按钮状态
    m_inferencePanel->resetButtons();

    // resetButtons 会隐藏进度条并恢复"就绪"文本；这里再次显示完成状态
    if (allFailed) {
        m_inferencePanel->setProgress(0, 0, "推理失败");
    } else {
        m_inferencePanel->setProgress(results.size(), results.size(), "推理完成");
    }

    // 恢复工具栏推理按钮和指示灯
    m_inferAction->setEnabled(true);
    if (allFailed) {
        setLightStatus(m_inferStatusLight, "red", "失败");
    } else {
        setLightStatus(m_inferStatusLight, "green", "完成");
    }
}

void TrainingInferenceView::onSelectAllClicked()
{
    int selectedCount = ImageManager::instance()->selectedCount();
    int totalCount = ImageManager::instance()->imageCount();
    
    if (selectedCount == totalCount && totalCount > 0) {
        // 全部选中，现在取消全选
        ImageManager::instance()->setAllSelected(false);
    } else {
        // 全选
        ImageManager::instance()->setAllSelected(true);
    }
}

void TrainingInferenceView::onInvertSelectionClicked()
{
    auto images = ImageManager::instance()->images();
    for (const auto& entry : images) {
        ImageManager::instance()->toggleSelection(entry.filePath);
    }
}

void TrainingInferenceView::onAddToCategoryRequested(const QString& categoryId, const QString& categoryName)
{
    Q_UNUSED(categoryId);
    // 合并两种来源的选中:
    //   1) 行内 CheckBox 标记的 (传统批量选择)
    //   2) QListWidget 列表中 Ctrl/Shift 多选的 (新支持)
    QStringList selected = ImageManager::instance()->selectedPaths();
    QSet<QString> selectedSet = QSet<QString>(selected.begin(), selected.end());

    const QList<QListWidgetItem*> listItems = m_imageList->selectedItems();
    for (const QListWidgetItem* item : listItems) {
        QString path = item->toolTip();
        // tooltip 直接来源于 m_imageList, 必然是有效图像路径
        if (!path.isEmpty()) {
            selectedSet.insert(path);
        }
    }

    if (selectedSet.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择要加入类别的图像");
        return;
    }

    // 直接执行添加: 为所有选中的图像设置标签 (去除原"加入成功"确认弹窗)
    for (const QString& path : selectedSet) {
        ImageManager::instance()->setLabel(path, categoryName);
        // 同步 CheckBox 状态: 仅勾选这次被加入类别的
        ImageManager::instance()->setSelected(path, true);
    }

    // 重新构建列表以显示标签
    rebuildImageList();

    // 直接执行, 不再弹"加入成功"复选确认框
    // (如有需要, 控制台日志会记录)
    QDV::Logger::info(QString("[Category] 已将 %1 张图像加入类别 \"%2\"").arg(selectedSet.size()).arg(categoryName));
}

void TrainingInferenceView::onSelectionChanged()
{
    updateSelectAllButton();
    updateCategoryPanelButtons();

    // 更新 QListWidgetItem 原生复选框状态，避免重建整个列表
    for (int i = 0; i < m_imageList->count(); ++i) {
        QListWidgetItem* item = m_imageList->item(i);
        if (!item) continue;

        QString filePath = item->toolTip();
        bool shouldBeChecked = ImageManager::instance()->isSelected(filePath);
        Qt::CheckState targetState = shouldBeChecked ? Qt::Checked : Qt::Unchecked;

        if (item->checkState() != targetState) {
            // 临时断开 itemChanged 信号，避免循环触发
            QSignalBlocker blocker(m_imageList);
            item->setCheckState(targetState);
        }
    }
}

void TrainingInferenceView::onItemChanged(QListWidgetItem* item)
{
    if (!item) return;

    QString filePath = item->toolTip();
    if (filePath.isEmpty()) return;

    bool checked = (item->checkState() == Qt::Checked);
    ImageManager::instance()->setSelected(filePath, checked);
}

void TrainingInferenceView::updateCategoryPanelButtons()
{
    bool hasSelection = ImageManager::instance()->selectedCount() > 0;
    m_categoryPanel->updateAddToCategoryButtons(hasSelection);
}

void TrainingInferenceView::updateSelectAllButton()
{
    int selectedCount = ImageManager::instance()->selectedCount();
    int totalCount = ImageManager::instance()->imageCount();

    m_selectionLabel->setText(QString("已选: %1 / %2").arg(selectedCount).arg(totalCount));

    if (totalCount > 0 && selectedCount == totalCount)
    {
        m_selectAllBtn->setText("取消全选");
    }
    else
    {
        m_selectAllBtn->setText("全选");
    }
}

// ==================== 项目管理方法 ====================

void TrainingInferenceView::onNewProject()
{
    // 检查是否有未保存的修改
    if (m_project->isDirty()) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "新建项目",
            "当前项目有未保存的修改，是否保存？",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (reply == QMessageBox::Save) {
            onSaveProject();
            // 如果保存被取消，则中止新建
            if (m_project->isDirty()) return;
        } else if (reply == QMessageBox::Cancel) {
            return;
        }
    }

    // 清空所有数据
    ImageManager::instance()->clearImages();
    CategoryManager::instance()->clearCategories();
    m_trainingBridge->resetState();
    m_project->reset();
    rebuildImageList();
    updateTitle();

    // 弹出项目信息对话框
    QDialog dialog(this);
    dialog.setWindowTitle("新建项目");
    dialog.setModal(true);

    QFormLayout* form = new QFormLayout(&dialog);
    QLineEdit* nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText("输入项目名称");
    QLineEdit* descEdit = new QLineEdit(&dialog);
    descEdit->setPlaceholderText("输入项目描述（可选）");

    form->addRow("项目名称:", nameEdit);
    form->addRow("描述:", descEdit);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() == QDialog::Accepted) {
        m_project->setName(nameEdit->text().isEmpty() ? "未命名项目" : nameEdit->text());
        m_project->setDescription(descEdit->text());
        m_project->markDirty();
        updateTitle();
    }
}

void TrainingInferenceView::onSaveProject()
{
    // 如果没有文件路径，走另存为流程
    if (m_project->filePath().isEmpty()) {
        onSaveProjectAs();
        return;
    }

    // 获取上次保存模式（默认引用模式）
    QSettings settings("奇测科技", "QDetectVision");
    int lastMode = settings.value("training/lastSaveMode", 0).toInt();
    ProjectSerializer::SaveMode mode = static_cast<ProjectSerializer::SaveMode>(lastMode);

    // 收集快照到项目
    collectSnapshotsToProject();

    // 暂停监控器，避免保存过程中 onTick 触发 UI 更新与保存操作产生竞态
    m_isLoadingOrSaving = true;
    QDV::TrainingInferenceMonitor::instance()->stop();
    QDV::Logger::info("[ProjectSave] 开始保存项目: " + m_project->filePath());

    // 显示进度对话框
    m_progressDialog = new QProgressDialog("正在保存项目...", "取消", 0, 100, this);
    m_progressDialog->setWindowTitle("保存项目");
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(500);
    m_progressDialog->show();

    // 异步保存
    m_serializer->saveAsync(m_project->filePath(), m_project, mode);
}

void TrainingInferenceView::onSaveProjectAs()
{
    // 弹出保存模式选择对话框
    QDialog modeDialog(this);
    modeDialog.setWindowTitle("选择保存模式");
    modeDialog.setModal(true);

    QVBoxLayout* layout = new QVBoxLayout(&modeDialog);
    QRadioButton* refRadio = new QRadioButton("仅引用（轻量，同设备使用）", &modeDialog);
    QRadioButton* bundleRadio = new QRadioButton("完整打包（含图像，跨设备兼容）", &modeDialog);

    // 读取上次选择
    QSettings settings("奇测科技", "QDetectVision");
    int lastMode = settings.value("training/lastSaveMode", 0).toInt();
    if (lastMode == 1) {
        bundleRadio->setChecked(true);
    } else {
        refRadio->setChecked(true);
    }

    layout->addWidget(new QLabel("请选择保存模式:", &modeDialog));
    layout->addWidget(refRadio);
    layout->addWidget(bundleRadio);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &modeDialog);
    connect(buttons, &QDialogButtonBox::accepted, &modeDialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &modeDialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (modeDialog.exec() != QDialog::Accepted) {
        return;
    }

    ProjectSerializer::SaveMode mode = refRadio->isChecked() ?
        ProjectSerializer::ModeReference : ProjectSerializer::ModeBundled;

    // 记忆选择
    settings.setValue("training/lastSaveMode", static_cast<int>(mode));

    // 弹出文件对话框
    QString filter = mode == ProjectSerializer::ModeBundled ?
        "训练项目文件 (*.qdvproj)" : "训练项目文件 (*.qdvproj)";
    QString filePath = QFileDialog::getSaveFileName(
        this, "保存项目", QString(), filter);

    if (filePath.isEmpty()) return;

    // 确保文件扩展名正确
    if (!filePath.endsWith(".qdvproj", Qt::CaseInsensitive)) {
        filePath += ".qdvproj";
    }

    // 更新项目文件路径
    m_project->setFilePath(filePath);
    m_project->updateModifiedTime();

    // 收集快照
    collectSnapshotsToProject();

    // 暂停监控器，避免保存过程中 onTick 触发 UI 更新与保存操作产生竞态
    m_isLoadingOrSaving = true;
    QDV::TrainingInferenceMonitor::instance()->stop();
    QDV::Logger::info("[ProjectSave] 开始保存项目(另存为): " + filePath);

    // 显示进度对话框
    m_progressDialog = new QProgressDialog("正在保存项目...", "取消", 0, 100, this);
    m_progressDialog->setWindowTitle("保存项目");
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(500);
    m_progressDialog->show();

    // 异步保存
    m_serializer->saveAsync(filePath, m_project, mode);
}

void TrainingInferenceView::onLoadProject()
{
    // 检查当前是否有数据
    if (ImageManager::instance()->imageCount() > 0 ||
        CategoryManager::instance()->categoryCount() > 0) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "加载项目",
            "当前数据将被替换，是否继续？",
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
    }

    // 弹出文件对话框
    QString filePath = QFileDialog::getOpenFileName(
        this, "加载项目", QString(), "训练项目文件 (*.qdvproj)");

    if (filePath.isEmpty()) return;

    // 暂停监控器，避免加载过程中 onTick 触发 UI 更新与加载操作产生竞态
    m_isLoadingOrSaving = true;
    QDV::TrainingInferenceMonitor::instance()->stop();
    QDV::Logger::info("[ProjectLoad] 开始加载项目: " + filePath);

    // 显示进度对话框
    m_progressDialog = new QProgressDialog("正在加载项目...", "取消", 0, 100, this);
    m_progressDialog->setWindowTitle("加载项目");
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(500);
    m_progressDialog->show();

    // 异步加载
    m_serializer->loadAsync(filePath);
}

void TrainingInferenceView::onRecentProject_triggered()
{
    // 由 QAction 触发，获取数据中的文件路径
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action) return;

    QString filePath = action->data().toString();
    if (filePath.isEmpty()) return;

    if (!QFile::exists(filePath)) {
        QMessageBox::warning(this, "项目不存在",
            QString("项目文件不存在: %1\n已从最近列表中移除").arg(filePath));
        m_recentProjects.removeAll(filePath);
        saveRecentProjects();
        updateRecentProjectsMenu();
        return;
    }

    // 检查当前是否有数据
    if (ImageManager::instance()->imageCount() > 0 ||
        CategoryManager::instance()->categoryCount() > 0) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "加载项目",
            "当前数据将被替换，是否继续？",
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
    }

    // 暂停监控器，避免加载过程中 onTick 触发 UI 更新与加载操作产生竞态
    m_isLoadingOrSaving = true;
    QDV::TrainingInferenceMonitor::instance()->stop();
    QDV::Logger::info("[ProjectLoad] 开始加载最近项目: " + filePath);

    // 显示进度对话框
    m_progressDialog = new QProgressDialog("正在加载项目...", "取消", 0, 100, this);
    m_progressDialog->setWindowTitle("加载项目");
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(500);
    m_progressDialog->show();

    // 异步加载
    m_serializer->loadAsync(filePath);
}

void TrainingInferenceView::onSaveProgress(int percent, const QString& stage)
{
    if (m_progressDialog) {
        m_progressDialog->setValue(percent);
        m_progressDialog->setLabelText(stage);
    }
}



void TrainingInferenceView::onSaveFinished(const QString& filePath, bool success, const QString& message)
{
    QDV::Logger::info(QString("[ProjectSave] 保存完成: success=%1 file=%2").arg(success).arg(filePath));

    // 关键修复：进度对话框改用 deleteLater() 延迟销毁（而不是同步 delete）。
    // 原因：工作线程发出的 loadProgress/saveProgress 是排队的跨线程 queued 事件，
    // 若在此同步 delete，晚到的 queued 事件仍会触发 onSaveProgress → QProgressDialog::setValue，
    // 访问到已释放的对话框，导致 ACCESS_VIOLATION 崩溃（UAF）。
    // deleteLater 保证对象在当前事件批次处理期间仍存活，挂起事件排空后再安全释放，
    // QPointer 随后自动置空，后续 onSaveProgress 判空安全返回。
    if (m_progressDialog) {
        m_progressDialog->close();
        m_progressDialog->deleteLater();
        // QPointer 自动置空，无需手动 m_progressDialog = nullptr
    }

    // 恢复监控器
    m_isLoadingOrSaving = false;
    QDV::TrainingInferenceMonitor::instance()->start();

    // 将 UI 更新与消息框延迟到下一帧，确保保存完成的同步路径完全退出
    QTimer::singleShot(0, this, [this, filePath, success, message]() {
        if (success) {
            m_project->markClean();
            m_project->appendHistory("save", QString("保存到 %1").arg(QFileInfo(filePath).fileName()));
            addRecentProject(filePath);
            updateTitle();
            QMessageBox::information(this, "保存成功", message);
        } else {
            QMessageBox::critical(this, "保存失败", message);
        }
    });
}

void TrainingInferenceView::onLoadProgress(int percent, const QString& stage)
{
    if (m_progressDialog) {
        m_progressDialog->setValue(percent);
        m_progressDialog->setLabelText(stage);
    }
}

void TrainingInferenceView::onLoadFinished(const QString& filePath, bool success,
                                            const QString& message,
                                            const QJsonObject& projectJson,
                                            const QStringList& missingImages)
{
    QDV::Logger::info(QString("[ProjectLoad] 加载完成: success=%1 file=%2").arg(success).arg(filePath));

    // 关键修复：进度对话框改用 deleteLater() 延迟销毁（而不是同步 delete）。
    // 原因：工作线程（QtConcurrent）发出的 loadProgress 是排队到本线程的 queued 事件，
    // 加载完成时这些事件可能尚未全部处理。若在此同步 delete，晚到的 queued 事件仍会
    // 触发 onLoadProgress → QProgressDialog::setValue，访问到已释放的对话框，
    // 导致 ACCESS_VIOLATION 崩溃（UAF，调用栈位于 onLoadProgress 的 setValue 处）。
    // deleteLater 保证对象在当前事件批次处理期间仍存活，挂起事件排空后再安全释放，
    // QPointer 随后自动置空，后续 onLoadProgress 判空安全返回。
    if (m_progressDialog) {
        m_progressDialog->close();
        m_progressDialog->deleteLater();
        // QPointer 自动置空
    }

    if (!success) {
        // 恢复监控器
        m_isLoadingOrSaving = false;
        QDV::TrainingInferenceMonitor::instance()->start();
        QTimer::singleShot(0, this, [this, message]() {
            QMessageBox::critical(this, "加载失败", message);
        });
        return;
    }

    QDV::Logger::info("[ProjectLoad] 开始解析项目JSON...");
    // 关键修复：在主线程用 fromJson 填充现有 m_project 对象
    QString parseErr;
    if (!m_project->fromJson(projectJson, &parseErr)) {
        m_isLoadingOrSaving = false;
        QDV::TrainingInferenceMonitor::instance()->start();
        QTimer::singleShot(0, this, [this, parseErr]() {
            QMessageBox::critical(this, "加载失败",
                QString("项目数据解析失败: %1").arg(parseErr));
        });
        return;
    }

    QDV::Logger::info("[ProjectLoad] 开始恢复数据到各Manager...");
    // 恢复数据到各 Manager（图像、类别、训练参数）
    restoreDataFromProject(m_project);
    QDV::Logger::info("[ProjectLoad] 数据恢复完成");

    m_project->setFilePath(filePath);
    m_project->markClean();
    m_project->appendHistory("load", QString("从 %1 加载").arg(QFileInfo(filePath).fileName()));

    // 关键修复：将 rebuildImageList 延迟到下一帧事件循环。
    // 不再在 rebuildImageList 中调用 processEvents，避免事件循环递归。
    // Monitor 已在加载开始时暂停，不会在此期间触发 onTick。
    QTimer::singleShot(0, this, [this, filePath, message, missingImages]() {
        QDV::Logger::info("[ProjectLoad] 开始重建图像列表...");
        rebuildImageList();
        QDV::Logger::info("[ProjectLoad] 图像列表重建完成");
        updateTitle();
        addRecentProject(filePath);

        // 恢复监控器（在 rebuildImageList 完成后）
        m_isLoadingOrSaving = false;
        QDV::TrainingInferenceMonitor::instance()->start();
        QDV::Logger::info("[ProjectLoad] 监控器已恢复");

        // 缺失图像提示/加载成功提示也在重建完成后再显示
        if (!missingImages.isEmpty()) {
            QStringList missing = missingImages;
            QTimer::singleShot(0, this, [this, missing]() {
                QMessageBox::StandardButton reply = QMessageBox::question(
                    this, "图像路径失效",
                    QString("%1 张图像路径失效，是否重新定位？").arg(missing.size()),
                    QMessageBox::Yes | QMessageBox::No);

                if (reply == QMessageBox::Yes) {
                    QString newDir = QFileDialog::getExistingDirectory(
                        this, "选择图像所在目录");

                    if (!newDir.isEmpty()) {
                        QList<ImageEntrySnapshot> images = m_project->imagesSnapshot();
                        QStringList relocated = ProjectSerializer::relocateMissingImages(images, newDir);

                        if (!relocated.isEmpty()) {
                            ImageManager::instance()->importFromSnapshot(images);
                            m_project->setImagesSnapshot(images);
                            rebuildImageList();
                            QMessageBox::information(this, "重定位完成",
                                QString("成功重定位 %1 张图像").arg(relocated.size()));
                        } else {
                            QMessageBox::warning(this, "重定位失败",
                                "在所选目录中未找到匹配的图像文件");
                        }
                    }
                }
            });
        } else {
            QString msg = message;
            QTimer::singleShot(0, this, [this, msg]() {
                QMessageBox::information(this, "加载成功", msg);
            });
        }
    });

}

void TrainingInferenceView::markProjectDirty()
{
    m_project->markDirty();
    updateTitle();
}

void TrainingInferenceView::updateTitle()
{
    QString name = m_project->name().isEmpty() ? "未命名" : m_project->name();
    QString dirtyMark = m_project->isDirty() ? "*" : "";
    QString title = QString("[%1%2] - 训练推理").arg(name).arg(dirtyMark);

    // 如果是独立窗口则设置窗口标题
    // 如果不是，尝试设置属性让父窗口可以读取
    setWindowTitle(title);
    emit viewChanged(title);
}

void TrainingInferenceView::loadRecentProjects()
{
    QSettings settings("奇测科技", "QDetectVision");
    QString jsonStr = settings.value("training/recentProjects").toString();

    m_recentProjects.clear();
    if (!jsonStr.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        QJsonArray arr = doc.array();
        for (const auto& val : arr) {
            QString path = val.toString();
            if (QFile::exists(path)) {
                m_recentProjects.append(path);
            }
        }
    }
}

void TrainingInferenceView::saveRecentProjects()
{
    QJsonArray arr;
    for (const QString& path : m_recentProjects) {
        arr.append(path);
    }

    QJsonDocument doc(arr);
    QSettings settings("奇测科技", "QDetectVision");
    settings.setValue("training/recentProjects", QString::fromUtf8(doc.toJson()));
}

void TrainingInferenceView::addRecentProject(const QString& path)
{
    // 移除已存在的同名路径
    m_recentProjects.removeAll(path);
    // 添加到最前
    m_recentProjects.prepend(path);
    // 限制数量
    while (m_recentProjects.size() > MAX_RECENT_PROJECTS) {
        m_recentProjects.removeLast();
    }
    saveRecentProjects();
    updateRecentProjectsMenu();
}

void TrainingInferenceView::updateRecentProjectsMenu()
{
    if (!m_recentProjectsMenu) return;

    m_recentProjectsMenu->clear();

    if (m_recentProjects.isEmpty()) {
        QAction* emptyAction = m_recentProjectsMenu->addAction("(无最近项目)");
        emptyAction->setEnabled(false);
        return;
    }

    for (const QString& path : m_recentProjects) {
        QString displayText = QFileInfo(path).fileName();
        QAction* action = m_recentProjectsMenu->addAction(displayText);
        action->setToolTip(path);
        action->setData(path);
        connect(action, &QAction::triggered, this, &TrainingInferenceView::onRecentProject_triggered);
    }
}

void TrainingInferenceView::collectSnapshotsToProject()
{
    // 从各 Manager 收集快照
    m_project->setImagesSnapshot(ImageManager::instance()->toSnapshot());
    m_project->setCategoriesSnapshot(CategoryManager::instance()->exportToJsonObject());

    // 类型转换：QDV::TrainingParamsSnapshot -> 全局 TrainingParamsSnapshot
    QDV::TrainingParamsSnapshot bridgeParams = m_trainingBridge->paramsSnapshot();
    TrainingParamsSnapshot params;
    params.modelType = bridgeParams.modelType;
    params.numEpochs = bridgeParams.numEpochs;
    params.batchSize = bridgeParams.batchSize;
    params.learningRate = bridgeParams.learningRate;
    params.valSplit = bridgeParams.valSplit;

    QDV::TrainingStateSnapshot bridgeState = m_trainingBridge->stateSnapshot();
    TrainingStateSnapshot state;
    state.hasTrained = bridgeState.hasTrained;
    state.lastTrainedAt = bridgeState.lastTrainedAt;
    state.lastMetrics = bridgeState.lastMetrics;
    state.onnxPath = bridgeState.onnxPath;

    m_project->setTrainingSnapshot(params, state);
    m_project->updateModifiedTime();
}

void TrainingInferenceView::restoreDataFromProject(TrainingProject* project)
{
    // 使用 QSignalBlocker 阻塞各 Manager 信号，避免加载过程中反复标记 dirty
    QSignalBlocker imageBlocker(ImageManager::instance());
    QSignalBlocker categoryBlocker(CategoryManager::instance());

    // 恢复图像列表
    ImageManager::instance()->importFromSnapshot(project->imagesSnapshot());

    // 恢复类别
    CategoryManager::instance()->clearCategories();
    CategoryManager::instance()->importFromJsonObject(project->categoriesSnapshot());

    // 恢复训练参数和状态（类型转换：全局 TrainingParamsSnapshot -> QDV::TrainingParamsSnapshot）
    TrainingParamsSnapshot projParams = project->trainingParams();
    QDV::TrainingParamsSnapshot bridgeParams;
    bridgeParams.modelType = projParams.modelType;
    bridgeParams.numEpochs = projParams.numEpochs;
    bridgeParams.batchSize = projParams.batchSize;
    bridgeParams.learningRate = projParams.learningRate;
    bridgeParams.valSplit = projParams.valSplit;

    TrainingStateSnapshot projState = project->trainingState();
    QDV::TrainingStateSnapshot bridgeState;
    bridgeState.hasTrained = projState.hasTrained;
    bridgeState.lastTrainedAt = projState.lastTrainedAt;
    bridgeState.lastMetrics = projState.lastMetrics;
    bridgeState.onnxPath = projState.onnxPath;

    m_trainingBridge->applySnapshot(bridgeParams, bridgeState);

    // 关键修复：由于上方 QSignalBlocker 阻塞了 CategoryManager 信号，
    // CategoryPanel::refreshTree() 不会通过 categoryCreated/Updated/Deleted 信号被触发，
    // 导致加载项目后类别树为空（类别信息丢失）。这里手动刷新一次类别面板。
    if (m_categoryPanel) {
        m_categoryPanel->refreshTree();
    }
}

// ==================== 训练推理监控方法 ====================

void TrainingInferenceView::onMonitorSnapshotReady(const QDV::ProcessSnapshot& snapshot)
{
    // 更新 CPU 占用
    m_cpuLabel->setText(QString("CPU: %1%").arg(QString::number(snapshot.cpuPercent, 'f', 1)));

    // 更新内存占用（转换为 MB，保留 1 位小数）
    double memMB = snapshot.workingSetBytes / 1024.0 / 1024.0;
    m_memoryLabel->setText(QString("内存: %1 MB").arg(QString::number(memMB, 'f', 1)));

    // GPU 状态：优先显示名称+利用率+显存
    if (snapshot.gpuAvailable) {
        QString gpuText = QString("GPU: %1 %2% %3/%4 MB")
                              .arg(snapshot.gpuName)
                              .arg(snapshot.gpuUtilizationPercent)
                              .arg(snapshot.gpuMemoryUsedMB)
                              .arg(snapshot.gpuMemoryTotalMB);
        m_gpuLabel->setText(gpuText);
        m_gpuLabel->setToolTip(QString("温度: %1°C").arg(snapshot.gpuTemperatureC));
    } else {
        m_gpuLabel->setText("GPU: --");
        m_gpuLabel->setToolTip(snapshot.gpuError.isEmpty() ? QString() : snapshot.gpuError);
    }

    // 训练运行时间：已运行 / 剩余
    auto formatDuration = [](qint64 seconds) -> QString {
        if (seconds < 0) {
            return "--";
        }
        qint64 hours = seconds / 3600;
        qint64 minutes = (seconds % 3600) / 60;
        qint64 secs = seconds % 60;
        if (hours > 0) {
            return QString("%1:%2:%3")
                .arg(hours)
                .arg(minutes, 2, 10, QChar('0'))
                .arg(secs, 2, 10, QChar('0'));
        }
        return QString("%1:%2")
            .arg(minutes, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    };

    if (snapshot.serverJobStatus == "running" && snapshot.trainingElapsedSeconds >= 0) {
        m_runtimeLabel->setText(
            QString("已运行: %1 / 剩余: %2")
                .arg(formatDuration(snapshot.trainingElapsedSeconds))
                .arg(formatDuration(snapshot.trainingRemainingSeconds)));
    } else {
        m_runtimeLabel->setText("已运行: -- / 剩余: --");
    }

    // 训练服务在线状态与颜色：本地训练 running 时也视为在线
    if (snapshot.serverReachable) {
        m_trainingServiceLabel->setText("训练服务: 在线");
        m_trainingServiceLabel->setStyleSheet("font-size: 12px; color: #4CAF50;");
    } else {
        m_trainingServiceLabel->setText("训练服务: 离线");
        m_trainingServiceLabel->setStyleSheet("font-size: 12px; color: #F44336;");
    }

    // 服务延迟：可达时显示毫秒，本地训练显示 0 ms，否则显示不可达
    if (snapshot.serverReachable && snapshot.serverLatencyMs >= 0) {
        m_latencyLabel->setText(QString("服务延迟: %1 ms").arg(snapshot.serverLatencyMs));
    } else {
        m_latencyLabel->setText("服务延迟: 不可达");
    }

    // 已加载模型数量
    m_loadedModelsLabel->setText(QString("已加载模型: %1").arg(snapshot.loadedModelCount));

    // 最近推理延迟
    if (snapshot.lastInferenceLatencyMs >= 0.0) {
        m_inferenceLatencyLabel->setText(
            QString("最近推理延迟: %1 ms").arg(QString::number(snapshot.lastInferenceLatencyMs, 'f', 1)));
    } else {
        m_inferenceLatencyLabel->setText("最近推理延迟: --");
    }

    // 顶部状态文本：running 表示运行中，其余显示就绪
    if (snapshot.serverJobStatus == "running") {
        m_monitorStatusLabel->setText("运行中");

        // 左侧补充 epoch/loss/acc 等实时训练指标
        QString metricsText;
        if (snapshot.trainingTotalEpochs > 0) {
            metricsText += QString("Epoch %1/%2").arg(snapshot.trainingEpoch).arg(snapshot.trainingTotalEpochs);
        }
        if (snapshot.trainingLoss >= 0.0) {
            metricsText += QString(" | Loss %1").arg(QString::number(snapshot.trainingLoss, 'f', 4));
        }
        if (snapshot.trainingAccuracy >= 0.0) {
            metricsText += QString(" | Acc %1%").arg(QString::number(snapshot.trainingAccuracy * 100, 'f', 2));
        }
        if (snapshot.valAccuracy >= 0.0) {
            metricsText += QString(" | Val %1%").arg(QString::number(snapshot.valAccuracy * 100, 'f', 2));
        }
        m_trainingMetricsLabel->setText(metricsText);
    } else {
        m_monitorStatusLabel->setText("就绪");
        m_trainingMetricsLabel->setText("");
    }
}

void TrainingInferenceView::onAnomalyDetected(const QDV::Anomaly& anomaly)
{
    // 新异常插入队首，最多保留 20 条
    m_recentAnomalies.prepend(anomaly);
    while (m_recentAnomalies.size() > 20) {
        m_recentAnomalies.removeLast();
    }

    // 更新最近异常标签
    QString text = QString("最近异常: [%1] %2").arg(anomaly.category, anomaly.message);
    m_latestAnomalyLabel->setText(text);

    // 根据严重级别设置颜色：warning 橙色，error/critical 红色
    QString color = "#aaa";
    if (anomaly.severity == "warning") {
        color = "#FFA726";
    } else if (anomaly.severity == "error" || anomaly.severity == "critical") {
        color = "#F44336";
    }
    m_latestAnomalyLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(color));
}

void TrainingInferenceView::onViewHistory()
{
    // 弹出异常历史对话框，显示最近 20 条异常详情
    QDialog dialog(this);
    dialog.setWindowTitle("异常历史");
    dialog.setMinimumSize(560, 400);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QListWidget* list = new QListWidget(&dialog);
    list->setStyleSheet(R"(
        QListWidget {
            background-color: #252525;
            border: 1px solid #444;
            color: #e0e0e0;
        }
        QListWidget::item {
            padding: 6px;
            border-bottom: 1px solid #333;
        }
    )");

    for (const auto& anomaly : m_recentAnomalies) {
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(anomaly.timestamp);
        QString timeStr = dt.toString("yyyy-MM-dd hh:mm:ss");
        QString itemText = QString("[%1] [%2] %3: %4")
            .arg(timeStr, anomaly.severity.toUpper(), anomaly.category, anomaly.message);
        QListWidgetItem* item = new QListWidgetItem(itemText);

        // 按级别着色列表项
        if (anomaly.severity == "warning") {
            item->setForeground(QColor("#FFA726"));
        } else if (anomaly.severity == "error" || anomaly.severity == "critical") {
            item->setForeground(QColor("#F44336"));
        }
        list->addItem(item);
    }

    layout->addWidget(list);

    QPushButton* closeBtn = new QPushButton("关闭", &dialog);
    closeBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #3d3d3d;
            color: #e0e0e0;
            border: 1px solid #555;
            border-radius: 3px;
            padding: 4px 12px;
            font-size: 12px;
        }
        QPushButton:hover { background-color: #555; }
        QPushButton:pressed { background-color: #2a2a2a; }
    )");
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(closeBtn, 0, Qt::AlignRight);

    dialog.exec();
}