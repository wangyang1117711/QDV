// ============================================================================
// 零样本检测视图 - 实现文件
// 作为 CentralWindow 第 9 个视图（索引 8），与 EditView 同级。
// 复用 ZeroShotKit 的 QWidget UI（ZeroShotPanel + ZeroShotResultPanel），
// 通过 zsu::Kit 门面承载零样本推理能力。
// 文件组织规范与编辑模块一致：头文件在 include/UI/，源文件在 src/UI/。
// ============================================================================

#include "ZeroShotDetectView.h"
#include "ZeroShotPanel.h"          // QDVMini::ZeroShotPanel
#include "ZeroShotResultPanel.h"    // QDVMini::ZeroShotResultPanel
#include "ZeroShotKit/ModelNotesManager.h"  // zsu::ModelNotesManager / zsu::ModelNote
#include "ZeroShotKit/ZeroShotEngine.h"     // zsu::ZeroShotEngine (setUseQuantizedModel)
#include "ZeroShotKit/ZeroShotTypes.h"      // zsu::StabilityConfig / ZeroShotModelType
#include "Core/PromptLibrary.h"             // v2.0 阶段二 Task 7：共享提示词库
#include "AI/ModelSourceManager.h"          // v2.0 阶段七 Task 18：模型源监控
#include "AI/TrainingBridge.h"              // v2.0 阶段五 Task 13：训练桥接
#include "UI/DataCollectionWizard.h"        // v2.0 阶段五 Task 13：数据收集向导
#include "UI/ResultExportDialog.h"          // v2.0 阶段六 Task 14：结果导出对话框
#include "OperatorLibrary/OperatorLibraryController.h"  // 推断专用模型算子 type
#include "Core/Logger.h"                    // QDV::Logger

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTextEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QShowEvent>
#include <QTimer>
#include <QDateTime>

#include <opencv2/opencv.hpp>

// ----------------------------------------------------------------------------
// 辅助函数：QStringList → HTML <li> 列表
// ----------------------------------------------------------------------------
static QString joinStringListAsHtmlList(const QStringList& list) {
    if (list.isEmpty()) return QString();
    QString html;
    for (const QString& item : list) {
        html += QStringLiteral("<li>") + item.toHtmlEscaped() + QStringLiteral("</li>");
    }
    return html;
}

// ============================================================================
// 构造 / 析构
// ============================================================================
ZeroShotDetectView::ZeroShotDetectView(QWidget* parent) : QWidget(parent) {
    // 1. 创建 Kit 实例（parent=this，Qt 自动回收）
    m_kit = new zsu::Kit(this);

    // 2. 创建 UI 面板（parent=this）
    m_panel = new QDVMini::ZeroShotPanel(this);
    m_resultPanel = new QDVMini::ZeroShotResultPanel(this);

    // 3. 注入引擎/管理器到配置面板
    m_panel->setEngine(m_kit->engine());
    m_panel->setModelNotesManager(m_kit->notesManager());
    m_panel->setBadCaseRecorder(m_kit->badCaseRecorder());

    // v2.0 阶段五 Task 13：创建第二层训练桥接实例（用于训练兑底流程）
    // 不复用 TrainingInferenceView 的桥接，保持模块独立性
    m_trainingBridge = new QDV::TrainingBridge(this);

    // 4. 构建布局与信号槽
    setupUI();
    connectSignals();
    updateModelStatus();
    updateNotesView();

    // v2.0 阶段二 Task 7：初始化共享提示词库 → ZeroShotPanel 目标类型下拉
    refreshTargetTypeEntries();

    // v2.0 阶段七 Task 18：初始化模型源监控 → ZeroShotPanel 具体模型下拉
    refreshModelEntries();
    // 启动文件系统监控，模型增删自动刷新下拉
    QDV::ModelSourceManager::instance()->startWatching();
}

ZeroShotDetectView::~ZeroShotDetectView() = default;

// ============================================================================
// UI 构建：顶部工具栏 + 三栏 QSplitter
// ============================================================================
void ZeroShotDetectView::setupUI() {
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // --- v2.0 阶段三 Task 9：定位调整提示横幅 ---
    // 本 tab 定位为"零样本模型调试/批量评估"专用，生产链路改用 ZeroShotDetect 算子。
    // 横幅浅色背景、居中、不遮挡现有内容；保留所有调试能力。
    m_banner = new QLabel(this);
    m_banner->setText(QStringLiteral(
        "💡 生产链路请使用 ZeroShotDetect 算子（在方案编辑中拖入），"
        "本 tab 用于模型调试/批量评估"));
    m_banner->setAlignment(Qt::AlignCenter);
    m_banner->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  background-color: #FFF8E1;"            // 浅琥珀色背景
        "  color: #5D4037;"                       // 深棕色文字
        "  padding: 8px 12px;"
        "  font-size: 12px;"
        "  border-bottom: 1px solid #FFB300;"     // 底部细分隔线
        "}"));
    m_banner->setWordWrap(true);
    root->addWidget(m_banner);

    // --- 顶部工具栏：[加载图像][批量目录][停止] [进度条] [状态标签] ---
    QHBoxLayout* toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(8, 6, 8, 6);
    toolbar->setSpacing(6);

    m_loadImageBtn = new QPushButton(QStringLiteral("加载图像"), this);
    m_batchBtn     = new QPushButton(QStringLiteral("批量目录"), this);
    m_stopBtn      = new QPushButton(QStringLiteral("停止"), this);
    m_progressBar  = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(8);
    m_statusLabel  = new QLabel(QStringLiteral("就绪"), this);
    m_statusLabel->setMinimumWidth(180);

    // v2.0 阶段六 Task 14：导出结果按钮
    m_exportBtn    = new QPushButton(QStringLiteral("导出结果"), this);
    // v2.0 阶段六 Task 15：批量推理按钮（选目录后立即推理）
    m_batchInferBtn = new QPushButton(QStringLiteral("批量推理"), this);

    toolbar->addWidget(m_loadImageBtn);
    toolbar->addWidget(m_batchBtn);
    toolbar->addWidget(m_batchInferBtn);
    toolbar->addWidget(m_stopBtn);
    toolbar->addWidget(m_exportBtn);
    toolbar->addWidget(m_progressBar, 1);
    toolbar->addWidget(m_statusLabel);
    root->addLayout(toolbar);

    // --- 三栏 QSplitter ---
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // 左栏：ZeroShotPanel 配置面板（固定最小 360px）
    m_panel->setMinimumWidth(360);
    m_splitter->addWidget(m_panel);

    // 中栏：ZeroShotResultPanel 结果面板（自适应）
    m_resultPanel->setMinimumWidth(400);
    m_splitter->addWidget(m_resultPanel);

    // 右栏：QTextEdit 模型注意事项显示（固定最小 280px）
    m_notesView = new QTextEdit(this);
    m_notesView->setReadOnly(true);
    m_notesView->setPlaceholderText(QStringLiteral("模型注意事项..."));
    m_notesView->setMinimumWidth(280);
    m_splitter->addWidget(m_notesView);

    // 初始拉伸比例：左栏不拉伸 / 中栏拉伸 / 右栏不拉伸
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);

    root->addWidget(m_splitter, 1);
}

// ============================================================================
// 信号槽连接
// ============================================================================
void ZeroShotDetectView::connectSignals() {
    // --- Panel 信号 → 业务槽 ---
    connect(m_panel, &QDVMini::ZeroShotPanel::modelLoadRequested,
            this, &ZeroShotDetectView::onModelLoadRequested);
    connect(m_panel, &QDVMini::ZeroShotPanel::inferenceRequested,
            this, &ZeroShotDetectView::onInferenceRequested);
    connect(m_panel, &QDVMini::ZeroShotPanel::inferenceAllRequested,
            this, &ZeroShotDetectView::onInferenceAllRequested);
    connect(m_panel, &QDVMini::ZeroShotPanel::stopRequested,
            this, &ZeroShotDetectView::onStopRequested);
    connect(m_panel, &QDVMini::ZeroShotPanel::addNormalSampleRequested,
            this, &ZeroShotDetectView::onAddNormalSampleRequested);
    connect(m_panel, &QDVMini::ZeroShotPanel::removeLastNormalSampleRequested,
            this, &ZeroShotDetectView::onRemoveLastNormalSampleRequested);
    connect(m_panel, &QDVMini::ZeroShotPanel::clearNormalSamplesRequested,
            this, &ZeroShotDetectView::onClearNormalSamplesRequested);
    connect(m_panel, &QDVMini::ZeroShotPanel::humanReviewToggled,
            this, &ZeroShotDetectView::onHumanReviewToggled);
    connect(m_panel, &QDVMini::ZeroShotPanel::settingsChanged,
            this, &ZeroShotDetectView::onSettingsChanged);

    // v2.0 阶段二 Task 7：共享提示词库桥接
    connect(m_panel, &QDVMini::ZeroShotPanel::customTargetTypeRequested,
            this, &ZeroShotDetectView::onCustomTargetTypeRequested);
    connect(m_panel, &QDVMini::ZeroShotPanel::targetTypeSelectionChanged,
            this, &ZeroShotDetectView::onTargetTypeSelectionChanged);
    // PromptLibrary 变化时自动刷新目标类型下拉
    connect(QDV::PromptLibrary::instance(), &QDV::PromptLibrary::libraryChanged,
            this, &ZeroShotDetectView::refreshTargetTypeEntries);

    // v2.0 阶段五 Task 13：第二层 训练兑底流程桥接
    connect(m_panel, &QDVMini::ZeroShotPanel::specializedTrainingRequested,
            this, &ZeroShotDetectView::onSpecializedTrainingRequested);
    // 训练桥接信号 → 状态显示与标记专用模型
    if (m_trainingBridge) {
        connect(m_trainingBridge, &QDV::TrainingBridge::trainingProgress,
                this, [this](const QVariantMap& progress) {
            int epoch = progress.value("epoch").toInt();
            int total = progress.value("totalEpochs").toInt();
            if (total > 0) {
                m_progressBar->setRange(0, total);
                m_progressBar->setValue(epoch);
                m_statusLabel->setText(
                    QStringLiteral("训练中: epoch %1/%2").arg(epoch).arg(total));
            }
        });
        connect(m_trainingBridge, &QDV::TrainingBridge::trainingCompleted,
                this, &ZeroShotDetectView::onSpecializedTrainingCompleted);
        connect(m_trainingBridge, &QDV::TrainingBridge::trainingError,
                this, [this](const QString& phase, const QString& message) {
            m_statusLabel->setText(QStringLiteral("训练失败: ") + message);
            m_progressBar->setRange(0, 100);
            m_progressBar->setValue(0);
            QMessageBox::warning(this, QStringLiteral("训练失败"),
                QStringLiteral("[%1] %2").arg(phase, message));
            m_pendingSpecializedPrompt.clear();
        });
        connect(m_trainingBridge, &QDV::TrainingBridge::logOutput,
                this, [this](const QString& msg) {
            QDV::Logger::info(QStringLiteral("[ZeroShotDetectView 训练] %1").arg(msg));
        });
    }

    // v2.0 阶段七 Task 18：模型源监控桥接
    // 用户在具体模型下拉中选中某项 → 记录日志（路径已由 Panel 自动填入 m_modelPathEdit）
    connect(m_panel, &QDVMini::ZeroShotPanel::modelSelectionChanged,
            this, &ZeroShotDetectView::onModelSelectionChanged);
    // ModelSourceManager 模型清单变化 → 自动刷新具体模型下拉
    connect(QDV::ModelSourceManager::instance(),
            &QDV::ModelSourceManager::modelListChanged,
            this, [this](const QVariantList&){ this->refreshModelEntries(); });
    // 源路径状态变化 → 更新 LM Studio 提示
    connect(QDV::ModelSourceManager::instance(),
            &QDV::ModelSourceManager::sourceStatusChanged,
            this, [this](const QString&, const QString&){ this->refreshModelEntries(); });

    // --- Kit 信号 → 结果面板槽 ---
    connect(m_kit, &zsu::Kit::inferenceCompleted,
            this, &ZeroShotDetectView::onInferenceCompleted);
    connect(m_kit, &zsu::Kit::batchCompleted,
            this, &ZeroShotDetectView::onBatchCompleted);
    connect(m_kit, &zsu::Kit::progressUpdated,
            this, &ZeroShotDetectView::onProgressUpdated);
    connect(m_kit, &zsu::Kit::errorOccurred,
            this, &ZeroShotDetectView::onErrorOccurred);

    // --- 工具栏按钮 ---
    connect(m_loadImageBtn, &QPushButton::clicked,
            this, &ZeroShotDetectView::loadImageForInference);
    connect(m_batchBtn, &QPushButton::clicked,
            this, &ZeroShotDetectView::loadBatchImages);
    connect(m_stopBtn, &QPushButton::clicked,
            this, &ZeroShotDetectView::onStopRequested);
    // v2.0 阶段六 Task 14：导出结果
    connect(m_exportBtn, &QPushButton::clicked,
            this, &ZeroShotDetectView::onExportResultsRequested);
    // v2.0 阶段六 Task 15：批量推理（选目录后立即推理）
    connect(m_batchInferBtn, &QPushButton::clicked,
            this, &ZeroShotDetectView::onBatchInferRequested);
}

// ============================================================================
// 模型加载 / 推理 / 停止槽
// ============================================================================
void ZeroShotDetectView::onModelLoadRequested(zsu::ZeroShotModelType type, const QString& path) {
    const bool ok = m_kit->loadModel(type, path);
    if (ok) {
        m_panel->updateModelStatus(true, m_panel->modelTypeToString(type));
        updateNotesView();  // 切换模型后刷新右栏注意事项
        m_statusLabel->setText(QStringLiteral("模型加载成功"));
    } else {
        m_panel->updateModelStatus(false, m_panel->modelTypeToString(type),
                                    QStringLiteral("模型加载失败"));
        m_statusLabel->setText(QStringLiteral("模型加载失败"));
        // 展示详细失败原因 + 改进建议（替代原先笼统的"模型加载失败"）
        const QString detail = m_kit->lastError();
        if (detail.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("模型加载失败"),
                QStringLiteral("模型加载失败，未获取到具体原因。\n请检查终端 / logs 目录中的日志。"));
        } else {
            QMessageBox::warning(this, QStringLiteral("模型加载失败"),
                QStringLiteral("%1\n\n（详细日志见终端及 logs 目录）").arg(detail));
        }
    }
}

void ZeroShotDetectView::onInferenceRequested() {
    if (m_currentImagePath.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("请先加载图像"));
        return;
    }
    if (!m_kit->isModelLoaded()) {
        m_statusLabel->setText(QStringLiteral("请先加载模型"));
        return;
    }
    m_resultPanel->clearResults();
    m_progressBar->setRange(0, 0);  // 不确定进度（忙碌状态）
    m_statusLabel->setText(QStringLiteral("推理中..."));
    m_kit->inferAsync(m_currentImagePath);
}

void ZeroShotDetectView::onInferenceAllRequested() {
    if (m_batchImagePaths.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("请先加载批量目录"));
        return;
    }
    if (!m_kit->isModelLoaded()) {
        m_statusLabel->setText(QStringLiteral("请先加载模型"));
        return;
    }
    m_resultPanel->clearResults();
    m_progressBar->setRange(0, m_batchImagePaths.size());
    m_progressBar->setValue(0);
    m_statusLabel->setText(QStringLiteral("批量推理中..."));
    m_kit->inferBatchAsync(m_batchImagePaths);
}

void ZeroShotDetectView::onStopRequested() {
    m_kit->cancel();
    m_statusLabel->setText(QStringLiteral("已停止"));
    m_progressBar->setRange(0, 100);
}

// ============================================================================
// PatchCore 正常样本管理槽
// ============================================================================
void ZeroShotDetectView::onAddNormalSampleRequested() {
    if (m_currentImagePath.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("请先加载图像"));
        return;
    }
    const cv::Mat img = readImageChineseSafe(m_currentImagePath);
    if (img.empty()) {
        m_statusLabel->setText(QStringLiteral("图像读取失败"));
        return;
    }
    if (m_kit->addNormalSample(img)) {
        m_panel->updatePatchCoreSampleCount(m_kit->normalSampleCount());
        m_statusLabel->setText(
            QStringLiteral("已添加正常样本 (共 %1 张)").arg(m_kit->normalSampleCount()));
    } else {
        m_statusLabel->setText(QStringLiteral("添加样本失败"));
    }
}

void ZeroShotDetectView::onRemoveLastNormalSampleRequested() {
    m_kit->removeLastNormalSample();
    m_panel->updatePatchCoreSampleCount(m_kit->normalSampleCount());
}

void ZeroShotDetectView::onClearNormalSamplesRequested() {
    m_kit->clearNormalSamples();
    m_panel->updatePatchCoreSampleCount(0);
    m_statusLabel->setText(QStringLiteral("已清空正常样本"));
}

// ============================================================================
// 准确性保障槽
// ============================================================================
void ZeroShotDetectView::onHumanReviewToggled(bool enabled) {
    m_resultPanel->setReviewMode(enabled);
}

void ZeroShotDetectView::onSettingsChanged() {
    // 把面板的最新配置同步到 Kit
    m_kit->setTextPrompts(m_panel->textPrompts());
    m_kit->setAnomalyThreshold(m_panel->anomalyThreshold());
    m_kit->setDetectionThreshold(m_panel->detectionThreshold());

    // 稳定性配置
    zsu::StabilityConfig cfg;
    cfg.enableMultiRunStability = m_panel->multiRunStabilityEnabled();
    cfg.numRuns = m_panel->multiRunCount();
    cfg.nmsIouThreshold = m_panel->nmsIouThreshold();
    cfg.confidenceThreshold = m_panel->detectionThreshold();
    m_kit->setStabilityConfig(cfg);

    // 量化模型
    if (m_kit->engine()) {
        m_kit->engine()->setUseQuantizedModel(m_panel->useQuantized());
    }
}

// ============================================================================
// Kit 信号 → 结果面板槽
// ============================================================================
void ZeroShotDetectView::onInferenceCompleted(const zsu::ZeroShotResult& result) {
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(100);
    m_resultPanel->setResult(result);
    m_statusLabel->setText(result.success
                           ? QStringLiteral("推理完成")
                           : (QStringLiteral("推理失败: ") + result.errorMessage));
    // v2.0 阶段六 Task 14：缓存最近一次结果与原图，供导出对话框使用
    if (result.success) {
        m_lastResult = result;
        if (!m_currentImagePath.isEmpty()) {
            m_lastImage = readImageChineseSafe(m_currentImagePath);
        }
        m_hasLastResult = true;
        emit zeroShotResultReady(1);
    } else {
        m_hasLastResult = false;
    }
}

void ZeroShotDetectView::onBatchCompleted(const QList<zsu::ZeroShotResult>& results) {
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(100);
    m_resultPanel->setResults(results);
    int okCount = 0;
    for (const auto& r : results) {
        if (r.success) ++okCount;
    }
    m_statusLabel->setText(
        QStringLiteral("批量完成: %1/%2 成功").arg(okCount).arg(results.size()));
    emit zeroShotResultReady(okCount);
}

void ZeroShotDetectView::onProgressUpdated(int current, int total) {
    m_progressBar->setRange(0, total);
    m_progressBar->setValue(current);
    m_resultPanel->setProgress(current, total);
}

void ZeroShotDetectView::onErrorOccurred(const QString& message) {
    m_statusLabel->setText(QStringLiteral("错误: ") + message);
    m_progressBar->setRange(0, 100);
    QMessageBox::warning(this, QStringLiteral("零样本检测错误"), message);
    emit zeroShotErrorOccurred(message);
}

// ============================================================================
// 文件对话框：加载单张 / 批量目录
// ============================================================================
void ZeroShotDetectView::loadImageForInference() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择图像"), QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp)"));
    if (path.isEmpty()) return;
    m_currentImagePath = path;
    m_statusLabel->setText(QStringLiteral("已加载: ") + QFileInfo(path).fileName());
}

void ZeroShotDetectView::loadBatchImages() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择批量推理目录"));
    if (dir.isEmpty()) return;
    QDir d(dir);
    const QStringList filters = {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tif", "*.tiff", "*.webp"};
    const QFileInfoList files = d.entryInfoList(filters, QDir::Files, QDir::Name);
    m_batchImagePaths.clear();
    for (const auto& f : files) {
        m_batchImagePaths << f.absoluteFilePath();
    }
    m_statusLabel->setText(
        QStringLiteral("批量目录: %1 张图像").arg(m_batchImagePaths.size()));
}

// ============================================================================
// 状态同步：模型状态 / 模型注意事项
// ============================================================================
void ZeroShotDetectView::updateModelStatus() {
    const bool loaded = m_kit->isModelLoaded();
    const QString name = loaded ? m_panel->modelTypeToString(m_kit->modelType())
                                : QStringLiteral("未加载");
    m_panel->updateModelStatus(loaded, name);
}

void ZeroShotDetectView::updateNotesView() {
    if (!m_kit || !m_kit->notesManager()) {
        m_notesView->setPlainText(QStringLiteral("（注意事项管理器未就绪）"));
        return;
    }
    // 优先显示已加载模型类型；否则显示面板当前选择的类型
    const zsu::ZeroShotModelType type = m_kit->isModelLoaded()
        ? m_kit->modelType()
        : m_panel->modelType();
    const zsu::ModelNote note = m_kit->notesManager()->getNote(type);

    const QString html = QStringLiteral(
        "<h3>%1</h3>"
        "<p><i>%2</i></p>"
        "<h4>注意事项</h4><ul>%3</ul>"
        "<h4>输入格式</h4><ul>%4</ul>"
        "<h4>限制条件</h4><ul>%5</ul>"
        "<h4>使用建议</h4><ul>%6</ul>")
        .arg(note.displayName.toHtmlEscaped())
        .arg(note.description.toHtmlEscaped())
        .arg(joinStringListAsHtmlList(note.notes))
        .arg(joinStringListAsHtmlList(note.inputFormat))
        .arg(joinStringListAsHtmlList(note.limitations))
        .arg(joinStringListAsHtmlList(note.tips));

    m_notesView->setHtml(html);
}

// ============================================================================
// 中文路径安全读取图像
// cv::imread 在 Windows 下不支持中文路径，使用 QFile + cv::imdecode 规避。
// 参考：third_party/ZeroShotKit/docs/IntegrationGuide.md
// ============================================================================
cv::Mat ZeroShotDetectView::readImageChineseSafe(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return cv::Mat();
    }
    const QByteArray data = file.readAll();
    file.close();
    return cv::imdecode(std::vector<uchar>(data.begin(), data.end()), cv::IMREAD_COLOR);
}

// ============================================================================
// 首次显示铺满修复（v1.1.0）
// ============================================================================
void ZeroShotDetectView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);

    // QStackedWidget 中隐藏的子控件不会触发 resize/layout，
    // 首次显示时 m_splitter 可能仍保持构造时的默认尺寸（如 640x480），
    // 导致三栏布局看起来"未铺满"。延迟到下一事件循环，等 QWidget 完成
    // 布局后再按当前实际宽度重新分配 splitter sizes。
    if (m_firstShow) {
        m_firstShow = false;
        QTimer::singleShot(0, this, &ZeroShotDetectView::refreshSplitterSizes);
    }
}

void ZeroShotDetectView::refreshSplitterSizes() {
    if (!m_splitter) return;
    const int total = m_splitter->width();
    if (total <= 0) return;

    // 按面板最小宽度比例分配；中间结果面板获得剩余空间。
    const int leftMin   = m_panel ? m_panel->minimumWidth() : 360;
    const int rightMin  = m_notesView ? m_notesView->minimumWidth() : 280;
    const int middleMin = m_resultPanel ? m_resultPanel->minimumWidth() : 400;

    int left  = leftMin;
    int right = rightMin;
    int middle = total - left - right;
    if (middle < middleMin) {
        // 空间不足时优先保证中间面板，再压缩左右
        middle = middleMin;
        const int spare = total - middle;
        if (spare > 0) {
            left  = spare * leftMin / (leftMin + rightMin);
            right = spare - left;
        }
    }
    // 保护非负
    left   = qMax(left, 0);
    right  = qMax(right, 0);
    middle = qMax(middle, 0);

    m_splitter->setSizes({left, middle, right});
}

// ============================================================================
// v2.0 阶段二 Task 7：共享提示词库桥接实现
// ============================================================================

void ZeroShotDetectView::refreshTargetTypeEntries() {
    if (!m_panel) return;
    // 从 PromptLibrary 加载所有条目（builtin 12 类 + custom + recent）
    // 传入 ZeroShotPanel，由其重建目标类型下拉菜单
    QDV::PromptLibrary* lib = QDV::PromptLibrary::instance();
    QVariantList entries = lib->allEntriesAsVariant();
    m_panel->setTargetTypeEntries(entries);
    QDV::Logger::info(QString("[ZeroShotDetectView] 刷新目标类型下拉: %1 条目").arg(entries.size()));
}

void ZeroShotDetectView::onCustomTargetTypeRequested(const QVariantMap& entry) {
    // 接收 ZeroShotPanel 的自定义类别保存请求，调用 PromptLibrary::addCustom 持久化
    QDV::PromptLibrary* lib = QDV::PromptLibrary::instance();
    const QString name   = entry.value("name").toString();
    const QString prompt = entry.value("prompt").toString();
    const QString cnName = entry.value("cnName").toString();
    const QString scene  = entry.value("scene").toString();
    const QString img    = entry.value("exampleImagePath").toString();

    bool ok = lib->addCustom(name, prompt, cnName, scene, img);
    if (ok) {
        QDV::Logger::info(QString("[ZeroShotDetectView] 自定义类别已保存: %1 -> %2").arg(name).arg(prompt));
        // libraryChanged 信号会触发 refreshTargetTypeEntries() 自动刷新下拉
    } else {
        QDV::Logger::warn(QString("[ZeroShotDetectView] 自定义类别保存失败: %1 (可能与内置类别重名或参数为空)").arg(name));
    }
}

void ZeroShotDetectView::onTargetTypeSelectionChanged(const QStringList& selectedPrompts) {
    // 用户选择变化时，记录到 PromptLibrary 的 recent 节（最多 10 条，去重）
    if (selectedPrompts.isEmpty()) return;
    QDV::PromptLibrary::instance()->addRecent(selectedPrompts);
    QDV::Logger::info(QString("[ZeroShotDetectView] 目标类型选择变化，记录 recent: %1")
                          .arg(selectedPrompts.join(" . ")));
}

// ============================================================================
// v2.0 阶段五 Task 13：第二层 训练兑底流程桥接实现
// ============================================================================

void ZeroShotDetectView::onSpecializedTrainingRequested(const QVariantMap& entry) {
    // 接收 ZeroShotPanel 的训练请求，启动数据收集向导
    launchDataCollectionWizard(entry);
}

void ZeroShotDetectView::launchDataCollectionWizard(const QVariantMap& entry) {
    QDV::DataCollectionWizard wizard(this);
    wizard.setTargetEntry(entry);

    if (wizard.exec() != QDialog::Accepted) {
        m_statusLabel->setText(QStringLiteral("数据收集向导已取消"));
        return;
    }

    // 向导完成后导出数据集
    QString err;
    if (!wizard.exportDataset(&err)) {
        QMessageBox::critical(this, QStringLiteral("数据集导出失败"), err);
        m_statusLabel->setText(QStringLiteral("数据集导出失败: ") + err);
        return;
    }

    // 读取训练参数
    QDV::DataCollectionWizard::TrainParams params = wizard.trainParams();
    if (params.datasetManifestPath.isEmpty()) {
        QMessageBox::critical(this, QStringLiteral("错误"),
            QStringLiteral("数据集 manifest 路径为空"));
        return;
    }

    // 记录待标记的类别 prompt（训练完成后用于标记专用模型）
    m_pendingSpecializedPrompt = entry.value("prompt").toString();
    if (m_pendingSpecializedPrompt.isEmpty()) {
        m_pendingSpecializedPrompt = params.className;
    }

    // 确定训练用 Python 路径（复用 TrainingInferenceView 约定的虚拟环境）
    QString pythonPath = QStringLiteral("E:/anchor/Trae/QDV/training/venv/Scripts/python.exe");
    if (!QFile::exists(pythonPath)) {
        pythonPath = QStringLiteral("python");  // 回退到系统 Python
    }

    // 检查 Python 环境
    QString pyErr;
    if (!QDV::TrainingBridge::checkPythonEnvironment(pythonPath, pyErr)) {
        QMessageBox::critical(this, QStringLiteral("环境检查失败"),
            QStringLiteral("训练需要 Python 环境：\n%1\n\n请先安装 Python 3.8+").arg(pyErr));
        m_pendingSpecializedPrompt.clear();
        return;
    }

    // 启动训练（modelType 改用 wizard 选择的 yolov8n 等）
    // TrainingBridge::startTraining 内部会写训练配置 + 启动 QProcess
    m_statusLabel->setText(QStringLiteral("启动训练: %1, %2 epochs")
                               .arg(params.modelType).arg(params.numEpochs));
    m_progressBar->setRange(0, params.numEpochs);
    m_progressBar->setValue(0);

    QDV::Logger::info(QStringLiteral("[ZeroShotDetectView] 启动专用模型训练: 类别=%1, manifest=%2, output=%3")
                          .arg(params.className, params.datasetManifestPath, params.outputDir));

    m_trainingBridge->startTraining(
        params.datasetManifestPath,
        params.outputDir,
        params.modelType,
        params.numEpochs,
        params.batchSize,
        params.learningRate,
        pythonPath
    );

    QMessageBox::information(this, QStringLiteral("训练已启动"),
        QStringLiteral("训练已在后台启动。\n\n"
                       "类别: %1\n"
                       "模型: %2\n"
                       "轮数: %3\n\n"
                       "训练完成后将自动注册算子并在提示词库标记 ✔。")
            .arg(params.className, params.modelType).arg(params.numEpochs));
}

void ZeroShotDetectView::onSpecializedTrainingCompleted(const QVariantMap& result) {
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(100);

    bool success = result.value("success").toBool();
    if (!success) {
        m_statusLabel->setText(QStringLiteral("训练失败: ") + result.value("errorMessage").toString());
        m_pendingSpecializedPrompt.clear();
        return;
    }

    QString onnxPath   = result.value("onnxPath").toString();
    QString labelsPath = result.value("labelsPath").toString();
    QString modelName  = QFileInfo(onnxPath).completeBaseName();
    if (modelName.isEmpty()) {
        modelName = result.value("modelName", QStringLiteral("trained_model")).toString();
    }

    m_statusLabel->setText(QStringLiteral("训练完成: %1").arg(modelName));

    if (m_pendingSpecializedPrompt.isEmpty()) {
        QDV::Logger::warn(QStringLiteral("[ZeroShotDetectView] 训练完成但 m_pendingSpecializedPrompt 为空，跳过专用模型标记"));
        return;
    }

    // 调用统一的标记方法
    markSpecializedModelAfterTraining(m_pendingSpecializedPrompt, onnxPath, modelName);
    m_pendingSpecializedPrompt.clear();

    QMessageBox::information(this, QStringLiteral("专用模型已就绪"),
        QStringLiteral("训练完成！\n\n"
                       "ONNX: %1\n"
                       "模型名: %2\n\n"
                       "已自动注册算子（YoloTrained_%2），\n"
                       "提示词库该类别旁已标记 ✔。\n\n"
                       "现在可在方案编辑中拖入算子使用，\n"
                       "或在零样本调试中切换模型类型。")
            .arg(onnxPath, modelName));
}

void ZeroShotDetectView::markSpecializedModelAfterTraining(const QString& prompt,
                                                            const QString& onnxPath,
                                                            const QString& modelName) {
    // 训练完成后：
    // 1) OperatorLibraryController::registerTrainedModelAsOperator 注册算子（拖入方案即用）
    // 2) PromptLibrary::setSpecializedModel 标记该类别，UI 自动刷新显示 ✔
    QString opType = QStringLiteral("YoloTrained_") + modelName;
    bool opRegistered = false;
    try {
        opRegistered = QDV::OperatorLibrary::OperatorLibraryController::instance()
            ->registerTrainedModelAsOperator(modelName, onnxPath, QString(), modelName);
    } catch (...) {
        opRegistered = false;
    }
    if (opRegistered) {
        QDV::Logger::info(QStringLiteral("[ZeroShotDetectView] 专用模型算子已注册: %1").arg(opType));
    } else {
        QDV::Logger::warn(QStringLiteral("[ZeroShotDetectView] 专用模型算子注册失败（不影响训练产物，用户可手动使用 YoloDetect 加载 %1）").arg(onnxPath));
    }

    // 在 PromptLibrary 中标记该类别有专用模型（持久化，重启后仍可见）
    // 专用模型路径存算子 type（便于 UI 显示与切换提示）
    QDV::PromptLibrary::instance()->setSpecializedModel(prompt, opType);

    QDV::Logger::info(QStringLiteral("[ZeroShotDetectView] 已标记专用模型: prompt=%1 -> %2").arg(prompt, opType));
    // libraryChanged 信号会触发 refreshTargetTypeEntries 自动刷新下拉菜单显示 ✔
}

void ZeroShotDetectView::onDatasetExported(const QVariantMap& trainParams) {
    // 备用接口：直接通过信号触发训练（当前流程在 launchDataCollectionWizard 中同步处理）
    Q_UNUSED(trainParams);
}

// ============================================================================
// v2.0 阶段七 Task 18：模型源监控桥接实现
// ============================================================================

void ZeroShotDetectView::refreshModelEntries() {
    if (!m_panel) return;
    // 从 ModelSourceManager 获取合并后的模型清单（含 isNew 标记）
    // 传入 ZeroShotPanel，由其重建具体模型下拉
    QDV::ModelSourceManager* mgr = QDV::ModelSourceManager::instance();
    QVariantList entries = mgr->allModelsAsVariant();
    m_panel->setModelEntries(entries);

    // LM Studio 路径不可用提示（不影响其他源使用）
    if (!mgr->isLmStudioAvailable()) {
        // 查找 LM Studio 源路径，生成提示文案
        QString lmPath;
        QString lmStatus;
        for (const QDV::ModelSource& s : mgr->sources()) {
            if (s.sourceType == QDV::ModelSourceTypes::LMSTUDIO) {
                lmPath = s.path;
                lmStatus = mgr->sourceStatuses().value(s.path);
                break;
            }
        }
        if (!lmPath.isEmpty()) {
            QString hint;
            if (lmStatus == QDV::ModelSourceStatus::UNMOUNTED) {
                hint = QStringLiteral(
                    "<b>⚠ LM Studio 模型源不可用</b><br>"
                    "路径 %1 不存在。<br>"
                    "请检查该路径是否已创建，或在 LM Studio 中设置下载目录为该路径。<br>"
                    "其他模型源（本地/训练产物）不受影响，可正常使用。").arg(lmPath);
            } else if (lmStatus == QDV::ModelSourceStatus::EMPTY) {
                hint = QStringLiteral(
                    "<b>⚠ 未检测到 LM Studio 模型</b><br>"
                    "路径 %1 存在但未发现可用模型文件（.gguf/.onnx/.pt/.safetensors）。<br>"
                    "请通过 LM Studio 下载模型到该路径，或检查路径配置。<br>"
                    "其他模型源不受影响。").arg(lmPath);
            }
            m_panel->setLmStudioStatusHint(hint);
        }
    } else {
        // LM Studio 可用，清除提示
        m_panel->setLmStudioStatusHint(QString());
    }

    QDV::Logger::info(QString("[ZeroShotDetectView] 刷新具体模型下拉: %1 条目").arg(entries.size()));
}

void ZeroShotDetectView::onModelSelectionChanged(const QString& path, const QString& sourceType) {
    // 用户在 ZeroShotPanel 的具体模型下拉中选中某项
    // 路径已由 Panel 自动填入 m_modelPathEdit，这里仅记录日志
    QString sourceLabel;
    if (sourceType == QDV::ModelSourceTypes::LMSTUDIO) {
        sourceLabel = QStringLiteral("LM Studio");
    } else if (sourceType == QDV::ModelSourceTypes::LOCAL) {
        sourceLabel = QStringLiteral("本地");
    } else if (sourceType == QDV::ModelSourceTypes::TRAINED) {
        sourceLabel = QStringLiteral("训练产物");
    } else {
        sourceLabel = sourceType;
    }
    QDV::Logger::info(QString("[ZeroShotDetectView] 用户选中模型: %1 (来源: %2)")
                          .arg(path).arg(sourceLabel));
}

// ============================================================================
// v2.0 阶段六 Task 14：结果导出实现
// ============================================================================

void ZeroShotDetectView::onExportResultsRequested() {
    if (!m_hasLastResult) {
        QMessageBox::information(this, QStringLiteral("无可导出结果"),
            QStringLiteral("请先执行单图推理，再点击导出。\n"
                           "（批量推理结果请通过结果面板的导航逐张导出）"));
        return;
    }

    // 构造检测结果 JSON（zsu::ZeroShotResult.toJson 已含 detections/anomaly_score/latency_ms 等）
    QJsonObject resultsJson = m_lastResult.toJson();

    // 构造元数据（图像路径/ROI/耗时/模型名/时间戳/detections 像素坐标版本）
    QVariantMap metadata;
    metadata["imagePath"]  = m_currentImagePath;
    metadata["modelName"]  = m_panel ? m_panel->modelTypeToString(m_kit->modelType())
                                     : QStringLiteral("Unknown");
    metadata["elapsedMs"]  = static_cast<double>(m_lastResult.metrics.totalMs);
    metadata["timestamp"]  = QDateTime::currentDateTime().toString(Qt::ISODate);
    // ROI：从 Panel 获取（若 Panel 暴露了 ROI 接口）；此处暂不集成，留待后续扩展
    // 提示词（便于审计复现）
    if (m_panel) {
        metadata["textPrompts"] = m_panel->textPrompts().join(QStringLiteral(" . "));
    }

    // detections 像素坐标版本（ZeroShotResult.toJson 中是归一化坐标，
    // 这里转为像素坐标供 JSON/CSV/图片标注使用，与 ZeroShotDetectTool 输出一致）
    QVariantList detList;
    const int imgW = m_lastImage.cols;
    const int imgH = m_lastImage.rows;
    if (imgW > 0 && imgH > 0) {
        for (const auto& d : m_lastResult.detections) {
            QVariantMap det;
            det["cx"]         = static_cast<double>(d.cx) * imgW;
            det["cy"]         = static_cast<double>(d.cy) * imgH;
            det["w"]          = static_cast<double>(d.w)  * imgW;
            det["h"]          = static_cast<double>(d.h)  * imgH;
            det["confidence"] = static_cast<double>(d.confidence);
            det["classId"]    = d.classId;
            det["className"]  = d.className;
            detList.append(det);
        }
    }
    metadata["detections"] = detList;

    // 弹出导出对话框
    ResultExportDialog dlg(this);
    dlg.setResults(resultsJson, m_lastImage, metadata);
    dlg.exec();
}

// ============================================================================
// v2.0 阶段六 Task 15：批量推理实现（选目录后立即推理）
// 与现有 m_batchBtn（仅选目录）不同：本按钮选目录后立即触发推理
// ============================================================================

void ZeroShotDetectView::onBatchInferRequested() {
    if (!m_kit->isModelLoaded()) {
        QMessageBox::warning(this, QStringLiteral("未加载模型"),
            QStringLiteral("请先加载模型，再执行批量推理。"));
        return;
    }

    // 1. 弹出目录选择对话框
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择批量推理目录（将自动遍历所有图像）"));
    if (dir.isEmpty()) return;

    // 2. 遍历目录下所有图像文件（.jpg/.png/.bmp 等常见格式）
    QDir d(dir);
    const QStringList filters = {"*.png", "*.jpg", "*.jpeg", "*.bmp",
                                  "*.tif", "*.tiff", "*.webp"};
    const QFileInfoList files = d.entryInfoList(filters, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("无图像文件"),
            QStringLiteral("所选目录中未发现图像文件（支持 PNG/JPG/BMP/TIF/WEBP）"));
        return;
    }

    // 3. 收集图像路径并触发批量推理
    m_batchImagePaths.clear();
    for (const auto& f : files) {
        m_batchImagePaths << f.absoluteFilePath();
    }

    m_resultPanel->clearResults();
    m_progressBar->setRange(0, m_batchImagePaths.size());
    m_progressBar->setValue(0);
    m_statusLabel->setText(
        QStringLiteral("批量推理中: %1 张图像...").arg(m_batchImagePaths.size()));

    QDV::Logger::info(QStringLiteral("[ZeroShotDetectView] 启动批量推理: %1 张图像, 目录=%2")
                          .arg(m_batchImagePaths.size()).arg(dir));

    // 调用 zsu::Kit::inferBatchAsync（已有方法），进度与结果通过信号回传
    m_kit->inferBatchAsync(m_batchImagePaths);
}
