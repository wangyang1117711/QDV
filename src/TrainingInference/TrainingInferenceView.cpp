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
#include "AI/InferenceEngine.h"
#include "AI/ModelManager.h"
#include "AI/TrainingBridge.h"
#include "Core/Logger.h"
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
#include <QStackedWidget>
#include <QGroupBox>
#include <QLineEdit>
#include <QRandomGenerator>
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
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>

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
}

void TrainingInferenceView::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setupToolbar(mainLayout);

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

    mainLayout->addWidget(m_mainSplitter, 1);
}

void TrainingInferenceView::setupToolbar(QVBoxLayout* mainLayout)
{
    m_toolbar = new QToolBar();
    m_toolbar->setStyleSheet(
        "QToolBar { background-color: #2d2d2d; border-bottom: 1px solid #444; "
        "padding: 4px 8px; spacing: 6px; }");

    QAction* importAction = m_toolbar->addAction("导入图像");
    connect(importAction, &QAction::triggered, this, &TrainingInferenceView::onImportImages);

    QAction* importFolderAction = m_toolbar->addAction("导入文件夹");
    connect(importFolderAction, &QAction::triggered, this, &TrainingInferenceView::onImportFolder);

    m_toolbar->addSeparator();

    QAction* trainAction = m_toolbar->addAction("启动训练");
    connect(trainAction, &QAction::triggered, this, &TrainingInferenceView::onRunTrainingPipeline);

    QAction* inferAction = m_toolbar->addAction("快速推理");
    connect(inferAction, &QAction::triggered, [this]() {
        QString modelPath = m_inferencePanel->currentModelPath();
        if (modelPath.isEmpty()) {
            modelPath = ModelManager::instance()->getDefaultModelPath();
        }
        
        if (modelPath.isEmpty()) {
            QMessageBox::warning(this, "提示", "未找到可用的模型");
            return;
        }
        
        QStringList paths = ImageManager::instance()->selectedPaths();
        if (paths.isEmpty()) {
            paths = ImageManager::instance()->allPaths();
        }
        emit m_inferencePanel->inferenceRequested(modelPath, paths);
    });

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
            background-color: #660874;
        }
        QListWidget::item:hover {
            background-color: #333;
        }
    )");
    connect(m_imageList, &QListWidget::currentRowChanged,
            this, &TrainingInferenceView::onImageSelected);
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
    m_categoryPanel->setFixedWidth(240);
    topLayout->addWidget(m_categoryPanel);

    m_inferencePanel = new InferencePanel();
    m_inferencePanel->setFixedWidth(240);
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
    
    QStringList allLabels = uniqueLabels.values();
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

    // --- 显示训练进度对话框 ---
    // 先关闭旧对话框（如果存在）
    if (m_progressDialog) {
        m_progressDialog->close();
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }

    m_progressDialog = new TrainingProgressDialog(epochSpin->value(), this);

    // 连接训练进度信号 -> 对话框更新
    connect(m_trainingBridge, &QDV::TrainingBridge::trainingProgress,
            m_progressDialog, &TrainingProgressDialog::updateProgress);

    // 连接取消按钮 -> 取消训练
    connect(m_progressDialog, &TrainingProgressDialog::cancelled,
            this, [this]() {
        m_trainingBridge->cancelTraining();
    });

    // 训练完成时关闭对话框（由 onTrainingCompleted 处理）
    // 训练失败时也关闭对话框
    connect(m_trainingBridge, &QDV::TrainingBridge::trainingError,
            m_progressDialog, [this](const QString& phase, const QString& message) {
        Q_UNUSED(phase);
        Q_UNUSED(message);
        if (m_progressDialog) {
            m_progressDialog->close();
            m_progressDialog->deleteLater();
            m_progressDialog = nullptr;
        }
    });

    m_progressDialog->show();
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
    if (m_progressDialog) {
        m_progressDialog->close();
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }

    bool success = map["success"].toBool();
    if (success) {
        QString onnxPath = map["onnxPath"].toString();
        QString labelsPath = map["labelsPath"].toString();
        double totalTime = map["totalTimeSeconds"].toDouble();

        m_consoleLog->append(QString("[%1] 训练完成！").arg(QTime::currentTime().toString("hh:mm:ss")));
        m_consoleLog->append(QString("总耗时: %1 秒").arg(QString::number(totalTime, 'f', 1)));
        m_consoleLog->append("模型保存: " + onnxPath);

        // --- 注册训练得到的模型到模型库 ---
        QString modelName = map.value("modelName", "trained_model").toString();
        bool registered = ModelManager::instance()->registerTrainedModel(
            onnxPath, labelsPath, modelName);

        if (registered) {
            m_consoleLog->append(QString("模型已注册到模型库: %1").arg(modelName));
            QMessageBox::information(this, "训练完成",
                QString("模型训练成功！\n\n"
                        "模型已保存并注册到模型库。\n\n"
                        "模型名称: %1\n"
                        "ONNX路径: %2").arg(modelName, onnxPath));
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
        QString error = map["errorMessage"].toString();
        m_consoleLog->append(QString("[错误] 训练失败: %1").arg(error));
        QMessageBox::critical(this, "训练失败", error);
    }
}

void TrainingInferenceView::onTrainingError(const QString& phase, const QString& message)
{
    m_consoleLog->append(QString("[错误] %1: %2").arg(phase, message));
}

void TrainingInferenceView::onTrainingLogOutput(const QString& message)
{
    m_consoleLog->append(message);
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

    QDV::Logger::debug("[DBG-R2] rebuildImageList: disconnect done, calling clear()");

    m_imageList->clear();

    QDV::Logger::debug(QString("[DBG-R3] rebuildImageList after clear: listCount=%1")
              .arg(m_imageList->count()));

    auto images = ImageManager::instance()->images();
    int restoreRow = -1;

    for (int i = 0; i < images.size(); ++i)
    {
        const auto& entry = images[i];
        
        // 创建自定义widget项
        QWidget* itemWidget = new QWidget();
        QHBoxLayout* itemLayout = new QHBoxLayout(itemWidget);
        itemLayout->setContentsMargins(8, 4, 8, 4);
        itemLayout->setSpacing(12);
        
        // 复选框
        QCheckBox* checkBox = new QCheckBox();
        checkBox->setChecked(entry.isSelected);
        checkBox->setProperty("filePath", entry.filePath);
        checkBox->setStyleSheet(R"(
            QCheckBox {
                spacing: 4px;
            }
            QCheckBox::indicator {
                width: 18px;
                height: 18px;
                border: 2px solid #555;
                border-radius: 3px;
                background-color: #2d2d2d;
            }
            QCheckBox::indicator:checked {
                background-color: #660874;
                border-color: #660874;
            }
            QCheckBox::indicator:hover {
                border-color: #7d1a8f;
            }
        )");
        connect(checkBox, &QCheckBox::toggled, [this, path = entry.filePath](bool checked) {
            ImageManager::instance()->setSelected(path, checked);
        });
        
        // 图像缩略图标签容器 - 用于显示带边框的缩略图
        QWidget* iconContainer = new QWidget();
        iconContainer->setFixedSize(68, 68);
        QVBoxLayout* iconLayout = new QVBoxLayout(iconContainer);
        iconLayout->setContentsMargins(0, 0, 0, 0);
        
        QLabel* iconLabel = new QLabel();
        iconLabel->setPixmap(entry.icon.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        iconLabel->setAlignment(Qt::AlignCenter);
        
        // 根据是否有标签设置不同的边框样式
        if (entry.isAnnotated && !entry.label.isEmpty()) {
            iconContainer->setStyleSheet(R"(
                QWidget {
                    border: 2px solid #660874;
                    border-radius: 4px;
                    background-color: #1a1a1a;
                }
            )");
        } else {
            iconContainer->setStyleSheet(R"(
                QWidget {
                    border: 2px solid #3a3a3a;
                    border-radius: 4px;
                    background-color: #1a1a1a;
                }
            )");
        }
        
        iconLayout->addWidget(iconLabel);
        
        // 文件名和标签区域
        QWidget* textContainer = new QWidget();
        QVBoxLayout* textLayout = new QVBoxLayout(textContainer);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(4);
        
        // 文件名标签
        QLabel* nameLabel = new QLabel(entry.fileName);
        nameLabel->setStyleSheet("color: #e0e0e0; font-size: 13px;");
        textLayout->addWidget(nameLabel);
        
        // 类别标签（如果有）
        if (entry.isAnnotated && !entry.label.isEmpty()) {
            QLabel* tagLabel = new QLabel(QString("类别: %1").arg(entry.label));
            tagLabel->setStyleSheet(R"(
                QLabel {
                    color: #660874;
                    font-size: 11px;
                    font-weight: bold;
                    background-color: rgba(102, 8, 116, 0.15);
                    padding: 2px 8px;
                    border-radius: 3px;
                }
            )");
            tagLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            textLayout->addWidget(tagLabel);
        }
        
        textLayout->addStretch();
        
        itemLayout->addWidget(checkBox);
        itemLayout->addWidget(iconContainer);
        itemLayout->addWidget(textContainer, 1);
        
        // 创建列表项
        QListWidgetItem* item = new QListWidgetItem();
        item->setToolTip(entry.filePath);
        item->setSizeHint(QSize(0, 80));
        m_imageList->addItem(item);
        m_imageList->setItemWidget(item, itemWidget);

        if (entry.filePath == previousPath) restoreRow = i;
    }

    QDV::Logger::debug(QString("[DBG-R4] rebuildImageList after rebuild: listCount=%1 restoreRow=%2")
              .arg(m_imageList->count()).arg(restoreRow));

    connect(m_imageList, &QListWidget::currentRowChanged,
            this, &TrainingInferenceView::onImageSelected);

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
    QList<InferenceResult> results;

    QStringList paths = imagePaths.isEmpty()
        ? ImageManager::instance()->allPaths() : imagePaths;

    for (int i = 0; i < paths.size(); ++i)
    {
        InferenceResult result;
        result.imagePath = paths[i];
        result.confidence = 0.85 + QRandomGenerator::global()->bounded(15) / 100.0;

        QList<CategoryNode> categories = CategoryManager::instance()->allCategories();
        if (!categories.isEmpty())
        {
            int idx = QRandomGenerator::global()->bounded(categories.size());
            result.category = categories[idx].name;
        }
        else
        {
            result.category = QString("Class_%1").arg(QRandomGenerator::global()->bounded(5));
        }

        QJsonObject raw;
        raw["model"] = modelPath;
        raw["latency_ms"] = 12.5;
        raw["input_size"] = "224x224";
        result.rawOutput = raw;

        results.append(result);
    }

    onInferenceCompleted(results);
}

void TrainingInferenceView::onInferenceCompleted(const QList<InferenceResult>& results)
{
    m_resultPanel->setResults(results);
    m_centerStack->setCurrentIndex(1);
    // 恢复推理按钮状态
    m_inferencePanel->resetButtons();
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
    
    // 更新现有复选框状态而不是重建整个列表
    for (int i = 0; i < m_imageList->count(); ++i) {
        QListWidgetItem* item = m_imageList->item(i);
        QString filePath = item->toolTip();
        
        // 找到对应的widget和checkbox
        QWidget* widget = m_imageList->itemWidget(item);
        if (!widget) continue;
        
        // 查找widget中的checkbox并更新状态
        QCheckBox* checkBox = widget->findChild<QCheckBox*>();
        if (checkBox) {
            bool shouldBeChecked = ImageManager::instance()->isSelected(filePath);
            if (checkBox->isChecked() != shouldBeChecked) {
                // 临时断开信号，避免循环触发
                QSignalBlocker blocker(checkBox);
                checkBox->setChecked(shouldBeChecked);
            }
        }
    }
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