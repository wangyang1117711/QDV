#include "TrainingInference/ModelLibraryDialog.h"
#include "AI/ModelManager.h"
#include "Core/Logger.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QHeaderView>
#include <QTreeWidget>
#include <QSettings>

using namespace QDV;

QList<BuiltinModelEntry> ModelLibraryDialog::builtinModels() {
    return {
        {"resnet18", "ResNet-18", "models/resnet18.onnx", "经典图像分类模型，18层残差网络", "分类", 224, 224},
        {"mobilenet_v2", "MobileNetV2", "models/mobilenet_v2.onnx", "轻量级移动端分类模型，适合实时检测", "分类", 224, 224},
        {"efficientnet_b0", "EfficientNet-B0", "models/efficientnet_b0.onnx", "高效分类模型，平衡精度与速度", "分类", 224, 224},
        {"yolov5s", "YOLOv5-Small", "models/yolov5s.onnx", "目标检测模型，适用于多目标检测场景", "检测", 640, 640},
        {"defect_resnet", "缺陷分类-ResNet", "models/defect_resnet.onnx", "工业缺陷分类专用模型", "分类", 224, 224},
    };
}

ModelLibraryDialog::ModelLibraryDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("模型库管理");
    setMinimumSize(700, 520);
    setStyleSheet(R"(
        QDialog {
            background-color: #252526;
            color: #e0e0e0;
        }
        QTabWidget::pane {
            border: 1px solid #444;
            background-color: #1e1e1e;
        }
        QTabBar::tab {
            background-color: #2d2d2d;
            color: #ccc;
            padding: 8px 16px;
            border: 1px solid #444;
            border-bottom: none;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }
        QTabBar::tab:selected {
            background-color: #1e1e1e;
            color: #fff;
            border-bottom: 2px solid #0d7377;
        }
        QListWidget {
            background-color: #1e1e1e;
            border: 1px solid #444;
            border-radius: 4px;
            color: #e0e0e0;
            font-size: 13px;
        }
        QListWidget::item {
            padding: 8px 12px;
            border-bottom: 1px solid #333;
        }
        QListWidget::item:selected {
            background-color: #0d7377;
            color: white;
        }
        QListWidget::item:hover {
            background-color: #333;
        }
        QLabel {
            color: #e0e0e0;
        }
        QPushButton {
            border: none;
            border-radius: 4px;
            padding: 8px 16px;
            font-size: 13px;
        }
    )");

    setupUI();
    populateBuiltinModels();
    populateUserModels();
}

void ModelLibraryDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    QHBoxLayout* topLayout = new QHBoxLayout();
    QLabel* titleLabel = new QLabel("模型库管理");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #fff;");
    topLayout->addWidget(titleLabel);
    topLayout->addStretch();

    m_scanBtn = new QPushButton("扫描模型目录");
    m_scanBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #0d7377;
            color: white;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #14919b;
        }
    )");
    topLayout->addWidget(m_scanBtn);
    mainLayout->addLayout(topLayout);

    m_tabWidget = new QTabWidget();
    m_tabWidget->setStyleSheet("QTabWidget::pane { border: 1px solid #444; background-color: #1e1e1e; }");

    QWidget* builtinTab = new QWidget();
    QVBoxLayout* builtinLayout = new QVBoxLayout(builtinTab);
    m_builtinList = new QListWidget();
    m_builtinList->setAlternatingRowColors(true);
    builtinLayout->addWidget(m_builtinList);
    m_tabWidget->addTab(builtinTab, "内置模型");

    QWidget* userTab = new QWidget();
    QVBoxLayout* userLayout = new QVBoxLayout(userTab);
    m_userList = new QListWidget();
    m_userList->setAlternatingRowColors(true);
    userLayout->addWidget(m_userList);
    m_tabWidget->addTab(userTab, "已加载模型");

    QWidget* scannedTab = new QWidget();
    QVBoxLayout* scannedLayout = new QVBoxLayout(scannedTab);
    m_scannedList = new QListWidget();
    m_scannedList->setAlternatingRowColors(true);
    scannedLayout->addWidget(m_scannedList);
    m_tabWidget->addTab(scannedTab, "扫描结果");

    mainLayout->addWidget(m_tabWidget, 1);

    QGroupBox* detailGroup = new QGroupBox("模型详情");
    detailGroup->setStyleSheet(R"(
        QGroupBox {
            color: #e0e0e0;
            font-weight: bold;
            border: 1px solid #444;
            border-radius: 4px;
            margin-top: 8px;
            padding-top: 16px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 4px;
        }
    )");
    QVBoxLayout* detailLayout = new QVBoxLayout(detailGroup);
    m_detailLabel = new QLabel("请从列表中选择一个模型以查看详情");
    m_detailLabel->setStyleSheet("color: #aaa; font-size: 12px; padding: 8px;");
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setMinimumHeight(60);
    detailLayout->addWidget(m_detailLabel);
    mainLayout->addWidget(detailGroup);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_cancelBtn = new QPushButton("取消");
    m_cancelBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #555;
            color: #e0e0e0;
        }
        QPushButton:hover {
            background-color: #666;
        }
    )");
    btnLayout->addWidget(m_cancelBtn);

    m_loadBtn = new QPushButton("加载选中模型");
    m_loadBtn->setEnabled(false);
    m_loadBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #0d7377;
            color: white;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #14919b;
        }
        QPushButton:disabled {
            background-color: #444;
            color: #888;
        }
    )");
    btnLayout->addWidget(m_loadBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_scanBtn, &QPushButton::clicked, this, &ModelLibraryDialog::onScanDirectory);
    connect(m_loadBtn, &QPushButton::clicked, this, &ModelLibraryDialog::onLoadSelected);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_builtinList, &QListWidget::itemSelectionChanged, this, &ModelLibraryDialog::onModelSelectionChanged);
    connect(m_userList, &QListWidget::itemSelectionChanged, this, &ModelLibraryDialog::onModelSelectionChanged);
    connect(m_scannedList, &QListWidget::itemSelectionChanged, this, &ModelLibraryDialog::onModelSelectionChanged);
    connect(m_builtinList, &QListWidget::itemDoubleClicked, this, &ModelLibraryDialog::onModelDoubleClicked);
    connect(m_userList, &QListWidget::itemDoubleClicked, this, &ModelLibraryDialog::onModelDoubleClicked);
    connect(m_scannedList, &QListWidget::itemDoubleClicked, this, &ModelLibraryDialog::onModelDoubleClicked);
}

void ModelLibraryDialog::populateBuiltinModels() {
    m_builtinList->clear();
    m_builtinRegistry.clear();

    for (const BuiltinModelEntry& entry : builtinModels()) {
        m_builtinRegistry[entry.id] = entry;

        QListWidgetItem* item = new QListWidgetItem();
        item->setText(QString("%1  [%2]")
            .arg(entry.name)
            .arg(entry.category));
        item->setToolTip(entry.description);
        item->setData(Qt::UserRole, entry.id);
        item->setData(Qt::UserRole + 1, entry.path);
        item->setData(Qt::UserRole + 2, entry.name);
        m_builtinList->addItem(item);
    }
}

void ModelLibraryDialog::populateUserModels() {
    m_userList->clear();

    QList<ModelInfo> loadedModels = ModelManager::instance()->loadedModelInfo();
    for (const ModelInfo& info : loadedModels) {
        QListWidgetItem* item = new QListWidgetItem();
        QString status = info.avgInferenceMs > 0
            ? QString(" [预热完成] %.1fms").arg(info.avgInferenceMs)
            : " [未预热]";
        item->setText(QString("%1%2").arg(QFileInfo(info.path).fileName()).arg(status));
        item->setData(Qt::UserRole, info.id);
        item->setData(Qt::UserRole + 1, info.path);
        item->setData(Qt::UserRole + 2, QFileInfo(info.path).fileName());
        item->setToolTip(QString("路径: %1\n输入尺寸: %2x%3\n访问次数: %4")
            .arg(info.path)
            .arg(info.inputSize.width())
            .arg(info.inputSize.height())
            .arg(info.accessCount));
        m_userList->addItem(item);
    }
}

void ModelLibraryDialog::onScanDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this,
        "选择模型目录",
        m_lastScanDir.isEmpty() ? QDir::currentPath() : m_lastScanDir);

    if (dir.isEmpty()) return;

    m_lastScanDir = dir;
    scanDirectory(dir);
}

void ModelLibraryDialog::scanDirectory(const QString& dir) {
    m_scannedList->clear();
    QStringList models = ModelManager::instance()->listModels(dir);

    if (models.isEmpty()) {
        QListWidgetItem* item = new QListWidgetItem();
        item->setText("未找到模型文件 (.onnx/.pth/.pt/.bin)");
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setForeground(QColor("#888"));
        m_scannedList->addItem(item);
        m_tabWidget->setCurrentIndex(2);
        return;
    }

    for (const QString& path : models) {
        QFileInfo fi(path);
        QListWidgetItem* item = new QListWidgetItem();
        item->setText(fi.fileName());
        item->setData(Qt::UserRole, fi.baseName());
        item->setData(Qt::UserRole + 1, path);
        item->setData(Qt::UserRole + 2, fi.fileName());
        item->setToolTip(QString("路径: %1\n大小: %2 KB").arg(path).arg(fi.size() / 1024));
        m_scannedList->addItem(item);
    }

    m_tabWidget->setCurrentIndex(2);
    Logger::info(QString("ModelLibrary: scanned %1 models in %2").arg(models.size()).arg(dir));
}

void ModelLibraryDialog::setCurrentModelPath(const QString& path) {
    for (int i = 0; i < m_builtinList->count(); ++i) {
        if (m_builtinList->item(i)->data(Qt::UserRole + 1).toString() == path) {
            m_builtinList->setCurrentRow(i);
            m_tabWidget->setCurrentIndex(0);
            return;
        }
    }
    for (int i = 0; i < m_userList->count(); ++i) {
        if (m_userList->item(i)->data(Qt::UserRole + 1).toString() == path) {
            m_userList->setCurrentRow(i);
            m_tabWidget->setCurrentIndex(1);
            return;
        }
    }
}

void ModelLibraryDialog::onModelSelectionChanged() {
    QListWidget* sender = qobject_cast<QListWidget*>(QObject::sender());
    if (!sender || !sender->currentItem()) {
        m_loadBtn->setEnabled(false);
        m_detailLabel->setText("请从列表中选择一个模型以查看详情");
        return;
    }

    QListWidgetItem* item = sender->currentItem();
    QString modelPath = item->data(Qt::UserRole + 1).toString();
    QString modelName = item->data(Qt::UserRole + 2).toString();

    showModelDetails(modelPath, modelName);
    m_loadBtn->setEnabled(true);
    m_selectedPath = modelPath;
    m_selectedName = modelName;
}

void ModelLibraryDialog::showModelDetails(const QString& modelPath, const QString& modelName) {
    QFileInfo fi(modelPath);
    QString details;

    details += QString("名称: %1\n").arg(modelName);
    details += QString("路径: %1\n").arg(modelPath);

    if (fi.exists()) {
        details += QString("大小: %1 KB\n").arg(fi.size() / 1024);
        details += QString("修改时间: %1\n").arg(fi.lastModified().toString("yyyy-MM-dd hh:mm:ss"));
    } else {
        details += "状态: 文件不存在（需下载或配置）\n";
    }

    QString id = m_builtinList->currentItem()
        ? m_builtinList->currentItem()->data(Qt::UserRole).toString()
        : QString();

    if (m_builtinRegistry.contains(id)) {
        const BuiltinModelEntry& entry = m_builtinRegistry[id];
        details += QString("输入尺寸: %1x%2\n").arg(entry.width).arg(entry.height);
        details += QString("分类: %1\n").arg(entry.category);
        details += QString("描述: %1\n").arg(entry.description);
    }

    QList<ModelInfo> loaded = ModelManager::instance()->loadedModelInfo();
    for (const ModelInfo& info : loaded) {
        if (info.path == modelPath) {
            details += QString("缓存状态: 已加载\n");
            details += QString("平均推理: %.1fms\n").arg(info.avgInferenceMs);
            details += QString("访问次数: %1\n").arg(info.accessCount);
            break;
        }
    }

    m_detailLabel->setText(details.trimmed());
}

void ModelLibraryDialog::onModelDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    m_selectedPath = item->data(Qt::UserRole + 1).toString();
    m_selectedName = item->data(Qt::UserRole + 2).toString();
    accept();
}

void ModelLibraryDialog::onLoadSelected() {
    if (m_selectedPath.isEmpty()) return;
    accept();
}

QString ModelLibraryDialog::selectedModelPath() const {
    return m_selectedPath;
}

QString ModelLibraryDialog::selectedModelName() const {
    return m_selectedName;
}