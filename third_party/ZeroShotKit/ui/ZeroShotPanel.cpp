#include "ZeroShotPanel.h"
#include "ModelNotesDialog.h"
#include "ZeroShotKit/ModelNotesManager.h"
#include "ZeroShotKit/BadCaseRecorder.h"
#include "ZeroShotKit/Logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QCoreApplication>
#include <QDir>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QMessageBox>

using namespace QDVMini;

ZeroShotPanel::ZeroShotPanel(QWidget* parent) : QWidget(parent) {
    setupUI();
    // 初始按默认模型类型刷新可见性与占位符
    updateControlVisibility();
    updatePromptPlaceholder();
}

// ============================================================================
// UI 构建
// ============================================================================
void ZeroShotPanel::setupUI() {
    // ==========================================================================
    // Task 2 修复：零样本面板不再持有内部 QScrollArea，避免与底部配置区外层
    // 滚动容器形成嵌套滚动条。内容由外部 TrainingInferenceView 的 QScrollArea 统一
    // 负责滚动，三个模块（分类管理 / 推理设置 / 零样本配置）保持一致的垂直堆叠风格。
    // ==========================================================================
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QWidget* content = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);

    // ==========================================================================
    // 第1组：模型配置（模型类型 + 路径 + 提示词）
    // ==========================================================================
    QGroupBox* modelConfigBox = new QGroupBox(tr("模型配置"), content);
    QVBoxLayout* modelConfigLayout = new QVBoxLayout(modelConfigBox);
    modelConfigLayout->setSpacing(6);
    modelConfigLayout->setContentsMargins(8, 12, 8, 8);

    // 模型类型行
    QHBoxLayout* typeRow = new QHBoxLayout();
    typeRow->setSpacing(6);
    typeRow->addWidget(new QLabel(tr("模型类型:"), modelConfigBox));

    m_modelTypeCombo = new QComboBox(modelConfigBox);
    m_modelTypeCombo->setToolTip(tr("选择零样本模型类型"));
    m_modelTypeCombo->addItem(tr("AnomalyCLIP (零样本异常检测)"),
                              static_cast<int>(zsu::ZeroShotModelType::AnomalyCLIP));
    m_modelTypeCombo->addItem(tr("Grounding DINO (开集目标检测)"),
                              static_cast<int>(zsu::ZeroShotModelType::GroundingDINO));
    m_modelTypeCombo->addItem(tr("MobileSAM (轻量分割)"),
                              static_cast<int>(zsu::ZeroShotModelType::MobileSAM));
    m_modelTypeCombo->addItem(tr("PatchCore (正常样本建模)"),
                              static_cast<int>(zsu::ZeroShotModelType::PatchCore));
    connect(m_modelTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ZeroShotPanel::onModelTypeChanged);
    typeRow->addWidget(m_modelTypeCombo, 1);

    // ℹ️ 信息按钮
    m_modelNotesBtn = new QPushButton(tr("\xe2\x84\xb9"), modelConfigBox);  // ℹ 符号
    m_modelNotesBtn->setFixedSize(26, 26);
    m_modelNotesBtn->setToolTip(tr("查看当前模型的注意事项与使用说明"));
    m_modelNotesBtn->setStyleSheet(
        "QPushButton { font-size: 13px; font-weight: bold; color: #569cd6; "
        "border: 1px solid #3a3a3e; border-radius: 4px; background: #252525; }"
        "QPushButton:hover { background: #2d2d30; color: #4ec9b0; }");
    connect(m_modelNotesBtn, &QPushButton::clicked, this, &ZeroShotPanel::onShowModelNotes);
    typeRow->addWidget(m_modelNotesBtn);
    modelConfigLayout->addLayout(typeRow);

    // ==========================================================================
    // v2.0 阶段七 Task 18：具体模型下拉（合并所有源路径的可用模型）
    // 按来源分组（LM Studio / 本地 / 训练产物），新模型追加" [新]"标签
    // 选中后自动填入"模型路径"输入框
    // ==========================================================================
    QHBoxLayout* modelComboRow = new QHBoxLayout();
    modelComboRow->setSpacing(6);
    modelComboRow->addWidget(new QLabel(tr("具体模型:"), modelConfigBox));

    m_modelCombo = new QComboBox(modelConfigBox);
    m_modelCombo->setToolTip(tr("从所有模型源路径合并的下拉列表中选择具体模型"));
    // 用 QComboBox 内置的 separators 实现分组标题（不可选）
    connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ZeroShotPanel::onModelSelectionChanged);
    modelComboRow->addWidget(m_modelCombo, 1);
    modelConfigLayout->addLayout(modelComboRow);

    // LM Studio 路径不可用提示标签（默认隐藏）
    m_lmStudioHintLabel = new QLabel(modelConfigBox);
    m_lmStudioHintLabel->setWordWrap(true);
    m_lmStudioHintLabel->setVisible(false);
    m_lmStudioHintLabel->setStyleSheet(
        "QLabel { color: #f9e2af; background: #2a2a2a; border: 1px solid #5a5a3a; "
        "border-radius: 3px; padding: 4px 6px; font-size: 11px; }");
    modelConfigLayout->addWidget(m_lmStudioHintLabel);

    // 模型路径行
    QHBoxLayout* pathRow = new QHBoxLayout();
    pathRow->setSpacing(6);
    pathRow->addWidget(new QLabel(tr("模型路径:"), modelConfigBox));

    m_modelPathEdit = new QLineEdit(modelConfigBox);
    m_modelPathEdit->setPlaceholderText(tr("选择模型所在目录..."));
    pathRow->addWidget(m_modelPathEdit, 1);

    m_browseBtn = new QPushButton(tr("浏览..."), modelConfigBox);
    m_browseBtn->setToolTip(tr("选择模型目录"));
    connect(m_browseBtn, &QPushButton::clicked, this, &ZeroShotPanel::onBrowseModel);
    pathRow->addWidget(m_browseBtn);
    modelConfigLayout->addLayout(pathRow);

    // 提示词行
    QHBoxLayout* promptRow = new QHBoxLayout();
    promptRow->setSpacing(6);
    promptRow->addWidget(new QLabel(tr("提示词:"), modelConfigBox));

    m_promptsEdit = new QLineEdit(modelConfigBox);
    m_promptsEdit->setToolTip(tr("多个提示词用分号 ; 或点号 . 分隔；目标类型选择后自动拼接"));
    promptRow->addWidget(m_promptsEdit, 1);
    modelConfigLayout->addLayout(promptRow);

    // ==========================================================================
    // v2.0 阶段二 Task 7：目标类型选择区（共享提示词库）
    // 多选下拉（builtin 12 类 + custom + recent）+ 自定义按钮
    // 选中项实时拼接为提示词串（用 " . " 分隔）填入提示词输入框
    // ==========================================================================
    QHBoxLayout* targetTypeRow = new QHBoxLayout();
    targetTypeRow->setSpacing(6);
    targetTypeRow->addWidget(new QLabel(tr("目标类型:"), modelConfigBox));

    m_targetTypeBtn = new QToolButton(modelConfigBox);
    m_targetTypeBtn->setText(tr("选择目标类型..."));
    m_targetTypeBtn->setToolTip(tr("点击选择一个或多个目标类型（来自共享提示词库）"));
    m_targetTypeBtn->setPopupMode(QToolButton::InstantPopup);
    m_targetTypeBtn->setStyleSheet(
        "QToolButton { padding: 4px 10px; border: 1px solid #3a3a3e; border-radius: 3px; "
        "background: #2d2d30; color: #dcdcdc; text-align: left; }"
        "QToolButton:hover { background: #37373d; }"
        "QToolButton::menu-indicator { image: none; }");
    m_targetTypeMenu = new QMenu(m_targetTypeBtn);
    m_targetTypeBtn->setMenu(m_targetTypeMenu);
    targetTypeRow->addWidget(m_targetTypeBtn, 1);

    m_customTypeBtn = new QPushButton(tr("自定义..."), modelConfigBox);
    m_customTypeBtn->setToolTip(tr("添加自定义目标类型到共享提示词库"));
    m_customTypeBtn->setStyleSheet(
        "QPushButton { padding: 4px 10px; border: 1px solid #3a5a3a; border-radius: 3px; "
        "background: #2d4a2d; color: #4ec9b0; }"
        "QPushButton:hover { background: #3d5a3d; }");
    connect(m_customTypeBtn, &QPushButton::clicked, this, &ZeroShotPanel::onCustomTargetType);
    targetTypeRow->addWidget(m_customTypeBtn);

    // v2.0 阶段五 Task 13：训练专用模型入口按钮
    // 当目标类型精度不足时，点击此按钮启动数据收集向导 → 训练 YOLO → 自动注册算子 → 标记"专用模型✔"
    // 仅在选中单一类别时启用（多选时不允许训练，避免数据集混乱）
    m_trainSpecializedBtn = new QPushButton(tr("训练专用模型..."), modelConfigBox);
    m_trainSpecializedBtn->setToolTip(tr("开放词汇精度不足时，启动数据收集向导训练专用 YOLO 模型（自动注册为算子）"));
    m_trainSpecializedBtn->setStyleSheet(
        "QPushButton { padding: 4px 10px; border: 1px solid #5a4a2a; border-radius: 3px; "
        "background: #4d3d1d; color: #ffd27f; }"
        "QPushButton:hover { background: #5d4d2d; }"
        "QPushButton:disabled { color: #555; background: #2a2a2a; border-color: #3a3a3a; }");
    m_trainSpecializedBtn->setEnabled(false);  // 初始禁用，选中单一类别时启用
    connect(m_trainSpecializedBtn, &QPushButton::clicked, this, &ZeroShotPanel::onTrainSpecializedModel);
    targetTypeRow->addWidget(m_trainSpecializedBtn);

    modelConfigLayout->addLayout(targetTypeRow);

    // 连接提示词框编辑信号（手动微调）
    connect(m_promptsEdit, &QLineEdit::textChanged, this, &ZeroShotPanel::onPromptsEdited);

    layout->addWidget(modelConfigBox);

    // ==========================================================================
    // 第2组：阈值参数（滑块独占整行，保证可操作性）
    // ==========================================================================
    QGroupBox* thresholdGroup = new QGroupBox(tr("阈值参数"), content);
    QVBoxLayout* thresholdLayout = new QVBoxLayout(thresholdGroup);
    thresholdLayout->setSpacing(6);
    thresholdLayout->setContentsMargins(8, 12, 8, 8);

    // 异常阈值：标签 + 滑块（全宽） + 数值
    QWidget* anomalyRow = new QWidget(thresholdGroup);
    QHBoxLayout* anomalyRowLayout = new QHBoxLayout(anomalyRow);
    anomalyRowLayout->setContentsMargins(0, 0, 0, 0);
    anomalyRowLayout->setSpacing(8);
    QLabel* anomalyLabel = new QLabel(tr("异常阈值"), anomalyRow);
    anomalyLabel->setMinimumWidth(60);
    m_anomalyThresholdSlider = new QSlider(Qt::Horizontal, anomalyRow);
    m_anomalyThresholdSlider->setRange(0, 100);
    m_anomalyThresholdSlider->setValue(50);
    m_anomalyThresholdSlider->setToolTip(tr("异常分数判定阈值，默认 0.50"));
    m_anomalyThresholdValueLabel = new QLabel("0.50", anomalyRow);
    m_anomalyThresholdValueLabel->setMinimumWidth(36);
    m_anomalyThresholdValueLabel->setAlignment(Qt::AlignCenter);
    anomalyRowLayout->addWidget(anomalyLabel);
    anomalyRowLayout->addWidget(m_anomalyThresholdSlider, 1);
    anomalyRowLayout->addWidget(m_anomalyThresholdValueLabel);
    connect(m_anomalyThresholdSlider, &QSlider::valueChanged,
            this, &ZeroShotPanel::onAnomalyThresholdChanged);
    thresholdLayout->addWidget(anomalyRow);

    // 检测阈值：标签 + 滑块（全宽） + 数值
    QWidget* detectionRow = new QWidget(thresholdGroup);
    QHBoxLayout* detectionRowLayout = new QHBoxLayout(detectionRow);
    detectionRowLayout->setContentsMargins(0, 0, 0, 0);
    detectionRowLayout->setSpacing(8);
    QLabel* detectionLabel = new QLabel(tr("检测阈值"), detectionRow);
    detectionLabel->setMinimumWidth(60);
    m_detectionThresholdSlider = new QSlider(Qt::Horizontal, detectionRow);
    m_detectionThresholdSlider->setRange(0, 100);
    m_detectionThresholdSlider->setValue(30);
    m_detectionThresholdSlider->setToolTip(tr("Grounding DINO 检测置信度阈值，默认 0.30"));
    m_detectionThresholdValueLabel = new QLabel("0.30", detectionRow);
    m_detectionThresholdValueLabel->setMinimumWidth(36);
    m_detectionThresholdValueLabel->setAlignment(Qt::AlignCenter);
    detectionRowLayout->addWidget(detectionLabel);
    detectionRowLayout->addWidget(m_detectionThresholdSlider, 1);
    detectionRowLayout->addWidget(m_detectionThresholdValueLabel);
    connect(m_detectionThresholdSlider, &QSlider::valueChanged,
            this, &ZeroShotPanel::onDetectionThresholdChanged);
    thresholdLayout->addWidget(detectionRow);

    // 量化开关
    m_quantizedCheck = new QCheckBox(tr("使用量化模型 (INT8)"), thresholdGroup);
    m_quantizedCheck->setToolTip(tr("启用后优先加载 _int8.onnx 量化版本，降低显存与延迟"));
    connect(m_quantizedCheck, &QCheckBox::toggled,
            this, &ZeroShotPanel::onQuantizedToggled);
    thresholdLayout->addWidget(m_quantizedCheck);

    layout->addWidget(thresholdGroup);

    // ==========================================================================
    // 第3组：操作按钮行（加载模型 + 状态 + 推理按钮）
    // ==========================================================================
    QHBoxLayout* actionRow = new QHBoxLayout();
    actionRow->setSpacing(6);

    m_loadModelBtn = new QPushButton(tr("加载模型"), content);
    m_loadModelBtn->setToolTip(tr("加载当前选择的模型到引擎"));
    m_loadModelBtn->setMinimumHeight(28);
    m_loadModelBtn->setStyleSheet(
        "QPushButton { background-color: #2d4a2d; color: #4ec9b0; "
        "border: 1px solid #3a5a3a; border-radius: 3px; padding: 2px 12px; }"
        "QPushButton:hover { background-color: #3d5a3d; }"
        "QPushButton:disabled { color: #555; background: #1a1a1a; }");
    connect(m_loadModelBtn, &QPushButton::clicked, this, &ZeroShotPanel::onLoadModel);
    actionRow->addWidget(m_loadModelBtn);

    m_modelStatusLabel = new QLabel(tr("未加载"), content);
    m_modelStatusLabel->setWordWrap(false);
    m_modelStatusLabel->setStyleSheet("color: #888; padding: 0 4px;");
    actionRow->addWidget(m_modelStatusLabel);

    actionRow->addStretch();

    m_inferCurrentBtn = new QPushButton(tr("推理当前"), content);
    m_inferCurrentBtn->setToolTip(tr("对当前图片执行推理"));
    m_inferCurrentBtn->setMinimumHeight(28);
    connect(m_inferCurrentBtn, &QPushButton::clicked, this, &ZeroShotPanel::onInferCurrent);
    actionRow->addWidget(m_inferCurrentBtn);

    m_inferAllBtn = new QPushButton(tr("推理全部"), content);
    m_inferAllBtn->setToolTip(tr("对所有已导入图片执行推理"));
    m_inferAllBtn->setMinimumHeight(28);
    connect(m_inferAllBtn, &QPushButton::clicked, this, &ZeroShotPanel::onInferAll);
    actionRow->addWidget(m_inferAllBtn);

    m_stopBtn = new QPushButton(tr("停止"), content);
    m_stopBtn->setToolTip(tr("停止当前推理任务"));
    m_stopBtn->setEnabled(false);
    m_stopBtn->setMinimumHeight(28);
    m_stopBtn->setStyleSheet(
        "QPushButton { color: #f44747; border: 1px solid #5a3a3a; border-radius: 3px; padding: 2px 12px; }"
        "QPushButton:hover { background-color: #4a2d2d; }"
        "QPushButton:disabled { color: #555; }");
    connect(m_stopBtn, &QPushButton::clicked, this, &ZeroShotPanel::onStop);
    actionRow->addWidget(m_stopBtn);

    layout->addLayout(actionRow);

    // ==========================================================================
    // 第4组：准确性保障（可折叠，默认展开）
    // ==========================================================================
    QGroupBox* accuracyBox = new QGroupBox(tr("准确性保障"), content);
    QVBoxLayout* accuracyLayout = new QVBoxLayout(accuracyBox);
    accuracyLayout->setSpacing(6);
    accuracyLayout->setContentsMargins(8, 12, 8, 8);

    // 多次推理取稳定值
    QHBoxLayout* multiRunLayout = new QHBoxLayout();
    multiRunLayout->setContentsMargins(0, 0, 0, 0);
    multiRunLayout->setSpacing(8);
    m_multiRunCheck = new QCheckBox(tr("多次推理取稳定值"), accuracyBox);
    m_multiRunCheck->setToolTip(tr("对同一张图推理多次，取投票结果，保证稳定性"));
    connect(m_multiRunCheck, &QCheckBox::toggled, this, &ZeroShotPanel::onMultiRunToggled);
    multiRunLayout->addWidget(m_multiRunCheck);

    multiRunLayout->addWidget(new QLabel(tr("次数:"), accuracyBox));
    m_multiRunSpin = new QSpinBox(accuracyBox);
    m_multiRunSpin->setRange(2, 10);
    m_multiRunSpin->setValue(3);
    m_multiRunSpin->setToolTip(tr("推理次数"));
    m_multiRunSpin->setEnabled(false);
    multiRunLayout->addWidget(m_multiRunSpin);
    multiRunLayout->addStretch();
    accuracyLayout->addLayout(multiRunLayout);

    // NMS IoU 阈值（全宽滑块）
    QWidget* nmsRow = new QWidget(accuracyBox);
    QHBoxLayout* nmsRowLayout = new QHBoxLayout(nmsRow);
    nmsRowLayout->setContentsMargins(0, 0, 0, 0);
    nmsRowLayout->setSpacing(8);
    QLabel* nmsLabel = new QLabel(tr("NMS IoU"), nmsRow);
    nmsLabel->setMinimumWidth(60);
    m_nmsThresholdSlider = new QSlider(Qt::Horizontal, nmsRow);
    m_nmsThresholdSlider->setRange(10, 80);
    m_nmsThresholdSlider->setValue(45);
    m_nmsThresholdSlider->setToolTip(tr("NMS 去重 IoU 阈值，越大保留越多框"));
    m_nmsThresholdValueLabel = new QLabel("0.45", nmsRow);
    m_nmsThresholdValueLabel->setMinimumWidth(36);
    m_nmsThresholdValueLabel->setAlignment(Qt::AlignCenter);
    nmsRowLayout->addWidget(nmsLabel);
    nmsRowLayout->addWidget(m_nmsThresholdSlider, 1);
    nmsRowLayout->addWidget(m_nmsThresholdValueLabel);
    connect(m_nmsThresholdSlider, &QSlider::valueChanged,
            this, &ZeroShotPanel::onNmsThresholdChanged);
    accuracyLayout->addWidget(nmsRow);

    // 人工复核开关
    m_humanReviewCheck = new QCheckBox(tr("启用人工复核"), accuracyBox);
    m_humanReviewCheck->setToolTip(tr("推理完成后逐张确认/拒绝检测结果，拒绝的结果计入 bad case"));
    connect(m_humanReviewCheck, &QCheckBox::toggled,
            this, &ZeroShotPanel::humanReviewToggled);
    accuracyLayout->addWidget(m_humanReviewCheck);

    m_accuracyGroup = accuracyBox;

    // 折叠/展开容器：用 QWidget 包裹内容区，通过按钮切换可见性
    QWidget* accuracyContent = new QWidget(content);
    QVBoxLayout* accuracyContentLayout = new QVBoxLayout(accuracyContent);
    accuracyContentLayout->setContentsMargins(0, 0, 0, 0);
    accuracyContentLayout->setSpacing(0);
    accuracyContentLayout->addWidget(accuracyBox);

    // 折叠按钮
    QPushButton* toggleBtn = new QPushButton(tr("\xe2\x96\xbc 准确性保障"), content);
    toggleBtn->setCheckable(true);
    toggleBtn->setChecked(true);
    toggleBtn->setToolTip(tr("展开/折叠准确性保障"));
    toggleBtn->setStyleSheet(
        "QPushButton { text-align: left; padding: 4px 8px; font-weight: bold; "
        "background: #2d2d30; color: #4ec9b0; border: 1px solid #3a3a3e; border-radius: 3px; }"
        "QPushButton:unchecked { color: #888; }");
    layout->addWidget(toggleBtn);
    layout->addWidget(accuracyContent);

    connect(toggleBtn, &QPushButton::toggled, [toggleBtn, accuracyContent](bool checked) {
        accuracyContent->setVisible(checked);
        toggleBtn->setText(checked ? tr("\xe2\x96\xbc 准确性保障") : tr("\xe2\x96\xb6 准确性保障"));
    });

    // ==========================================================================
    // 第5组：PatchCore 正常样本管理（默认隐藏）
    // ==========================================================================
    QGroupBox* patchCoreBox = new QGroupBox(tr("正常样本管理"), content);
    QVBoxLayout* patchCoreLayout = new QVBoxLayout(patchCoreBox);
    patchCoreLayout->setSpacing(6);
    patchCoreLayout->setContentsMargins(8, 12, 8, 8);

    m_sampleCountLabel = new QLabel(tr("样本数: 0"), patchCoreBox);
    patchCoreLayout->addWidget(m_sampleCountLabel);

    QHBoxLayout* patchCoreBtnLayout = new QHBoxLayout();
    m_addSampleBtn = new QPushButton(tr("添加当前图"), patchCoreBox);
    m_addSampleBtn->setToolTip(tr("将当前图片加入 PatchCore memory bank"));
    connect(m_addSampleBtn, &QPushButton::clicked, this, &ZeroShotPanel::onAddNormalSample);
    patchCoreBtnLayout->addWidget(m_addSampleBtn);

    m_removeLastBtn = new QPushButton(tr("移除最后"), patchCoreBox);
    m_removeLastBtn->setToolTip(tr("从 memory bank 移除最后添加的样本"));
    connect(m_removeLastBtn, &QPushButton::clicked, this, &ZeroShotPanel::onRemoveLastSample);
    patchCoreBtnLayout->addWidget(m_removeLastBtn);

    m_clearSamplesBtn = new QPushButton(tr("清空全部"), patchCoreBox);
    m_clearSamplesBtn->setToolTip(tr("清空 PatchCore memory bank"));
    connect(m_clearSamplesBtn, &QPushButton::clicked, this, &ZeroShotPanel::onClearSamples);
    patchCoreBtnLayout->addWidget(m_clearSamplesBtn);
    patchCoreLayout->addLayout(patchCoreBtnLayout);

    QHBoxLayout* progressiveLayout = new QHBoxLayout();
    progressiveLayout->setContentsMargins(0, 0, 0, 0);
    progressiveLayout->addWidget(new QLabel(tr("渐进式切换阈值:"), patchCoreBox));
    m_progressiveThresholdSpin = new QSpinBox(patchCoreBox);
    m_progressiveThresholdSpin->setRange(1, 100);
    m_progressiveThresholdSpin->setValue(10);
    m_progressiveThresholdSpin->setToolTip(tr("当 memory bank 样本数超过此值时切换到 PatchCore"));
    progressiveLayout->addWidget(m_progressiveThresholdSpin);
    patchCoreLayout->addLayout(progressiveLayout);

    m_patchCoreGroup = patchCoreBox;
    m_patchCoreGroup->setVisible(false);
    layout->addWidget(m_patchCoreGroup);

    layout->addStretch();
    // Task 2 修复：内容直接加入零样本面板布局，由外部滚动区统一滚动
    outerLayout->addWidget(content);
}

// ============================================================================
// 根据模型类型动态显示/隐藏控件
// ============================================================================
void ZeroShotPanel::updateControlVisibility() {
    const zsu::ZeroShotModelType type = modelType();

    // 异常阈值行（滑块父控件即整行容器）
    QWidget* anomalyRow = m_anomalyThresholdSlider ? m_anomalyThresholdSlider->parentWidget() : nullptr;
    // 检测阈值行
    QWidget* detectionRow = m_detectionThresholdSlider ? m_detectionThresholdSlider->parentWidget() : nullptr;

    // 默认全部隐藏，按类型开启
    bool showAnomaly = false;
    bool showDetection = false;
    bool showPatchCore = false;

    switch (type) {
    case zsu::ZeroShotModelType::AnomalyCLIP:
        // 显示异常阈值，隐藏检测阈值与 PatchCore 区
        showAnomaly = true;
        break;
    case zsu::ZeroShotModelType::GroundingDINO:
        // 显示检测阈值，隐藏异常阈值与 PatchCore 区
        showDetection = true;
        break;
    case zsu::ZeroShotModelType::MobileSAM:
        // 两个阈值均隐藏，隐藏 PatchCore 区
        break;
    case zsu::ZeroShotModelType::PatchCore:
        // 显示异常阈值与 PatchCore 管理区，隐藏检测阈值
        showAnomaly = true;
        showPatchCore = true;
        break;
    default:
        break;
    }

    if (anomalyRow) anomalyRow->setVisible(showAnomaly);
    if (detectionRow) detectionRow->setVisible(showDetection);
    if (m_patchCoreGroup) m_patchCoreGroup->setVisible(showPatchCore);
}

// ============================================================================
// 根据模型类型更新提示词占位符
// ============================================================================
void ZeroShotPanel::updatePromptPlaceholder() {
    if (!m_promptsEdit) return;

    QString placeholder;
    switch (modelType()) {
    case zsu::ZeroShotModelType::AnomalyCLIP:
        placeholder = tr("每行一个提示词:\nnormal: a photo of a normal product\nanomaly: a photo of a damaged product");
        break;
    case zsu::ZeroShotModelType::GroundingDINO:
        placeholder = tr("点号分隔: scratch . dent . stain");
        break;
    case zsu::ZeroShotModelType::MobileSAM:
        placeholder = tr("（MobileSAM 不需要文本提示词）");
        break;
    case zsu::ZeroShotModelType::PatchCore:
        placeholder = tr("（PatchCore 不需要文本提示词）");
        break;
    default:
        break;
    }
    m_promptsEdit->setPlaceholderText(placeholder);
}

// ============================================================================
// 模型类型枚举 → 可读字符串
// ============================================================================
QString ZeroShotPanel::modelTypeToString(zsu::ZeroShotModelType type) const {
    switch (type) {
    case zsu::ZeroShotModelType::AnomalyCLIP:   return tr("AnomalyCLIP");
    case zsu::ZeroShotModelType::GroundingDINO: return tr("Grounding DINO");
    case zsu::ZeroShotModelType::MobileSAM:     return tr("MobileSAM");
    case zsu::ZeroShotModelType::PatchCore:     return tr("PatchCore");
    case zsu::ZeroShotModelType::OpenCLIP:      return tr("OpenCLIP");
    default: return tr("未知");
    }
}

// ============================================================================
// 配置读取
// ============================================================================
zsu::ZeroShotModelType ZeroShotPanel::modelType() const {
    if (!m_modelTypeCombo) return zsu::ZeroShotModelType::Unknown;
    int value = m_modelTypeCombo->currentData().toInt();
    return static_cast<zsu::ZeroShotModelType>(value);
}

QString ZeroShotPanel::modelPath() const {
    return m_modelPathEdit ? m_modelPathEdit->text().trimmed() : QString();
}

QStringList ZeroShotPanel::textPrompts() const {
    if (!m_promptsEdit) return {};
    // v2.0 阶段二 Task 7：支持两种分隔符
    //   - 分号 ; ：用户手动输入
    //   - " . " ：目标类型多选后自动拼接（GroundingDINO 风格）
    // 解析时先将 " . " 统一替换为 ";"，再按 ";" 分割
    QString text = m_promptsEdit->text();
    text.replace(QStringLiteral(" . "), QStringLiteral(";"));
    QStringList raw = text.split(';', Qt::SkipEmptyParts);
    QStringList result;
    for (const QString& line : raw) {
        QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) result << trimmed;
    }
    return result;
}

float ZeroShotPanel::anomalyThreshold() const {
    return m_anomalyThresholdSlider ? m_anomalyThresholdSlider->value() / 100.0f : 0.5f;
}

float ZeroShotPanel::detectionThreshold() const {
    return m_detectionThresholdSlider ? m_detectionThresholdSlider->value() / 100.0f : 0.3f;
}

bool ZeroShotPanel::useQuantized() const {
    return m_quantizedCheck ? m_quantizedCheck->isChecked() : false;
}

int ZeroShotPanel::progressiveSwitchThreshold() const {
    return m_progressiveThresholdSpin ? m_progressiveThresholdSpin->value() : 10;
}

// ============================================================================
// 引擎注入与状态更新
// ============================================================================
void ZeroShotPanel::setEngine(zsu::ZeroShotEngine* engine) {
    m_engine = engine;
    if (m_engine) {
        // 同步当前面板配置到引擎
        m_engine->setAnomalyThreshold(anomalyThreshold());
        m_engine->setDetectionThreshold(detectionThreshold());
        m_engine->setUseQuantizedModel(useQuantized());
        m_engine->setProgressiveSwitchThreshold(progressiveSwitchThreshold());
        m_engine->setTextPrompts(textPrompts());
        // 同步准确性保障配置
        zsu::StabilityConfig sc;
        sc.enableMultiRunStability = multiRunStabilityEnabled();
        sc.numRuns = multiRunCount();
        sc.nmsIouThreshold = nmsIouThreshold();
        sc.confidenceThreshold = detectionThreshold();
        m_engine->setStabilityConfig(sc);
        // 若引擎已加载模型，同步状态显示
        updateModelStatus(m_engine->isModelLoaded(),
                          modelTypeToString(m_engine->modelType()));
    }
}

void ZeroShotPanel::updatePatchCoreSampleCount(int count) {
    if (m_sampleCountLabel) {
        m_sampleCountLabel->setText(tr("样本数: %1").arg(count));
    }
}

void ZeroShotPanel::updateModelStatus(bool loaded, const QString& modelTypeName, const QString& errorMsg) {
    if (!m_modelStatusLabel) return;

    if (loaded) {
        m_modelStatusLabel->setText(tr("已加载: %1").arg(modelTypeName));
        m_modelStatusLabel->setStyleSheet("color: green;");
    } else if (!errorMsg.isEmpty()) {
        m_modelStatusLabel->setText(tr("加载失败: %1").arg(errorMsg));
        m_modelStatusLabel->setStyleSheet("color: red;");
    } else {
        m_modelStatusLabel->setText(tr("未加载模型"));
        m_modelStatusLabel->setStyleSheet("color: gray;");
    }
}

// ============================================================================
// 私有槽实现
// ============================================================================
void ZeroShotPanel::onModelTypeChanged(int /*index*/) {
    // 模型类型变化时刷新可见性与占位符
    updateControlVisibility();
    updatePromptPlaceholder();
    // 同步到引擎
    if (m_engine) {
        // 仅刷新配置，不触发加载
        m_engine->setTextPrompts(textPrompts());
    }
    emit settingsChanged();
}

void ZeroShotPanel::onBrowseModel() {
    // 起始目录：可执行文件目录上溯两级 + resources/models/
    const QString basePath = QCoreApplication::applicationDirPath() + "/../../resources/models/";

    // 根据模型类型选择子目录
    QString subDir;
    const zsu::ZeroShotModelType type = modelType();
    if (type == zsu::ZeroShotModelType::AnomalyCLIP ||
        type == zsu::ZeroShotModelType::PatchCore) {
        subDir = "clip";
    } else if (type == zsu::ZeroShotModelType::GroundingDINO ||
               type == zsu::ZeroShotModelType::MobileSAM) {
        subDir = "grounding_sam";
    }

    QString startDir = basePath + subDir;
    if (!QDir(startDir).exists()) {
        // 子目录不存在时退回到 models 根目录
        startDir = basePath;
    }

    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("选择模型目录"), startDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        m_modelPathEdit->setText(dir);
        emit settingsChanged();
    }
}

void ZeroShotPanel::onLoadModel() {
    const zsu::ZeroShotModelType type = modelType();
    const QString path = modelPath();

    if (path.isEmpty()) {
        updateModelStatus(false, QString(), tr("模型路径为空"));
        ZSU_LOG_WARN("ZeroShotPanel: 模型路径为空，取消加载");
        return;
    }

    ZSU_LOG_INFO(QString("ZeroShotPanel: 请求加载模型 %1，路径: %2")
                     .arg(modelTypeToString(type)).arg(path));

    // 若引擎已注入，直接调用引擎加载
    if (m_engine) {
        m_engine->setUseQuantizedModel(useQuantized());
        const bool ok = m_engine->loadModel(type, path);
        if (ok) {
            updateModelStatus(true, modelTypeToString(type));
            ZSU_LOG_INFO(QString("ZeroShotPanel: 模型加载成功 (%1)").arg(modelTypeToString(type)));
        } else {
            // 展示详细失败原因 + 改进建议（替代原先笼统的"引擎加载失败"）
            const QString detail = m_engine->lastError();
            updateModelStatus(false, detail.isEmpty() ? QString()
                                                      : QStringLiteral("引擎加载失败"));
            ZSU_LOG_ERROR(QString("ZeroShotPanel: 引擎加载模型失败 (%1): %2")
                              .arg(modelTypeToString(type)).arg(path));
            if (detail.isEmpty()) {
                QMessageBox::warning(this, tr("模型加载失败"),
                    tr("模型加载失败，未获取到具体原因。\n请检查终端 / logs 目录中的日志。"));
            } else {
                QMessageBox::warning(this, tr("模型加载失败"),
                    tr("%1\n\n（详细日志见终端及 logs 目录）").arg(detail));
            }
        }
    }

    // 通知外部（TrainingInferenceView）也感知加载请求
    emit modelLoadRequested(type, path);
}

void ZeroShotPanel::onInferCurrent() {
    ZSU_LOG_INFO("ZeroShotPanel: 推理当前");
    emit inferenceRequested();
}

void ZeroShotPanel::onInferAll() {
    ZSU_LOG_INFO("ZeroShotPanel: 推理全部");
    emit inferenceAllRequested();
}

void ZeroShotPanel::onStop() {
    ZSU_LOG_INFO("ZeroShotPanel: 请求停止推理");
    emit stopRequested();
}

void ZeroShotPanel::onAddNormalSample() {
    ZSU_LOG_INFO("ZeroShotPanel: 添加正常样本");
    emit addNormalSampleRequested();
}

void ZeroShotPanel::onRemoveLastSample() {
    ZSU_LOG_INFO("ZeroShotPanel: 移除最后样本");
    emit removeLastNormalSampleRequested();
}

void ZeroShotPanel::onClearSamples() {
    ZSU_LOG_INFO("ZeroShotPanel: 清空全部样本");
    emit clearNormalSamplesRequested();
}

void ZeroShotPanel::onAnomalyThresholdChanged(int value) {
    // 滑块 0-100 → 显示 0.00-1.00
    const float v = value / 100.0f;
    m_anomalyThresholdValueLabel->setText(QString::number(v, 'f', 2));
    if (m_engine) {
        m_engine->setAnomalyThreshold(v);
    }
    emit settingsChanged();
}

void ZeroShotPanel::onDetectionThresholdChanged(int value) {
    const float v = value / 100.0f;
    m_detectionThresholdValueLabel->setText(QString::number(v, 'f', 2));
    if (m_engine) {
        m_engine->setDetectionThreshold(v);
    }
    emit settingsChanged();
}

void ZeroShotPanel::onQuantizedToggled(bool checked) {
    ZSU_LOG_INFO(QString("ZeroShotPanel: 量化模型开关 -> %1").arg(checked ? "开" : "关"));
    if (m_engine) {
        m_engine->setUseQuantizedModel(checked);
    }
    emit settingsChanged();
}

// ============================================================================
// 准确性保障配置读取
// ============================================================================
bool ZeroShotPanel::multiRunStabilityEnabled() const {
    return m_multiRunCheck ? m_multiRunCheck->isChecked() : false;
}

int ZeroShotPanel::multiRunCount() const {
    return m_multiRunSpin ? m_multiRunSpin->value() : 3;
}

float ZeroShotPanel::nmsIouThreshold() const {
    return m_nmsThresholdSlider ? m_nmsThresholdSlider->value() / 100.0f : 0.45f;
}

bool ZeroShotPanel::humanReviewEnabled() const {
    return m_humanReviewCheck ? m_humanReviewCheck->isChecked() : false;
}

void ZeroShotPanel::setModelNotesManager(zsu::ModelNotesManager* manager) {
    m_notesManager = manager;
}

void ZeroShotPanel::setBadCaseRecorder(zsu::BadCaseRecorder* recorder) {
    m_badCaseRecorder = recorder;
}

// ============================================================================
// 模型注意事项按钮
// ============================================================================
void ZeroShotPanel::onShowModelNotes() {
    if (!m_notesManager) {
        ZSU_LOG_WARN("ZeroShotPanel: 模型注意事项管理器未初始化");
        return;
    }
    // 弹出注意事项对话框
    ModelNotesDialog dialog(m_notesManager, modelType(), this);
    dialog.exec();
}

// ============================================================================
// 多次推理开关
// ============================================================================
void ZeroShotPanel::onMultiRunToggled(bool checked) {
    if (m_multiRunSpin) {
        m_multiRunSpin->setEnabled(checked);
    }
    // 同步到引擎
    if (m_engine) {
        zsu::StabilityConfig sc = m_engine->stabilityConfig();
        sc.enableMultiRunStability = checked;
        sc.numRuns = multiRunCount();
        m_engine->setStabilityConfig(sc);
    }
    ZSU_LOG_INFO(QString("ZeroShotPanel: 多次推理取稳定值 -> %1").arg(checked ? "开" : "关"));
    emit settingsChanged();
}

// ============================================================================
// NMS IoU 阈值变化
// ============================================================================
void ZeroShotPanel::onNmsThresholdChanged(int value) {
    const float v = value / 100.0f;
    if (m_nmsThresholdValueLabel) {
        m_nmsThresholdValueLabel->setText(QString::number(v, 'f', 2));
    }
    // 同步到引擎
    if (m_engine) {
        zsu::StabilityConfig sc = m_engine->stabilityConfig();
        sc.nmsIouThreshold = v;
        m_engine->setStabilityConfig(sc);
    }
    emit settingsChanged();
}

// ============================================================================
// v2.0 阶段二 Task 7：目标类型选择区（共享提示词库）
// ============================================================================

void ZeroShotPanel::setTargetTypeEntries(const QVariantList& entries) {
    // 保存当前已选中的 prompt 列表，用于重建菜单后恢复选中状态
    QStringList previouslySelected = selectedTargetPrompts();

    m_targetTypeEntries = entries;
    rebuildTargetTypeMenu();

    // 恢复选中状态：遍历菜单中的 action，若其 prompt 在 previouslySelected 中则勾选
    if (m_targetTypeMenu) {
        const QList<QAction*> actions = m_targetTypeMenu->actions();
        for (QAction* act : actions) {
            QString prompt = act->property("prompt").toString();
            if (!prompt.isEmpty() && previouslySelected.contains(prompt)) {
                act->blockSignals(true);
                act->setChecked(true);
                act->blockSignals(false);
            }
        }
    }

    // 更新按钮文本
    if (m_targetTypeBtn) {
        m_targetTypeBtn->setText(currentTargetTypeSummary());
    }
}

QStringList ZeroShotPanel::selectedTargetPrompts() const {
    QStringList result;
    if (!m_targetTypeMenu) return result;
    const QList<QAction*> actions = m_targetTypeMenu->actions();
    for (QAction* act : actions) {
        if (act->isCheckable() && act->isChecked()) {
            QString prompt = act->property("prompt").toString();
            if (!prompt.isEmpty()) {
                result << prompt;
            }
        }
    }
    return result;
}

void ZeroShotPanel::rebuildTargetTypeMenu() {
    if (!m_targetTypeMenu) return;
    m_targetTypeMenu->clear();

    // 按来源分组：builtin → custom → recent
    QString currentGroup;
    for (const QVariant& v : m_targetTypeEntries) {
        QVariantMap entry = v.toMap();
        QString source = entry.value("source").toString();
        QString groupName;
        if (source == QStringLiteral("builtin")) {
            groupName = tr("内置（12 类默认缺陷）");
        } else if (source == QStringLiteral("custom")) {
            groupName = tr("自定义");
        } else if (source == QStringLiteral("recent")) {
            groupName = tr("最近使用");
        } else {
            groupName = tr("其他");
        }

        // 分组分隔符
        if (currentGroup != groupName) {
            if (!currentGroup.isEmpty()) {
                m_targetTypeMenu->addSeparator();
            }
            // 添加分组标题（不可勾选）
            QAction* titleAct = m_targetTypeMenu->addAction(groupName);
            titleAct->setEnabled(false);
            QFont font = titleAct->font();
            font.setBold(true);
            titleAct->setFont(font);
            currentGroup = groupName;
        }

        // 添加可勾选项
        QString prompt = entry.value("prompt").toString();
        QString cnName = entry.value("cnName").toString();
        QString scene  = entry.value("scene").toString();
        QString displayText = cnName.isEmpty() ? prompt : cnName;
        // v2.0 阶段五 Task 13：已训练专用模型的类别在显示文本前加"✔"标记
        bool hasSpecialized = entry.value("hasSpecializedModel", false).toBool();
        if (hasSpecialized) {
            displayText = QStringLiteral("\xe2\x9c\x94 ") + displayText;  // ✔ 符号
        }
        if (!scene.isEmpty()) {
            displayText += QStringLiteral("  (%1)").arg(scene);
        }
        if (!cnName.isEmpty() && cnName != prompt) {
            displayText += QStringLiteral(" [%1]").arg(prompt);
        }

        QAction* act = m_targetTypeMenu->addAction(displayText);
        act->setCheckable(true);
        act->setProperty("prompt", prompt);
        act->setProperty("name", entry.value("name"));
        act->setProperty("cnName", cnName);
        act->setProperty("hasSpecializedModel", hasSpecialized);
        act->setProperty("specializedModelPath", entry.value("specializedModelPath"));
        act->setToolTip(QStringLiteral("%1: %2").arg(prompt).arg(scene));
        connect(act, &QAction::toggled, this, &ZeroShotPanel::onTargetTypeToggled);
    }

    // 若没有任何条目，显示占位
    if (m_targetTypeEntries.isEmpty()) {
        QAction* empty = m_targetTypeMenu->addAction(tr("（提示词库为空，请点击 [自定义...] 添加）"));
        empty->setEnabled(false);
    }
}

void ZeroShotPanel::onTargetTypeToggled() {
    // 更新按钮文本
    if (m_targetTypeBtn) {
        m_targetTypeBtn->setText(currentTargetTypeSummary());
    }
    // 将选中项拼接为提示词串，填入提示词输入框
    syncPromptsFromSelection();

    QStringList selected = selectedTargetPrompts();
    emit targetTypeSelectionChanged(selected);
    emit settingsChanged();

    // v2.0 阶段五 Task 13：训练按钮仅在选中单一类别时启用
    // （训练数据集需绑定到具体类别，多选时禁用避免数据集混乱）
    if (m_trainSpecializedBtn) {
        m_trainSpecializedBtn->setEnabled(selected.size() == 1);
    }
}

void ZeroShotPanel::syncPromptsFromSelection() {
    if (!m_promptsEdit) return;
    QStringList selected = selectedTargetPrompts();
    // 用 " . " 分隔拼接（GroundingDINO 风格）
    QString joined = selected.join(QStringLiteral(" . "));
    // 设置标记，避免 textChanged 触发 onPromptsEdited 误处理
    m_syncingPrompts = true;
    m_promptsEdit->setText(joined);
    m_syncingPrompts = false;

    // 同步到引擎
    if (m_engine) {
        m_engine->setTextPrompts(textPrompts());
    }
}

QString ZeroShotPanel::currentTargetTypeSummary() const {
    QStringList selected = selectedTargetPrompts();
    if (selected.isEmpty()) {
        return tr("选择目标类型...");
    }
    // 显示选中数量 + 前两项预览
    if (selected.size() <= 2) {
        return selected.join(QStringLiteral(", "));
    }
    return tr("%1 项已选: %2 ...")
        .arg(selected.size())
        .arg(selected.first());
}

void ZeroShotPanel::onPromptsEdited(const QString& text) {
    // 避免程序设置文本时误触发
    if (m_syncingPrompts) return;

    // 用户手动编辑提示词后，取消菜单中所有勾选（避免状态不一致）
    // 因为用户手动输入的内容可能与目标类型选择不匹配
    if (m_targetTypeMenu) {
        const QList<QAction*> actions = m_targetTypeMenu->actions();
        for (QAction* act : actions) {
            if (act->isCheckable() && act->isChecked()) {
                act->blockSignals(true);
                act->setChecked(false);
                act->blockSignals(false);
            }
        }
    }
    if (m_targetTypeBtn) {
        m_targetTypeBtn->setText(currentTargetTypeSummary());
    }

    // v2.0 阶段五 Task 13：手动编辑后训练按钮禁用（无选中类别）
    if (m_trainSpecializedBtn) {
        m_trainSpecializedBtn->setEnabled(false);
    }

    // 同步到引擎
    if (m_engine) {
        m_engine->setTextPrompts(textPrompts());
    }
    emit settingsChanged();
}

void ZeroShotPanel::onCustomTargetType() {
    // 弹出对话框：类别名 + 提示词 + 中文名 + 场景 + 可选示例图路径
    QDialog dialog(this);
    dialog.setWindowTitle(tr("添加自定义目标类型"));
    dialog.setMinimumWidth(380);

    QFormLayout* form = new QFormLayout(&dialog);
    form->setSpacing(8);
    form->setContentsMargins(12, 12, 12, 12);

    QLineEdit* nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText(tr("如: oil_stain（英文标识，不可与内置重复）"));
    form->addRow(tr("类别名:"), nameEdit);

    QLineEdit* promptEdit = new QLineEdit(&dialog);
    promptEdit->setPlaceholderText(tr("如: oil stain（英文提示词，必填）"));
    form->addRow(tr("提示词:"), promptEdit);

    QLineEdit* cnNameEdit = new QLineEdit(&dialog);
    cnNameEdit->setPlaceholderText(tr("如: 油渍（可选）"));
    form->addRow(tr("中文名:"), cnNameEdit);

    QLineEdit* sceneEdit = new QLineEdit(&dialog);
    sceneEdit->setPlaceholderText(tr("如: 表面油污检测（可选）"));
    form->addRow(tr("适用场景:"), sceneEdit);

    // 示例图路径（可选）
    QHBoxLayout* imgRow = new QHBoxLayout();
    QLineEdit* imgPathEdit = new QLineEdit(&dialog);
    imgPathEdit->setPlaceholderText(tr("可选，仅元数据记录"));
    QPushButton* imgBrowseBtn = new QPushButton(tr("浏览..."), &dialog);
    imgRow->addWidget(imgPathEdit, 1);
    imgRow->addWidget(imgBrowseBtn);
    form->addRow(tr("示例图:"), imgRow);

    connect(imgBrowseBtn, &QPushButton::clicked, [&]() {
        const QString path = QFileDialog::getOpenFileName(
            &dialog, tr("选择示例图"), QString(),
            tr("图片文件 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;所有文件 (*)"));
        if (!path.isEmpty()) {
            imgPathEdit->setText(path);
        }
    });

    // 按钮
    QDialogButtonBox* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    btns->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    btns->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    form->addRow(btns);

    connect(btns, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // OK 按钮初始禁用，name/prompt 非空时启用
    btns->button(QDialogButtonBox::Ok)->setEnabled(false);
    auto updateOkState = [&]() {
        bool ok = !nameEdit->text().trimmed().isEmpty() &&
                  !promptEdit->text().trimmed().isEmpty();
        btns->button(QDialogButtonBox::Ok)->setEnabled(ok);
    };
    connect(nameEdit, &QLineEdit::textChanged, updateOkState);
    connect(promptEdit, &QLineEdit::textChanged, updateOkState);

    if (dialog.exec() == QDialog::Accepted) {
        QVariantMap entry;
        entry["name"]   = nameEdit->text().trimmed();
        entry["prompt"] = promptEdit->text().trimmed();
        entry["cnName"] = cnNameEdit->text().trimmed();
        entry["scene"]  = sceneEdit->text().trimmed();
        entry["exampleImagePath"] = imgPathEdit->text().trimmed();
        // 通知外部保存到 PromptLibrary::addCustom 并刷新 entries
        emit customTargetTypeRequested(entry);
    }
}

// ============================================================================
// v2.0 阶段五 Task 13：训练专用模型入口
// 当用户选中单一类别后点击"训练专用模型..."按钮，发射 specializedTrainingRequested
// 信号，由 ZeroShotDetectView 接收后弹出 DataCollectionWizard 数据收集向导。
// 训练完成后，ZeroShotDetectView 调用 PromptLibrary::setSpecializedModel 标记该类别，
// 重建菜单时本类的 rebuildTargetTypeMenu 会自动在显示文本前加"✔"。
// ============================================================================

void ZeroShotPanel::onTrainSpecializedModel() {
    QStringList selected = selectedTargetPrompts();
    if (selected.size() != 1) {
        // 防御性：按钮已禁用此情况，但仍在运行时校验
        QMessageBox::information(this, tr("训练专用模型"),
            tr("请先在目标类型下拉中选择且仅选择一个类别。"));
        return;
    }

    // 从菜单中取出该 prompt 对应的完整 entry 信息
    QString targetPrompt = selected.first();
    QVariantMap entry;
    if (m_targetTypeMenu) {
        const QList<QAction*> actions = m_targetTypeMenu->actions();
        for (QAction* act : actions) {
            if (act->isCheckable() && act->isChecked() &&
                act->property("prompt").toString() == targetPrompt) {
                entry["name"]                  = act->property("name");
                entry["prompt"]                = act->property("prompt");
                entry["cnName"]                = act->property("cnName");
                entry["hasSpecializedModel"]   = act->property("hasSpecializedModel");
                entry["specializedModelPath"]  = act->property("specializedModelPath");
                break;
            }
        }
    }

    if (entry.isEmpty()) {
        QMessageBox::warning(this, tr("训练专用模型"),
            tr("无法获取选中类别的元数据，请重试。"));
        return;
    }

    // 若已有专用模型，提示用户确认"重训/覆盖"
    bool hasExisting = entry.value("hasSpecializedModel", false).toBool();
    if (hasExisting) {
        QString existingPath = entry.value("specializedModelPath").toString();
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, tr("重训专用模型"),
            tr("该类别已有专用模型（%1）。\n重新训练将覆盖现有专用模型标记。\n\n是否继续？")
                .arg(existingPath),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
    }

    // 发射信号，由 ZeroShotDetectView 接收并弹出 DataCollectionWizard
    emit specializedTrainingRequested(entry);
}

// ============================================================================
// v2.0 阶段七 Task 18：模型源监控集成（依赖注入方案）
// ZeroShotPanel 通过 setModelEntries 接收外部传入的模型清单，
// 通过 modelSelectionChanged 信号通知外部，保持 ZeroShotKit CMake 独立性。
// ============================================================================

void ZeroShotPanel::setModelEntries(const QVariantList& entries) {
    // 保存当前已选中的模型路径，用于重建下拉后恢复选中状态
    QString previouslySelectedPath;
    if (m_modelCombo && m_modelCombo->currentIndex() >= 0) {
        previouslySelectedPath = m_modelCombo->currentData().toMap()
                                     .value("path").toString();
    }

    m_modelEntries = entries;
    rebuildModelCombo();

    // 恢复选中状态：遍历下拉项，匹配 path 则设为当前
    if (m_modelCombo && !previouslySelectedPath.isEmpty()) {
        for (int i = 0; i < m_modelCombo->count(); ++i) {
            QVariantMap data = m_modelCombo->itemData(i).toMap();
            if (data.value("path").toString() == previouslySelectedPath) {
                m_syncingModelCombo = true;
                m_modelCombo->setCurrentIndex(i);
                m_syncingModelCombo = false;
                break;
            }
        }
    }
}

void ZeroShotPanel::setLmStudioStatusHint(const QString& hint) {
    if (!m_lmStudioHintLabel) return;
    if (hint.isEmpty()) {
        m_lmStudioHintLabel->setVisible(false);
        m_lmStudioHintLabel->setText(QString());
    } else {
        m_lmStudioHintLabel->setText(hint);
        m_lmStudioHintLabel->setVisible(true);
    }
}

void ZeroShotPanel::rebuildModelCombo() {
    if (!m_modelCombo) return;

    // 标记程序重建，避免 currentIndexChanged 误触发 onModelSelectionChanged
    m_syncingModelCombo = true;
    m_modelCombo->clear();

    // 按 sourceType 分组：lmstudio → local → trained
    struct GroupInfo {
        QString sourceType;
        QString title;
    };
    const QList<GroupInfo> groups = {
        {QStringLiteral("lmstudio"), tr("— LM Studio —")},
        {QStringLiteral("local"),    tr("— 本地 —")},
        {QStringLiteral("trained"),  tr("— 训练产物 —")}
    };

    for (const GroupInfo& g : groups) {
        // 收集该分组的模型条目
        QVariantList groupEntries;
        for (const QVariant& v : m_modelEntries) {
            QVariantMap entry = v.toMap();
            if (entry.value("sourceType").toString() == g.sourceType) {
                groupEntries.append(entry);
            }
        }
        if (groupEntries.isEmpty()) continue;

        // 添加分组标题项（带 data 为空，用于在 onModelSelectionChanged 中识别为不可选）
        m_modelCombo->addItem(g.title, QVariant());
        // 紧接着插入 separator，Qt 会自动把前一项渲染为不可选分隔条样式
        m_modelCombo->insertSeparator(m_modelCombo->count() - 1);

        // 添加该分组下的模型项
        for (const QVariant& v : groupEntries) {
            QVariantMap entry = v.toMap();
            QString name = entry.value("name").toString();
            bool isNew = entry.value("isNew", false).toBool();
            // 新模型追加" [新]"标签
            QString displayText = isNew ? (name + QStringLiteral("  [新]")) : name;

            m_modelCombo->addItem(displayText, entry);
        }
    }

    // 若没有任何条目，显示占位
    if (m_modelCombo->count() == 0) {
        m_modelCombo->addItem(tr("（暂无可用模型，请检查模型源路径）"), QVariant());
    }

    m_syncingModelCombo = false;
}

void ZeroShotPanel::onModelSelectionChanged(int index) {
    // 程序重建下拉时不触发
    if (m_syncingModelCombo) return;
    if (index < 0 || !m_modelCombo) return;

    QVariantMap data = m_modelCombo->itemData(index).toMap();
    if (data.isEmpty()) return;  // 分组标题项无 data

    QString path = data.value("path").toString();
    QString sourceType = data.value("sourceType").toString();

    if (path.isEmpty()) return;

    // 自动填入模型路径输入框（用户可后续微调）
    if (m_modelPathEdit) {
        m_modelPathEdit->setText(path);
    }

    ZSU_LOG_INFO(QString("ZeroShotPanel: 选中模型 %1 (来源: %2)").arg(path).arg(sourceType));

    // 通知外部（ZeroShotDetectView 可据此记录日志/加载模型等）
    emit modelSelectionChanged(path, sourceType);
    emit settingsChanged();
}
