#include "TrainingInference/InferencePanel.h"
#include "AI/InferenceEngine.h"
#include "AI/ModelManager.h"
#include "Core/Logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QFileInfo>



InferencePanel::InferencePanel(QWidget* parent) : QWidget(parent) {
    setupUI();
    
    // 连接停止按钮
    connect(m_stopBtn, &QPushButton::clicked, this, &InferencePanel::onStopInference);
    
    // 连接ModelManager的信号
    connect(ModelManager::instance(), &ModelManager::defaultModelLoadFailed,
            this, &InferencePanel::onDefaultModelLoadFailed);
    
    // 初始化模型列表
    refreshModelList();
}

void InferencePanel::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    QLabel* titleLabel = new QLabel("模型推理");
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e0e0e0; background: transparent;");
    layout->addWidget(titleLabel);

    QGroupBox* modelGroup = new QGroupBox("模型选择");
    QVBoxLayout* modelLayout = new QVBoxLayout(modelGroup);

    m_modelCombo = new QComboBox();
    m_modelCombo->setStyleSheet(R"(
        QComboBox {
            background-color: #2d2d2d;
            border: 1px solid #555;
            border-radius: 4px;
            padding: 6px 10px;
            color: #e0e0e0;
            font-size: 13px;
        }
        QComboBox:hover { border-color: #7C4DFF; }
        QComboBox QAbstractItemView {
            background-color: #2d2d2d;
            color: #e0e0e0;
            selection-background-color: #7C4DFF;
            border: 1px solid #555;
        }
    )");
    connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InferencePanel::onModelChanged);
    modelLayout->addWidget(m_modelCombo);
    
    // 添加模型路径显示标签
    m_modelPathLabel = new QLabel("路径: -");
    m_modelPathLabel->setStyleSheet("color: #888; font-size: 11px; padding: 4px 0;");
    m_modelPathLabel->setWordWrap(true);
    modelLayout->addWidget(m_modelPathLabel);

    m_modelInfoList = new QListWidget();
    m_modelInfoList->setMaximumHeight(120);
    m_modelInfoList->setStyleSheet(R"(
        QListWidget {
            background-color: #252525;
            border: 1px solid #444;
            border-radius: 4px;
            color: #aaa;
            font-size: 11px;
            padding: 4px;
        }
    )");
    modelLayout->addWidget(m_modelInfoList);

    layout->addWidget(modelGroup);

    QGroupBox* configGroup = new QGroupBox("推理配置");
    QVBoxLayout* configLayout = new QVBoxLayout(configGroup);

    m_batchCheckBox = new QCheckBox("批量推理模式");
    m_batchCheckBox->setStyleSheet(R"(
        QCheckBox { color: #e0e0e0; font-size: 13px; }
        QCheckBox::indicator { width: 16px; height: 16px; }
    )");
    connect(m_batchCheckBox, &QCheckBox::toggled, this, &InferencePanel::onBatchToggled);
    configLayout->addWidget(m_batchCheckBox);

    layout->addWidget(configGroup);

    m_runBtn = new QPushButton("开始推理");
    m_runBtn->setToolTip("选择模型后点击开始推理，支持批量推理");
    m_runBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #7C4DFF;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 10px 0;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover { background-color: #8E66FF; }
        QPushButton:disabled { background-color: #555; }
    )");
    connect(m_runBtn, &QPushButton::clicked, this, &InferencePanel::onRunInference);
    layout->addWidget(m_runBtn);

    m_stopBtn = new QPushButton("停止推理");
    m_stopBtn->setEnabled(false);
    m_stopBtn->setToolTip("推理进行中时点击可中止推理操作");
    m_stopBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #c62828;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 10px 0;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover { background-color: #e53935; }
        QPushButton:disabled { background-color: #555; }
    )");
    layout->addWidget(m_stopBtn);

    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    m_progressBar->setStyleSheet(R"(
        QProgressBar {
            background-color: #2d2d2d;
            border: 1px solid #444;
            border-radius: 4px;
            height: 18px;
            text-align: center;
            color: #e0e0e0;
        }
        QProgressBar::chunk { background-color: #7C4DFF; border-radius: 3px; }
    )");
    layout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("就绪");
    m_statusLabel->setStyleSheet("color: #aaa; font-size: 12px; padding: 4px;");
    layout->addWidget(m_statusLabel);

    layout->addStretch();
}

void InferencePanel::refreshModelList() {
    // 仅保留 OpenCV DNN 可加载的 .onnx 模型
    QStringList allModels = ModelManager::instance()->getAvailableModelNames();
    QStringList models;
    for (const QString& name : allModels) {
        QString path = ModelManager::instance()->getModelPathByName(name);
        if (path.endsWith(".onnx", Qt::CaseInsensitive)) {
            models << name;
        }
    }

    if (models.isEmpty()) {
        models << "未找到模型";
        m_runBtn->setEnabled(false);
    } else {
        m_runBtn->setEnabled(true);
    }

    m_modelCombo->clear();
    m_modelCombo->addItems(models);
    
    // 优先选择"YOLO"，其次选择包含"yolo"的模型，最后选择默认模型
    int defaultIndex = m_modelCombo->findText("YOLO", Qt::MatchExactly);
    if (defaultIndex < 0) {
        // 查找包含yolo的模型
        for (int i = 0; i < m_modelCombo->count(); ++i) {
            QString text = m_modelCombo->itemText(i).toLower();
            if (text.contains("yolo")) {
                defaultIndex = i;
                break;
            }
        }
    }
    if (defaultIndex < 0) {
        // 最后尝试默认模型名称
        defaultIndex = m_modelCombo->findText(QString::fromLatin1(ModelManager::DEFAULT_MODEL_NAME));
    }
    
    if (defaultIndex >= 0) {
        m_modelCombo->setCurrentIndex(defaultIndex);
    } else if (m_modelCombo->count() > 0) {
        // 没有任何优先匹配项时，默认选中第一个可用模型
        m_modelCombo->setCurrentIndex(0);
    }
}

void InferencePanel::setModelList(const QStringList& models) {
    m_modelCombo->clear();
    m_modelCombo->addItems(models);
}

QString InferencePanel::currentModel() const {
    return m_modelCombo->currentText();
}

QString InferencePanel::currentModelPath() const {
    // 优先使用模型库显式选中的路径，否则通过模型名称反查
    if (!m_explicitModelPath.isEmpty()) {
        return m_explicitModelPath;
    }
    return ModelManager::instance()->getModelPathByName(currentModel());
}

void InferencePanel::setCurrentModelPath(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    m_explicitModelPath = path;

    // 刷新列表，确保包含默认目录下的模型
    refreshModelList();

    // 尝试在现有下拉项中找到匹配的模型名称
    QString baseName = QFileInfo(path).completeBaseName();
    int targetIndex = -1;
    for (int i = 0; i < m_modelCombo->count(); ++i) {
        const QString& name = m_modelCombo->itemText(i);
        if (name == baseName) {
            targetIndex = i;
            break;
        }
        if (ModelManager::instance()->getModelPathByName(name) == path) {
            targetIndex = i;
            break;
        }
    }

    // 如果找不到，则新增一个临时项
    if (targetIndex < 0) {
        m_modelCombo->addItem(baseName);
        targetIndex = m_modelCombo->count() - 1;
    }

    // 阻塞信号，避免 onModelChanged 在设置过程中清除显式路径
    const QSignalBlocker blocker(m_modelCombo);
    m_modelCombo->setCurrentIndex(targetIndex);

    // 手动更新模型信息展示
    updateModelInfo(m_modelCombo->currentText());
    emit modelSelected(path);
}

bool InferencePanel::isBatchMode() const {
    return m_batchCheckBox->isChecked();
}

void InferencePanel::setStatus(const QString& text, bool isError) {
    m_statusLabel->setText(text);
    if (isError) {
        m_statusLabel->setStyleSheet("color: #F44336; font-size: 12px; padding: 4px;");
    } else {
        m_statusLabel->setStyleSheet("color: #aaa; font-size: 12px; padding: 4px;");
    }
}

void InferencePanel::setProgress(int value, int total, const QString& status) {
    // 确保进度条始终可见（忙碌或百分比模式均需显示）
    m_progressBar->setVisible(true);

    if (total <= 0) {
        // 总数量未知时显示忙碌动画，保留状态文本
        m_progressBar->setRange(0, 0);
        if (!status.isEmpty()) {
            m_statusLabel->setText(status);
        }
        return;
    }

    m_progressBar->setRange(0, total);
    m_progressBar->setValue(qBound(0, value, total));

    // 在状态标签中显示百分比与状态文本
    int percent = total > 0 ? static_cast<int>(value * 100.0 / total) : 0;
    QString text = QString("已完成 %1% (%2/%3)").arg(percent).arg(value).arg(total);
    if (!status.isEmpty()) {
        text = status + " " + text;
    }
    m_statusLabel->setText(text);
}

void InferencePanel::onRunInference() {
    QString modelName = m_modelCombo->currentText();
    if (modelName.isEmpty() || modelName == "未找到模型") {
        setStatus("请先选择模型", true);
        return;
    }
    
    QString modelPath = currentModelPath();
    if (modelPath.isEmpty()) {
        setStatus("无法获取模型路径", true);
        return;
    }

    setStatus("推理中...");
    m_runBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0);

    emit inferenceRequested(modelPath, QStringList());
}

void InferencePanel::onModelChanged(int index) {
    Q_UNUSED(index);
    m_modelInfoList->clear();

    QString modelName = m_modelCombo->currentText();

    // 用户手动切换模型时，若与显式路径不一致则清除显式路径
    if (!m_explicitModelPath.isEmpty()) {
        QString selectedPath = ModelManager::instance()->getModelPathByName(modelName);
        if (selectedPath != m_explicitModelPath) {
            m_explicitModelPath.clear();
        }
    }

    if (modelName.isEmpty() || modelName == "未找到模型") {
        m_modelPathLabel->setText("路径: -");
        return;
    }

    updateModelInfo(modelName);
    emit modelSelected(currentModelPath());
}

void InferencePanel::updateModelInfo(const QString& modelName) {
    m_modelInfoList->clear();

    // 优先使用模型库显式选中的路径，否则通过名称反查
    QString modelPath = currentModelPath();
    if (modelPath.isEmpty()) {
        modelPath = ModelManager::instance()->getModelPathByName(modelName);
    }

    // 更新路径显示
    if (!modelPath.isEmpty()) {
        m_modelPathLabel->setText(QString("路径: %1").arg(modelPath));
    } else {
        m_modelPathLabel->setText("路径: -");
    }

    m_modelInfoList->addItem(QString("模型: %1").arg(modelName));
    
    if (!modelPath.isEmpty()) {
        QFileInfo fi(modelPath);
        m_modelInfoList->addItem(QString("文件: %1").arg(fi.fileName()));
        m_modelInfoList->addItem(QString("大小: %1 KB").arg(fi.size() / 1024));
    }
    
    // 根据模型路径自动识别模型类型与输入尺寸
    QString modelType = ModelManager::instance()->autoDetectModelType(modelPath);
    QSize inputSize = ModelManager::instance()->autoDetectInputSize(modelPath);
    if (modelType == "yolo" || inputSize == QSize(640, 640)) {
        m_modelInfoList->addItem("类型: 目标检测");
        m_modelInfoList->addItem("输入: 640x640 RGB");
    } else {
        m_modelInfoList->addItem("类型: 图像分类");
        m_modelInfoList->addItem(QString("输入: %1x%2 RGB").arg(inputSize.width()).arg(inputSize.height()));
    }
}

void InferencePanel::onBatchToggled(bool checked) {
    Q_UNUSED(checked);
}

void InferencePanel::onStopInference() {
    setStatus("正在停止...");
    emit stopInferenceRequested();
    resetButtons();
}

void InferencePanel::resetButtons() {
    m_runBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    m_progressBar->setVisible(false);
    setStatus("就绪");
}

void InferencePanel::onDefaultModelLoadFailed(const QString& error) {
    setStatus(QString("错误: %1").arg(error), true);
    QDV::Logger::error(QString("InferencePanel: %1").arg(error));
}

