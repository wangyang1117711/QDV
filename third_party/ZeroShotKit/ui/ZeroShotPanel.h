#pragma once

#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include "ZeroShotKit/ZeroShotTypes.h"  // zsu::ZeroShotModelType
#include "ZeroShotKit/ZeroShotEngine.h"  // zsu::ZeroShotEngine

class QToolButton;
class QMenu;
class QAction;

namespace zsu {
class ModelNotesManager;
class BadCaseRecorder;
}

namespace QDVMini {

// 零样本配置面板：提供 AnomalyCLIP / GroundingDINO / MobileSAM / PatchCore
// 的模型加载、阈值调整、提示词输入与正常样本管理界面。
// v2.0 阶段二 Task 7：新增"目标类型"多选下拉（共享提示词库），通过依赖注入接收数据。
class ZeroShotPanel : public QWidget {
    Q_OBJECT

public:
    explicit ZeroShotPanel(QWidget* parent = nullptr);

    // --- 获取当前配置 ---
    zsu::ZeroShotModelType modelType() const;
    QString modelPath() const;
    QStringList textPrompts() const;
    float anomalyThreshold() const;
    float detectionThreshold() const;
    bool useQuantized() const;
    int progressiveSwitchThreshold() const;

    // --- 准确性保障配置 ---
    bool multiRunStabilityEnabled() const;
    int multiRunCount() const;
    float nmsIouThreshold() const;
    bool humanReviewEnabled() const;

    // 设置引擎实例（由外部 TrainingInferenceView 注入，非拥有）
    void setEngine(zsu::ZeroShotEngine* engine);

    // 设置模型注意事项管理器
    void setModelNotesManager(zsu::ModelNotesManager* manager);

    // 设置 Bad Case 记录器
    void setBadCaseRecorder(zsu::BadCaseRecorder* recorder);

    // 更新 PatchCore 样本计数显示
    void updatePatchCoreSampleCount(int count);

    // 更新模型状态显示
    void updateModelStatus(bool loaded, const QString& modelTypeName, const QString& errorMsg = QString());

    // 模型类型枚举转中文字符串（供外部状态显示使用）
    QString modelTypeToString(zsu::ZeroShotModelType type) const;

    // ============================================================
    // v2.0 阶段二 Task 7：共享提示词库集成（依赖注入方案）
    // ============================================================

    /// 设置可选目标类型条目（由外部从 PromptLibrary 加载后注入）
    /// @param entries  每项为 QVariantMap: {name, prompt, cnName, scene, source("builtin"/"custom"/"recent")}
    /// 调用后重建"目标类型"下拉菜单，保留当前已选项（若仍存在）
    void setTargetTypeEntries(const QVariantList& entries);

    /// 获取当前选中的目标类型提示词列表（按选中顺序）
    QStringList selectedTargetPrompts() const;

    // ============================================================
    // v2.0 阶段七 Task 18：模型源监控集成（依赖注入方案）
    // ZeroShotPanel 不直接依赖主项目 ModelSourceManager，由
    // ZeroShotDetectView 桥接：ModelSourceManager → QVariantList → setModelEntries
    // ============================================================

    /// 设置可选具体模型条目（由外部从 ModelSourceManager 加载后注入）
    /// @param entries  每项为 QVariantMap: {name, path, sourceType, size,
    ///                  modifiedTime, parentPath, isNew}
    /// 调用后重建"具体模型"下拉，按 sourceType 分组显示
    /// （LM Studio / 本地 / 训练产物），新模型项追加" [新]"标签
    void setModelEntries(const QVariantList& entries);

    /// 设置 LM Studio 路径可用性提示（路径不存在/为空时由外部传入提示文案，空字符串表示正常）
    /// 非空时在"具体模型"下拉下方显示警告标签
    void setLmStudioStatusHint(const QString& hint);

signals:
    void modelLoadRequested(zsu::ZeroShotModelType type, const QString& path);
    void inferenceRequested();       // 推理当前
    void inferenceAllRequested();    // 推理全部
    void stopRequested();            // 停止
    void addNormalSampleRequested(); // PatchCore 添加当前图
    void removeLastNormalSampleRequested();
    void clearNormalSamplesRequested();
    void settingsChanged();          // 任何配置变化
    void humanReviewToggled(bool enabled); // 人工复核开关

    // v2.0 阶段二 Task 7：提示词库相关信号
    /// 用户请求添加自定义目标类型（外部接收后保存到 PromptLibrary::addCustom 并刷新 entries）
    /// @param entry  QVariantMap: {name, prompt, cnName, scene, exampleImagePath}
    void customTargetTypeRequested(const QVariantMap& entry);
    /// 目标类型选择变化（外部可据此记录 recent）
    /// @param selectedPrompts  当前选中的提示词列表
    void targetTypeSelectionChanged(const QStringList& selectedPrompts);

    // v2.0 阶段五 Task 13：第二层 训练兑底流程入口信号
    /// 用户请求为指定类别训练专用模型（外部接收后弹出 DataCollectionWizard）
    /// @param entry  QVariantMap: {name, prompt, cnName, scene, hasSpecializedModel, specializedModelPath}
    ///               含 hasSpecializedModel=true 时表示已有专用模型，向导可提示"重训/覆盖"
    void specializedTrainingRequested(const QVariantMap& entry);

    // v2.0 阶段七 Task 18：模型选择变化信号
    /// 用户在"具体模型"下拉中选中某模型时发射
    /// @param path        模型完整路径（自动填入模型路径输入框）
    /// @param sourceType  来源类型 lmstudio/local/trained
    void modelSelectionChanged(const QString& path, const QString& sourceType);

private slots:
    void onModelTypeChanged(int index);
    void onBrowseModel();
    void onLoadModel();
    void onInferCurrent();
    void onInferAll();
    void onStop();
    void onAddNormalSample();
    void onRemoveLastSample();
    void onClearSamples();
    void onAnomalyThresholdChanged(int value);
    void onDetectionThresholdChanged(int value);
    void onQuantizedToggled(bool checked);
    void onShowModelNotes();         // 显示模型注意事项
    void onMultiRunToggled(bool checked);
    void onNmsThresholdChanged(int value);

    // v2.0 阶段二 Task 7：目标类型选择区槽
    void onTargetTypeToggled();      // 菜单项勾选变化 → 拼接提示词串
    void onCustomTargetType();       // "自定义..."按钮 → 弹出对话框
    void onPromptsEdited(const QString& text);  // 提示词框手动微调

    // v2.0 阶段五 Task 13：训练专用模型入口槽
    void onTrainSpecializedModel();  // "训练专用模型..."按钮 → 发射 specializedTrainingRequested 信号

    // v2.0 阶段七 Task 18：具体模型下拉选择槽
    void onModelSelectionChanged(int index);

private:
    void setupUI();
    void updateControlVisibility();  // 根据模型类型动态显示/隐藏控件
    void updatePromptPlaceholder();  // 根据模型类型更新提示词占位符

    // v2.0 阶段二 Task 7：目标类型选择区辅助
    void rebuildTargetTypeMenu();    // 根据 m_targetTypeEntries 重建菜单
    void syncPromptsFromSelection(); // 将选中项拼接为提示词串填入 m_promptsEdit
    QString currentTargetTypeSummary() const;  // 当前选中项的摘要（用于按钮文本）

    // v2.0 阶段七 Task 18：具体模型下拉辅助
    void rebuildModelCombo();        // 根据 m_modelEntries 重建下拉（分组显示）

    // --- 控件指针 ---
    QComboBox* m_modelTypeCombo = nullptr;
    QPushButton* m_modelNotesBtn = nullptr;   // ℹ️ 信息按钮
    QLineEdit* m_modelPathEdit = nullptr;
    QPushButton* m_browseBtn = nullptr;
    QLineEdit* m_promptsEdit = nullptr;
    QSlider* m_anomalyThresholdSlider = nullptr;
    QLabel* m_anomalyThresholdValueLabel = nullptr;
    QSlider* m_detectionThresholdSlider = nullptr;
    QLabel* m_detectionThresholdValueLabel = nullptr;
    QCheckBox* m_quantizedCheck = nullptr;
    QPushButton* m_loadModelBtn = nullptr;
    QLabel* m_modelStatusLabel = nullptr;
    // 推理按钮
    QPushButton* m_inferCurrentBtn = nullptr;
    QPushButton* m_inferAllBtn = nullptr;
    QPushButton* m_stopBtn = nullptr;
    // PatchCore 管理区
    QWidget* m_patchCoreGroup = nullptr;
    QLabel* m_sampleCountLabel = nullptr;
    QPushButton* m_addSampleBtn = nullptr;
    QPushButton* m_removeLastBtn = nullptr;
    QPushButton* m_clearSamplesBtn = nullptr;
    QSpinBox* m_progressiveThresholdSpin = nullptr;
    // 准确性保障区
    QWidget* m_accuracyGroup = nullptr;
    QCheckBox* m_multiRunCheck = nullptr;
    QSpinBox* m_multiRunSpin = nullptr;
    QSlider* m_nmsThresholdSlider = nullptr;
    QLabel* m_nmsThresholdValueLabel = nullptr;
    QCheckBox* m_humanReviewCheck = nullptr;

    // v2.0 阶段二 Task 7：目标类型选择区控件
    QToolButton* m_targetTypeBtn = nullptr;     // 目标类型多选下拉按钮
    QMenu*       m_targetTypeMenu = nullptr;    // 多选菜单（含分组 + 可勾选项）
    QPushButton* m_customTypeBtn = nullptr;     // "自定义..."按钮
    QVariantList m_targetTypeEntries;           // 当前置项数据（由 setTargetTypeEntries 注入）

    // v2.0 阶段五 Task 13：训练专用模型入口控件
    QPushButton* m_trainSpecializedBtn = nullptr;  // "训练专用模型..."按钮（在自定义按钮旁）

    // v2.0 阶段七 Task 18：具体模型下拉控件
    QComboBox*   m_modelCombo = nullptr;        // 具体模型下拉（按来源分组）
    QLabel*      m_lmStudioHintLabel = nullptr; // LM Studio 路径不可用提示
    QVariantList m_modelEntries;                // 当前置项数据（由 setModelEntries 注入）
    bool         m_syncingModelCombo = false;   // 标记：程序重建下拉时避免误触发信号

    // 引擎指针（非拥有）
    zsu::ZeroShotEngine* m_engine = nullptr;
    // 模型注意事项管理器（非拥有）
    zsu::ModelNotesManager* m_notesManager = nullptr;
    // Bad Case 记录器（非拥有）
    zsu::BadCaseRecorder* m_badCaseRecorder = nullptr;

    // 标记：正在通过程序设置提示词（避免 onPromptsEdited 误触发）
    bool m_syncingPrompts = false;
};

} // namespace QDVMini
