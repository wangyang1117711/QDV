#include "TrainingInference/InferencePanel.h"
#include "AI/InferenceEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidgetItem>



InferencePanel::InferencePanel(QWidget* parent) : QWidget(parent) {
    setupUI();
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

void InferencePanel::setModelList(const QStringList& models) {
    m_modelCombo->clear();
    m_modelCombo->addItems(models);
}

QString InferencePanel::currentModel() const {
    return m_modelCombo->currentText();
}

bool InferencePanel::isBatchMode() const {
    return m_batchCheckBox->isChecked();
}

void InferencePanel::onRunInference() {
    QString model = m_modelCombo->currentText();
    if (model.isEmpty()) {
        m_statusLabel->setText("请先选择模型");
        m_statusLabel->setStyleSheet("color: #F44336; font-size: 12px; padding: 4px;");
        return;
    }

    m_statusLabel->setText("推理中...");
    m_statusLabel->setStyleSheet("color: #42A5F5; font-size: 12px; padding: 4px;");
    m_runBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0);

    emit inferenceRequested(model, QStringList());
}

void InferencePanel::onModelChanged(int index) {
    Q_UNUSED(index);
    m_modelInfoList->clear();

    QString model = m_modelCombo->currentText();
    if (model.isEmpty()) return;

    emit modelSelected(model);

    m_modelInfoList->addItem(QString("模型: %1").arg(model));
    m_modelInfoList->addItem("类型: 图像分类");
    m_modelInfoList->addItem("输入: 224x224 RGB");
    m_modelInfoList->addItem("参数量: ~3.5M");
}

void InferencePanel::onBatchToggled(bool checked) {
    Q_UNUSED(checked);
}

