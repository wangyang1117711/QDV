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
        QComboBox:hover { border-color: #660874; }
        QComboBox QAbstractItemView {
            background-color: #2d2d2d;
            color: #e0e0e0;
            selection-background-color: #660874;
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
    m_runBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #660874;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 10px 0;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover { background-color: #7d1a8f; }
        QPushButton:disabled { background-color: #555; }
    )");
    connect(m_runBtn, &QPushButton::clicked, this, &InferencePanel::onRunInference);
    layout->addWidget(m_runBtn);

    m_stopBtn = new QPushButton("停止推理");
    m_stopBtn->setEnabled(false);
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
        QProgressBar::chunk { background-color: #660874; border-radius: 3px; }
    )");
    layout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("就绪");
    m_statusLabel->setStyleSheet("color: #aaa; font-size: 12px; padding: 4px;");
    layout->addWidget(m_statusLabel);

    layout->addStretch();
}

void InferencePanel::refreshModelList() {
    QStringList models = ModelManager::instance()->getAvailableModelNames();
    
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
    return ModelManager::instance()->getModelPathByName(currentModel());
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
    if (modelName.isEmpty() || modelName == "未找到模型") {
        m_modelPathLabel->setText("路径: -");
        return;
    }
    
    updateModelInfo(modelName);
    emit modelSelected(currentModelPath());
}

void InferencePanel::updateModelInfo(const QString& modelName) {
    QString modelPath = ModelManager::instance()->getModelPathByName(modelName);
    
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
    
    m_modelInfoList->addItem("类型: 图像分类");
    m_modelInfoList->addItem("输入: 224x224 RGB");
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

