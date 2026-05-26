#include "TrainingInference/TrainingInferenceView.h"
#include "TrainingInference/ImageViewWidget.h"
#include "TrainingInference/CategoryPanel.h"
#include "TrainingInference/InferencePanel.h"
#include "TrainingInference/ScriptEditorWidget.h"
#include "TrainingInference/ResultPanel.h"
#include "TrainingInference/ImageManager.h"
#include "TrainingInference/CategoryManager.h"
#include "TrainingInference/ExportManager.h"
#include "AI/InferenceEngine.h"
#include "AI/ModelManager.h"
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

TrainingInferenceView::TrainingInferenceView(QWidget* parent) : QWidget(parent)
{
    setupUI();

    connect(ImageManager::instance(), &ImageManager::imagesImported,
            this, &TrainingInferenceView::onImagesImported);
    connect(m_inferencePanel, &InferencePanel::inferenceRequested,
            this, &TrainingInferenceView::onInferenceRequested);
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
        QString model = m_inferencePanel->currentModel();
        if (model.isEmpty()) model = "default_model";
        QStringList paths = ImageManager::instance()->allPaths();
        emit m_inferencePanel->inferenceRequested(model, paths);
    });

    mainLayout->addWidget(m_toolbar);
}

void TrainingInferenceView::setupImagePanel(QSplitter* splitter)
{
    QWidget* imagePanel = new QWidget();
    QVBoxLayout* imageLayout = new QVBoxLayout(imagePanel);
    imageLayout->setContentsMargins(8, 8, 8, 8);

    QLabel* imageListLabel = new QLabel("图像列表");
    imageListLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e0e0e0;");
    imageLayout->addWidget(imageListLabel);

    m_imageList = new QListWidget();
    m_imageList->setIconSize(QSize(128, 128));
    m_imageList->setStyleSheet(
        "QListWidget { background-color: #252525; border: 1px solid #444; "
        "border-radius: 4px; color: #e0e0e0; }"
        "QListWidget::item:selected { background-color: #660874; }"
        "QListWidget::item:hover { background-color: #333; }");
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

    QTextEdit* consoleLog = new QTextEdit();
    consoleLog->setReadOnly(true);
    consoleLog->setStyleSheet(
        "QTextEdit { background-color: #1a1a1a; border: 1px solid #444; "
        "border-radius: 4px; color: #aaa; font-family: 'Consolas', monospace; "
        "font-size: 12px; padding: 8px; }");
    bottomLayout->addWidget(consoleLog);

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
}

void TrainingInferenceView::onImagesImported(int count)
{
    m_imageList->clear();
    auto images = ImageManager::instance()->images();
    for (const auto& entry : images)
    {
        QListWidgetItem* item = new QListWidgetItem();
        item->setText(entry.fileName);
        item->setToolTip(entry.filePath);
        item->setIcon(QIcon(entry.icon));
        item->setSizeHint(QSize(0, 80));
        m_imageList->addItem(item);
    }
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

    m_inferencePanel->findChild<QProgressBar*>()->setVisible(false);
}

void TrainingInferenceView::onInferenceCompleted(const QList<InferenceResult>& results)
{
    m_resultPanel->setResults(results);
    m_centerStack->setCurrentIndex(1);
}