#include "TrainingInference/ModelLibraryDialog.h"
#include "AI/ModelManager.h"
#include "Core/Logger.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QTabWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QInputDialog>
#include <QSet>
#include <QHeaderView>
#include <QTreeWidget>
#include <QSettings>
#include <QFrame>
#include <QCoreApplication>

using namespace QDV;

// =============================================================================
// 通用暗色主题样式（v2.7.2 统一）
// =============================================================================
static const char* kDarkDialogStyle = R"(
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
    QLineEdit {
        background-color: #2d2d2d;
        border: 1px solid #555;
        border-radius: 4px;
        padding: 6px 10px;
        color: #e0e0e0;
        font-size: 13px;
    }
    QLineEdit:focus {
        border: 1px solid #0d7377;
    }
    QCheckBox {
        color: #e0e0e0;
        font-size: 12px;
    }
    QCheckBox::indicator {
        width: 14px;
        height: 14px;
    }
    QPushButton {
        border: none;
        border-radius: 4px;
        padding: 8px 16px;
        font-size: 13px;
    }
    QPushButton:disabled {
        background-color: #444;
        color: #888;
    }
)";

// 按钮色板
static const char* kBtnPrimary = "background-color: #0d7377; color: white; font-weight: bold;";
static const char* kBtnPrimaryHover = "QPushButton { background-color: #0d7377; color: white; font-weight: bold; }"
                                       "QPushButton:hover { background-color: #14919b; }"
                                       "QPushButton:disabled { background-color: #444; color: #888; }";
static const char* kBtnSecondary = "QPushButton { background-color: #555; color: #e0e0e0; }"
                                   "QPushButton:hover { background-color: #666; }"
                                   "QPushButton:disabled { background-color: #444; color: #888; }";
static const char* kBtnDanger = "QPushButton { background-color: #6b2c2c; color: #ffd0d0; }"
                                "QPushButton:hover { background-color: #8a3838; }"
                                "QPushButton:disabled { background-color: #444; color: #888; }";
static const char* kBtnVerify = "QPushButton { background-color: #1e3a5f; color: #b0d4ff; }"
                                "QPushButton:hover { background-color: #2a4f7f; }"
                                "QPushButton:disabled { background-color: #444; color: #888; }";

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
    setMinimumSize(960, 680);
    setStyleSheet(kDarkDialogStyle);

    setupUI();
    refreshAllLists();
}

// =============================================================================
// UI 构建
// =============================================================================
void ModelLibraryDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(10);

    // --- 顶部工具栏：标题 + 批量操作 + 导入/验证/扫描按钮 ---
    QHBoxLayout* topLayout = new QHBoxLayout();
    QLabel* titleLabel = new QLabel("模型库管理");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #fff;");
    topLayout->addWidget(titleLabel);
    topLayout->addStretch();

    // v2.7.2 批量操作按钮
    m_batchRegisterBtn = new QPushButton("批量注册");
    m_batchRegisterBtn->setToolTip("将扫描结果中所有选中的模型批量注册到模型库");
    m_batchRegisterBtn->setStyleSheet(kBtnPrimaryHover);
    m_batchRegisterBtn->setEnabled(false);
    topLayout->addWidget(m_batchRegisterBtn);

    m_batchDeleteBtn = new QPushButton("批量删除");
    m_batchDeleteBtn->setToolTip("批量删除选中的模型（注册模型→.trash；扫描外部文件→直接删除）");
    m_batchDeleteBtn->setStyleSheet(kBtnDanger);
    m_batchDeleteBtn->setEnabled(false);
    topLayout->addWidget(m_batchDeleteBtn);

    m_batchVerifyBtn = new QPushButton("批量验证");
    m_batchVerifyBtn->setToolTip("批量验证选中模型的 SHA256 完整性");
    m_batchVerifyBtn->setStyleSheet(kBtnVerify);
    m_batchVerifyBtn->setEnabled(false);
    topLayout->addWidget(m_batchVerifyBtn);

    topLayout->addSpacing(12);

    m_importBtn = new QPushButton("+ 导入模型");
    m_importBtn->setStyleSheet(kBtnPrimaryHover);
    topLayout->addWidget(m_importBtn);

    m_verifyAllBtn = new QPushButton("验证完整性");
    m_verifyAllBtn->setStyleSheet(kBtnVerify);
    topLayout->addWidget(m_verifyAllBtn);

    m_scanBtn = new QPushButton("扫描目录");
    m_scanBtn->setStyleSheet(kBtnSecondary);
    topLayout->addWidget(m_scanBtn);
    mainLayout->addLayout(topLayout);

    // --- 搜索栏 ---
    QHBoxLayout* searchLayout = new QHBoxLayout();
    QLabel* searchIcon = new QLabel("🔍");
    searchIcon->setStyleSheet("color: #888; font-size: 14px;");
    searchLayout->addWidget(searchIcon);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("搜索模型名称、文件名或描述（实时过滤当前标签页）...");
    searchLayout->addWidget(m_searchEdit, 1);
    mainLayout->addLayout(searchLayout);

    // --- 标签页（4 个） ---
    m_tabWidget = new QTabWidget();
    m_tabWidget->setStyleSheet("QTabWidget::pane { border: 1px solid #444; background-color: #1e1e1e; }");

    QWidget* builtinTab = new QWidget();
    QVBoxLayout* builtinLayout = new QVBoxLayout(builtinTab);
    builtinLayout->setContentsMargins(0, 0, 0, 0);
    m_builtinList = new QListWidget();
    m_builtinList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_builtinList->setAlternatingRowColors(true);
    builtinLayout->addWidget(m_builtinList);
    m_tabWidget->addTab(builtinTab, "内置模型");

    QWidget* userTab = new QWidget();
    QVBoxLayout* userLayout = new QVBoxLayout(userTab);
    userLayout->setContentsMargins(0, 0, 0, 0);
    m_userList = new QListWidget();
    m_userList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_userList->setAlternatingRowColors(true);
    userLayout->addWidget(m_userList);
    m_tabWidget->addTab(userTab, "已加载/已注册");

    QWidget* scannedTab = new QWidget();
    QVBoxLayout* scannedLayout = new QVBoxLayout(scannedTab);
    scannedLayout->setContentsMargins(0, 0, 0, 0);
    m_scannedList = new QListWidget();
    m_scannedList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_scannedList->setAlternatingRowColors(true);
    scannedLayout->addWidget(m_scannedList);
    m_tabWidget->addTab(scannedTab, "扫描结果");

    QWidget* trashTab = new QWidget();
    QVBoxLayout* trashLayout = new QVBoxLayout(trashTab);
    trashLayout->setContentsMargins(0, 0, 0, 0);
    m_trashList = new QListWidget();
    m_trashList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_trashList->setAlternatingRowColors(true);
    trashLayout->addWidget(m_trashList);
    m_tabWidget->addTab(trashTab, "回收站(.trash)");
    mainLayout->addWidget(m_tabWidget, 1);

    // --- 详情面板（含完整性校验 + 加载兼容性 + 删除按钮） ---
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

    m_detailLabel = new QLabel("请从列表中选择一个模型以查看详情\n提示：按住 Ctrl 可多选，按住 Shift 可连续选择");
    m_detailLabel->setStyleSheet("color: #aaa; font-size: 12px; padding: 8px;");
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setMinimumHeight(80);
    m_detailLabel->setTextFormat(Qt::PlainText);
    detailLayout->addWidget(m_detailLabel);

    // 加载兼容性结果区
    m_loadabilityLabel = new QLabel("加载兼容性：未检测");
    m_loadabilityLabel->setStyleSheet("background-color: #2d2d2d; color: #888; "
                                      "border-left: 3px solid #555; padding: 8px; font-size: 12px;");
    m_loadabilityLabel->setWordWrap(true);
    detailLayout->addWidget(m_loadabilityLabel);

    // 完整性校验结果区
    m_integrityLabel = new QLabel("完整性校验：未验证");
    m_integrityLabel->setStyleSheet("background-color: #2d2d2d; color: #888; "
                                    "border-left: 3px solid #555; padding: 8px; font-size: 12px;");
    m_integrityLabel->setWordWrap(true);
    detailLayout->addWidget(m_integrityLabel);

    // 操作按钮行：加载/注册 + 验证 + 删除
    QHBoxLayout* itemActionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton("加载选中模型");
    m_loadBtn->setEnabled(false);
    m_loadBtn->setStyleSheet(kBtnPrimaryHover);
    itemActionLayout->addWidget(m_loadBtn);

    m_verifyOneBtn = new QPushButton("验证此项");
    m_verifyOneBtn->setEnabled(false);
    m_verifyOneBtn->setStyleSheet(kBtnVerify);
    itemActionLayout->addWidget(m_verifyOneBtn);

    m_deleteBtn = new QPushButton("删除");
    m_deleteBtn->setEnabled(false);
    m_deleteBtn->setStyleSheet(kBtnDanger);
    itemActionLayout->addWidget(m_deleteBtn);
    detailLayout->addLayout(itemActionLayout);

    // 删除选项
    m_deleteRelatedCheck = new QCheckBox("删除时同时清理关联数据（训练记录/评估报告/日志）");
    m_deleteRelatedCheck->setChecked(false);
    detailLayout->addWidget(m_deleteRelatedCheck);

    mainLayout->addWidget(detailGroup);

    // --- 底部按钮栏 ---
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QLabel* statusLabel = new QLabel("提示：导入即发布到 models/ 目录；删除采用事务性 .trash 机制（可恢复）；不兼容 OpenCV DNN 的模型无法用于推理");
    statusLabel->setStyleSheet("color: #777; font-size: 11px;");
    statusLabel->setWordWrap(true);
    btnLayout->addWidget(statusLabel);
    btnLayout->addStretch();

    m_cancelBtn = new QPushButton("取消");
    m_cancelBtn->setStyleSheet(kBtnSecondary);
    btnLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(btnLayout);

    // --- 信号连接 ---
    connect(m_scanBtn, &QPushButton::clicked, this, &ModelLibraryDialog::onScanDirectory);
    connect(m_loadBtn, &QPushButton::clicked, [this]() {
        // 扫描结果标签页：点击"注册到模型库"
        // 其他标签页：点击"加载选中模型"
        if (m_tabWidget->currentIndex() == 2) {
            onRegisterScannedModel();
        } else {
            onLoadSelected();
        }
    });
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_importBtn, &QPushButton::clicked, this, &ModelLibraryDialog::onImportModel);
    connect(m_deleteBtn, &QPushButton::clicked, this, &ModelLibraryDialog::onDeleteModel);
    connect(m_verifyOneBtn, &QPushButton::clicked, this, &ModelLibraryDialog::onVerifySelected);
    connect(m_verifyAllBtn, &QPushButton::clicked, this, &ModelLibraryDialog::onVerifyAll);
    connect(m_batchRegisterBtn, &QPushButton::clicked, this, &ModelLibraryDialog::onBatchRegister);
    connect(m_batchDeleteBtn, &QPushButton::clicked, this, &ModelLibraryDialog::onBatchDelete);
    connect(m_batchVerifyBtn, &QPushButton::clicked, this, &ModelLibraryDialog::onBatchVerify);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ModelLibraryDialog::onSearchChanged);

    // 选择信号
    connect(m_builtinList, &QListWidget::itemSelectionChanged, this, &ModelLibraryDialog::onModelSelectionChanged);
    connect(m_userList, &QListWidget::itemSelectionChanged, this, &ModelLibraryDialog::onModelSelectionChanged);
    connect(m_scannedList, &QListWidget::itemSelectionChanged, this, &ModelLibraryDialog::onModelSelectionChanged);
    connect(m_trashList, &QListWidget::itemSelectionChanged, this, &ModelLibraryDialog::onModelSelectionChanged);

    // 双击加载
    connect(m_builtinList, &QListWidget::itemDoubleClicked, this, &ModelLibraryDialog::onModelDoubleClicked);
    connect(m_userList, &QListWidget::itemDoubleClicked, this, &ModelLibraryDialog::onModelDoubleClicked);
    connect(m_scannedList, &QListWidget::itemDoubleClicked, this, &ModelLibraryDialog::onModelDoubleClicked);
}

// =============================================================================
// 列表项元数据工具函数
// =============================================================================
void ModelLibraryDialog::setItemData(QListWidgetItem* item, const ModelListItemData& data) {
    if (!item) return;
    item->setData(Qt::UserRole, data.id);
    item->setData(Qt::UserRole + 1, data.path);
    item->setData(Qt::UserRole + 2, data.name);
    item->setData(Qt::UserRole + 3, data.source);
    item->setData(Qt::UserRole + 4, data.fileExists);
    item->setData(Qt::UserRole + 5, data.loadable);
    item->setData(Qt::UserRole + 6, data.loadableKnown);
    item->setData(Qt::UserRole + 7, data.loadError);
}

ModelListItemData ModelLibraryDialog::itemData(QListWidgetItem* item) const {
    ModelListItemData data;
    if (!item) return data;
    data.id = item->data(Qt::UserRole).toString();
    data.path = item->data(Qt::UserRole + 1).toString();
    data.name = item->data(Qt::UserRole + 2).toString();
    data.source = item->data(Qt::UserRole + 3).toString();
    data.fileExists = item->data(Qt::UserRole + 4).toBool();
    data.loadable = item->data(Qt::UserRole + 5).toBool();
    data.loadableKnown = item->data(Qt::UserRole + 6).toBool();
    data.loadError = item->data(Qt::UserRole + 7).toString();
    return data;
}

QListWidget* ModelLibraryDialog::currentListWidget() const {
    int idx = m_tabWidget->currentIndex();
    if (idx == 0) return m_builtinList;
    if (idx == 1) return m_userList;
    if (idx == 2) return m_scannedList;
    if (idx == 3) return m_trashList;
    return nullptr;
}

QList<ModelListItemData> ModelLibraryDialog::selectedItemDatas() const {
    QList<ModelListItemData> result;
    QListWidget* list = currentListWidget();
    if (!list) return result;
    for (QListWidgetItem* item : list->selectedItems()) {
        result.append(itemData(item));
    }
    return result;
}

QString ModelLibraryDialog::formatFileSize(qint64 bytes) const {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024);
    if (bytes < 1024LL * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

// =============================================================================
// 数据加载
// =============================================================================
void ModelLibraryDialog::refreshAllLists() {
    populateBuiltinModels();
    populateUserModels();
    populateTrashModels();
    // 扫描结果：如果用户已经扫描过某个目录，删除/注册后应该重新扫描该目录以反映最新状态
    // 注意：这里不切换当前标签页，避免打断用户当前正在查看的标签页
    if (!m_lastScanDir.isEmpty() && QDir(m_lastScanDir).exists()) {
        scanDirectory(m_lastScanDir, false);
    }
    applySearchFilter(m_searchEdit->text());
}

void ModelLibraryDialog::populateBuiltinModels() {
    m_builtinList->clear();
    m_builtinRegistry.clear();

    for (const BuiltinModelEntry& entry : builtinModels()) {
        m_builtinRegistry[entry.id] = entry;

        QListWidgetItem* item = new QListWidgetItem();
        item->setText(QString("● %1  [%2]").arg(entry.name).arg(entry.category));
        item->setToolTip(entry.description);

        ModelListItemData data;
        data.id = entry.id;
        data.name = entry.name;
        data.path = entry.path;
        data.source = "builtin";
        data.fileExists = false;  // 内置模型文件可能不存在
        data.loadableKnown = false;
        setItemData(item, data);
        m_builtinList->addItem(item);
    }

    m_tabWidget->setTabText(0, QString("内置模型 (%1)").arg(m_builtinList->count()));
}

void ModelLibraryDialog::populateUserModels() {
    m_userList->clear();

    QSet<QString> seenPaths;
    QString modelsDir = ModelManager::instance()->defaultModelDirectory();
    QJsonArray manifestModels = ModelManager::instance()->manifestModels();

    // 1. 显示已经加载到引擎缓存中的模型
    QList<ModelInfo> loadedModels = ModelManager::instance()->loadedModelInfo();
    for (const ModelInfo& info : loadedModels) {
        QListWidgetItem* item = new QListWidgetItem();
        QString status = info.avgInferenceMs > 0
            ? QString(" [预热完成] %.1fms").arg(info.avgInferenceMs)
            : " [未预热]";
        item->setText(QString("● %1%2").arg(QFileInfo(info.path).fileName()).arg(status));

        ModelListItemData data;
        data.id = info.id;
        data.name = QFileInfo(info.path).fileName();
        data.path = info.path;
        data.source = "loaded";
        data.fileExists = QFile::exists(info.path);
        data.loadableKnown = true;
        data.loadable = true;
        setItemData(item, data);

        item->setToolTip(QString("路径: %1\n输入尺寸: %2x%3\n访问次数: %4")
            .arg(info.path)
            .arg(info.inputSize.width())
            .arg(info.inputSize.height())
            .arg(info.accessCount));
        m_userList->addItem(item);
        seenPaths.insert(info.path);
    }

    // 2. 显示 manifest 中已注册（含 addCustomModel 导入）但尚未加载的模型
    for (const QJsonValue& value : manifestModels) {
        QJsonObject obj = value.toObject();
        QString fileName = obj["file_name"].toString();
        if (fileName.isEmpty()) {
            continue;
        }
        QString path = QDir(modelsDir).absoluteFilePath(fileName);
        if (seenPaths.contains(path)) {
            continue;
        }

        QString displayName = obj["display_name"].toString();
        if (displayName.isEmpty()) {
            displayName = QFileInfo(path).completeBaseName();
        }

        bool exists = QFile::exists(path);
        bool loadable = obj["loadable"].toBool(false);
        bool loadableKnown = obj.contains("loadable");
        QString loadError = obj["load_error"].toString();

        QListWidgetItem* item = new QListWidgetItem();
        QString marker = exists ? (loadableKnown ? (loadable ? "●" : "●") : "●") : "●";
        QString existenceMark = exists ? "" : " [文件缺失]";
        QString loadableMark = loadableKnown ? (loadable ? "" : " [不兼容OpenCV]") : " [未检测兼容性]";
        item->setText(QString("%1 %2 [已注册]%3%4").arg(marker).arg(displayName).arg(existenceMark).arg(loadableMark));

        ModelListItemData data;
        data.id = displayName;
        data.name = displayName;
        data.path = path;
        data.source = "registered";
        data.fileExists = exists;
        data.loadable = loadable;
        data.loadableKnown = loadableKnown;
        data.loadError = loadError;
        setItemData(item, data);

        if (!exists) {
            item->setForeground(QColor("#f44336"));  // 红色标记缺失文件
        } else if (loadableKnown && !loadable) {
            item->setForeground(QColor("#ff9800"));  // 橙色标记不兼容
        }
        item->setToolTip(QString("路径: %1\n来源: manifest\nSHA256: %2\n兼容性: %3")
            .arg(path)
            .arg(obj["sha256"].toString())
            .arg(loadableKnown ? (loadable ? "可加载" : "不兼容 OpenCV DNN") : "未检测"));
        m_userList->addItem(item);
        seenPaths.insert(path);
    }

    m_tabWidget->setTabText(1, QString("已加载/已注册 (%1)").arg(m_userList->count()));
}

void ModelLibraryDialog::populateTrashModels() {
    m_trashList->clear();

    QString modelsDir = ModelManager::instance()->defaultModelDirectory();
    QDir dir(modelsDir);
    if (!dir.exists()) return;

    QStringList filters = {"*.trash"};
    QFileInfoList trashFiles = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

    if (trashFiles.isEmpty()) {
        QListWidgetItem* item = new QListWidgetItem();
        item->setText("回收站为空（无 .trash 文件）");
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setForeground(QColor("#888"));
        m_trashList->addItem(item);
        m_tabWidget->setTabText(3, "回收站(.trash) (0)");
        return;
    }

    for (const QFileInfo& fi : trashFiles) {
        QListWidgetItem* item = new QListWidgetItem();
        item->setText(QString("● %1 (%2)").arg(fi.fileName()).arg(formatFileSize(fi.size())));

        ModelListItemData data;
        data.id = fi.completeBaseName();
        data.name = fi.fileName();
        data.path = fi.absoluteFilePath();
        data.source = "trash";
        data.fileExists = true;
        data.loadableKnown = false;
        setItemData(item, data);

        item->setForeground(QColor("#ff9800"));  // 橙色标记回收站
        item->setToolTip(QString("路径: %1\n大小: %2\n修改时间: %3\n提示：可手动去除 .trash 后缀恢复")
            .arg(fi.absoluteFilePath())
            .arg(formatFileSize(fi.size()))
            .arg(fi.lastModified().toString("yyyy-MM-dd hh:mm:ss")));
        m_trashList->addItem(item);
    }

    m_tabWidget->setTabText(3, QString("回收站(.trash) (%1)").arg(m_trashList->count()));
}

void ModelLibraryDialog::onScanDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this,
        "选择模型目录",
        m_lastScanDir.isEmpty() ? QDir::currentPath() : m_lastScanDir);

    if (dir.isEmpty()) return;

    m_lastScanDir = dir;
    scanDirectory(dir);
}

void ModelLibraryDialog::scanDirectory(const QString& dir, bool switchToScanTab) {
    m_scannedList->clear();
    QStringList models = ModelManager::instance()->listModels(dir);

    if (models.isEmpty()) {
        QListWidgetItem* item = new QListWidgetItem();
        item->setText("未找到模型文件 (.onnx/.pth/.pt/.bin)");
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setForeground(QColor("#888"));
        m_scannedList->addItem(item);
        if (switchToScanTab) {
            m_tabWidget->setCurrentIndex(2);
        }
        m_tabWidget->setTabText(2, "扫描结果 (0)");
        return;
    }

    for (const QString& path : models) {
        QFileInfo fi(path);
        QListWidgetItem* item = new QListWidgetItem();

        // 扫描时即时预检加载兼容性（可能稍慢，但信息最准确）
        auto probe = ModelManager::instance()->probeModelLoadability(path);
        QString loadMark = probe.first ? "" : " [不兼容OpenCV]";

        item->setText(QString("● %1%2").arg(fi.fileName()).arg(loadMark));

        ModelListItemData data;
        data.id = fi.baseName();
        data.name = fi.fileName();
        data.path = path;
        data.source = "scanned";
        data.fileExists = true;
        data.loadable = probe.first;
        data.loadableKnown = true;
        data.loadError = probe.second;
        setItemData(item, data);

        if (!probe.first) {
            item->setForeground(QColor("#ff9800"));
        }
        item->setToolTip(QString("路径: %1\n大小: %2\n兼容性: %3")
            .arg(path)
            .arg(formatFileSize(fi.size()))
            .arg(probe.first ? "可加载" : "不兼容 OpenCV DNN"));
        m_scannedList->addItem(item);
    }

    if (switchToScanTab) {
        m_tabWidget->setCurrentIndex(2);
    }
    m_tabWidget->setTabText(2, QString("扫描结果 (%1)").arg(m_scannedList->count()));
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

// =============================================================================
// 选择变化处理（支持多选）
// =============================================================================
void ModelLibraryDialog::onModelSelectionChanged() {
    QList<ModelListItemData> selected = selectedItemDatas();

    if (selected.isEmpty()) {
        m_loadBtn->setEnabled(false);
        m_deleteBtn->setEnabled(false);
        m_verifyOneBtn->setEnabled(false);
        m_batchRegisterBtn->setEnabled(false);
        m_batchDeleteBtn->setEnabled(false);
        m_batchVerifyBtn->setEnabled(false);
        m_detailLabel->setText("请从列表中选择一个模型以查看详情\n提示：按住 Ctrl 可多选，按住 Shift 可连续选择");
        m_integrityLabel->setText("完整性校验：未验证");
        m_integrityLabel->setStyleSheet("background-color: #2d2d2d; color: #888; "
                                        "border-left: 3px solid #555; padding: 8px; font-size: 12px;");
        m_loadabilityLabel->setText("加载兼容性：未检测");
        m_loadabilityLabel->setStyleSheet("background-color: #2d2d2d; color: #888; "
                                          "border-left: 3px solid #555; padding: 8px; font-size: 12px;");
        m_selectedPath.clear();
        m_selectedName.clear();
        return;
    }

    // 单选：显示详情；多选：显示汇总
    if (selected.size() == 1) {
        const ModelListItemData& data = selected.first();
        m_selectedPath = data.path;
        m_selectedName = data.name;
        showModelDetails(data.path, data.name);
        probeAndShowLoadability(data.path);

        // 扫描结果：提供"注册到模型库"按钮
        if (data.source == "scanned") {
            m_loadBtn->setText("注册到模型库");
            m_loadBtn->setEnabled(true);
            m_verifyOneBtn->setEnabled(false);
            m_deleteBtn->setEnabled(true);
            m_batchRegisterBtn->setEnabled(false);
            m_batchDeleteBtn->setEnabled(false);
            m_batchVerifyBtn->setEnabled(false);
            return;
        }

        // 恢复默认加载按钮文本
        m_loadBtn->setText("加载选中模型");

        // 加载按钮：内置/已加载/已注册可加载；回收站不可加载
        bool canLoad = (data.source == "builtin" || data.source == "loaded" || data.source == "registered");
        m_loadBtn->setEnabled(canLoad);

        // 删除按钮：仅"已注册"来源可删除；回收站项也不在此删除
        bool canDelete = (data.source == "registered");
        m_deleteBtn->setEnabled(canDelete);

        // 验证按钮：已注册和已加载可验证
        bool canVerify = (data.source == "registered" || data.source == "loaded");
        m_verifyOneBtn->setEnabled(canVerify);

        // 批量按钮：单选时禁用
        m_batchRegisterBtn->setEnabled(false);
        m_batchDeleteBtn->setEnabled(false);
        m_batchVerifyBtn->setEnabled(false);
    } else {
        // 多选模式
        m_selectedPath.clear();
        m_selectedName.clear();
        showMultiSelectionSummary(selected);

        int currentTab = m_tabWidget->currentIndex();
        bool hasScanned = false;
        bool hasDeletable = false;
        bool hasVerifiable = false;
        for (const auto& data : selected) {
            if (data.source == "scanned") hasScanned = true;
            if (data.source == "registered") hasDeletable = true;
            if (data.source == "registered" || data.source == "loaded") hasVerifiable = true;
        }

        // 加载按钮：多选时不提供加载（语义不清）
        m_loadBtn->setText("加载选中模型");
        m_loadBtn->setEnabled(false);
        m_verifyOneBtn->setEnabled(false);
        m_deleteBtn->setEnabled(false);

        // 批量注册：仅在扫描结果标签页可用
        m_batchRegisterBtn->setEnabled(currentTab == 2 && hasScanned);
        // 批量删除：仅当所有选中项都可删除（注册模型或扫描外部文件）
        m_batchDeleteBtn->setEnabled(hasDeletable || (currentTab == 2 && hasScanned));
        // 批量验证：仅包含已注册/已加载模型
        m_batchVerifyBtn->setEnabled(hasVerifiable);
    }
}

void ModelLibraryDialog::showMultiSelectionSummary(const QList<ModelListItemData>& selected) {
    int scannedCount = 0;
    int registeredCount = 0;
    int loadedCount = 0;
    int builtinCount = 0;
    int trashCount = 0;
    int incompatibleCount = 0;
    qint64 totalSize = 0;

    for (const auto& data : selected) {
        if (data.source == "scanned") scannedCount++;
        else if (data.source == "registered") registeredCount++;
        else if (data.source == "loaded") loadedCount++;
        else if (data.source == "builtin") builtinCount++;
        else if (data.source == "trash") trashCount++;
        if (data.loadableKnown && !data.loadable) incompatibleCount++;
        if (data.fileExists) {
            totalSize += QFileInfo(data.path).size();
        }
    }

    QString summary = QString("已选择 %1 个模型\n").arg(selected.size());
    if (builtinCount > 0) summary += QString("内置模型: %1\n").arg(builtinCount);
    if (loadedCount > 0) summary += QString("已加载: %1\n").arg(loadedCount);
    if (registeredCount > 0) summary += QString("已注册: %1\n").arg(registeredCount);
    if (scannedCount > 0) summary += QString("扫描结果: %1\n").arg(scannedCount);
    if (trashCount > 0) summary += QString("回收站: %1\n").arg(trashCount);
    summary += QString("总大小: %1\n").arg(formatFileSize(totalSize));
    if (incompatibleCount > 0) {
        summary += QString("注意: %1 个模型不兼容当前 OpenCV DNN 后端").arg(incompatibleCount);
    }

    m_detailLabel->setText(summary.trimmed());
    m_integrityLabel->setText("完整性校验：多选模式下请使用右上角『批量验证』");
    m_integrityLabel->setStyleSheet("background-color: #2d2d2d; color: #888; "
                                    "border-left: 3px solid #555; padding: 8px; font-size: 12px;");
    m_loadabilityLabel->setText("加载兼容性：多选模式下请查看列表项颜色标记");
    m_loadabilityLabel->setStyleSheet("background-color: #2d2d2d; color: #888; "
                                      "border-left: 3px solid #555; padding: 8px; font-size: 12px;");
}

void ModelLibraryDialog::probeAndShowLoadability(const QString& modelPath) {
    if (modelPath.isEmpty() || !QFile::exists(modelPath)) {
        m_loadabilityLabel->setText("加载兼容性：文件不存在，无法检测");
        m_loadabilityLabel->setStyleSheet("background-color: #3a1b1b; color: #f88; "
                                          "border-left: 3px solid #f44336; padding: 8px; font-size: 12px;");
        return;
    }

    auto probe = ModelManager::instance()->probeModelLoadability(modelPath);
    if (probe.first) {
        m_loadabilityLabel->setText("✓ 加载兼容性：当前 OpenCV DNN 后端可加载");
        m_loadabilityLabel->setStyleSheet("background-color: #1b3a1b; color: #8f8; "
                                          "border-left: 3px solid #4caf50; padding: 8px; font-size: 12px;");
    } else {
        QString err = probe.second;
        // 截取关键错误信息，避免过长
        if (err.length() > 300) err = err.left(300) + "...";
        m_loadabilityLabel->setText(QString("✗ 加载兼容性：%1").arg(err));
        m_loadabilityLabel->setStyleSheet("background-color: #3a1b1b; color: #f88; "
                                          "border-left: 3px solid #f44336; padding: 8px; font-size: 12px;");
    }
}

void ModelLibraryDialog::showModelDetails(const QString& modelPath, const QString& modelName) {
    QFileInfo fi(modelPath);
    QString details;

    details += QString("名称: %1\n").arg(modelName);
    details += QString("路径: %1\n").arg(modelPath);

    if (fi.exists()) {
        details += QString("大小: %1\n").arg(formatFileSize(fi.size()));
        details += QString("修改时间: %1\n").arg(fi.lastModified().toString("yyyy-MM-dd hh:mm:ss"));
    } else {
        details += "状态: 文件不存在（需下载或配置）\n";
    }

    QListWidget* list = currentListWidget();
    QListWidgetItem* currentItem = list ? list->currentItem() : nullptr;
    QString id = currentItem ? currentItem->data(Qt::UserRole).toString() : QString();

    if (m_builtinRegistry.contains(id)) {
        const BuiltinModelEntry& entry = m_builtinRegistry[id];
        details += QString("输入尺寸: %1x%2\n").arg(entry.width).arg(entry.height);
        details += QString("分类: %1\n").arg(entry.category);
        details += QString("描述: %1\n").arg(entry.description);
    }

    QJsonArray manifestModels = ModelManager::instance()->manifestModels();
    QString modelFileName = fi.fileName();
    for (const QJsonValue& value : manifestModels) {
        QJsonObject obj = value.toObject();
        if (obj["file_name"].toString() == modelFileName) {
            details += QString("SHA256: %1\n").arg(obj["sha256"].toString());
            if (obj.contains("labels_file")) {
                details += QString("标签文件: %1\n").arg(obj["labels_file"].toString());
            }
            if (obj.contains("registered_at")) {
                details += QString("注册时间: %1\n").arg(obj["registered_at"].toString());
            }
            if (obj.contains("description")) {
                details += QString("描述: %1\n").arg(obj["description"].toString());
            }
            if (obj.contains("loadable")) {
                bool loadable = obj["loadable"].toBool(false);
                details += QString("兼容性: %1\n").arg(loadable ? "可加载" : "不兼容 OpenCV DNN");
            }
            break;
        }
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

    // 重置完整性校验状态
    m_integrityLabel->setText("完整性校验：未验证（点击下方『验证此项』按钮检查）");
    m_integrityLabel->setStyleSheet("background-color: #2d2d2d; color: #888; "
                                    "border-left: 3px solid #555; padding: 8px; font-size: 12px;");
}

void ModelLibraryDialog::showIntegrityResult(bool ok, const QString& detail) {
    if (ok) {
        m_integrityLabel->setText(QString("✓ 完整性校验通过：%1").arg(detail));
        m_integrityLabel->setStyleSheet("background-color: #1b3a1b; color: #8f8; "
                                        "border-left: 3px solid #4caf50; padding: 8px; font-size: 12px;");
    } else {
        m_integrityLabel->setText(QString("✗ 完整性校验失败：%1").arg(detail));
        m_integrityLabel->setStyleSheet("background-color: #3a1b1b; color: #f88; "
                                        "border-left: 3px solid #f44336; padding: 8px; font-size: 12px;");
    }
}

// =============================================================================
// v2.7.1 / v2.7.2 功能实现
// =============================================================================

void ModelLibraryDialog::onSearchChanged(const QString& text) {
    applySearchFilter(text);
}

void ModelLibraryDialog::applySearchFilter(const QString& text) {
    QListWidget* currentList = currentListWidget();
    if (!currentList) return;

    QString keyword = text.trimmed().toLower();
    for (int i = 0; i < currentList->count(); ++i) {
        QListWidgetItem* item = currentList->item(i);
        if (keyword.isEmpty()) {
            item->setHidden(false);
            continue;
        }
        ModelListItemData data = itemData(item);
        bool match = data.name.toLower().contains(keyword)
                  || data.path.toLower().contains(keyword)
                  || item->toolTip().toLower().contains(keyword);
        item->setHidden(!match);
    }
}

void ModelLibraryDialog::onImportModel() {
    QString lastDir = m_lastScanDir.isEmpty() ? QDir::homePath() : m_lastScanDir;
    QString onnxPath = QFileDialog::getOpenFileName(this,
        "选择要导入的 ONNX 模型文件",
        lastDir,
        "ONNX 模型 (*.onnx);;所有文件 (*.*)");

    if (onnxPath.isEmpty()) return;

    QString defaultName = QFileInfo(onnxPath).completeBaseName();
    QString displayName = QInputDialog::getText(this,
        "输入模型显示名",
        "模型显示名（用作模型 ID，重名时会自动追加时间戳）：",
        QLineEdit::Normal,
        defaultName);

    if (displayName.isEmpty()) return;

    setEnabled(false);
    QCoreApplication::processEvents();

    bool ok = ModelManager::instance()->addCustomModel(onnxPath, displayName);

    setEnabled(true);

    if (ok) {
        QMessageBox::information(this, "导入成功",
            QString("模型 '%1' 已成功导入到 models/ 目录").arg(displayName));
        refreshAllLists();
        m_tabWidget->setCurrentIndex(1);
    } else {
        QMessageBox::warning(this, "导入失败",
            QString("导入模型 '%1' 失败，请查看日志了解详情").arg(displayName));
    }
}

void ModelLibraryDialog::onDeleteModel() {
    QList<ModelListItemData> selected = selectedItemDatas();
    if (selected.isEmpty()) return;

    const ModelListItemData& data = selected.first();
    if (data.path.isEmpty() || data.name.isEmpty()) return;

    bool isScanned = (data.source == "scanned");

    if (isScanned) {
        QString modelsDir = ModelManager::instance()->defaultModelDirectory();
        bool insideModelsDir = data.path.startsWith(modelsDir + "/") ||
                               data.path.startsWith(modelsDir + "\\");

        QString msg;
        if (insideModelsDir) {
            msg = QString("确认删除模型 '%1'？\n\n").arg(data.name);
            msg += "该文件位于 models/ 目录内，删除操作采用事务性机制：\n";
            msg += "文件会被重命名为 .trash 后缀（可手动恢复）。\n";
            msg += "manifest.json 中会记录删除审计日志。";
        } else {
            msg = QString("确认删除模型文件 '%1'？\n\n").arg(data.name);
            msg += "警告：该文件位于 models/ 目录外，删除将直接从磁盘移除，不可恢复！\n";
            msg += "路径: " + data.path + "\n\n";
            msg += "如果希望保留原文件，请使用『注册到模型库』功能将其复制到 models/ 目录。";
        }

        auto ret = QMessageBox::warning(this, "确认删除模型文件", msg,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (ret != QMessageBox::Yes) return;

        bool ok = false;
        if (insideModelsDir) {
            ok = ModelManager::instance()->removeCustomModelTransactional(data.name, false);
        } else {
            ok = QFile::remove(data.path);
            if (ok) {
                QDV::Logger::info(QString("ModelLibrary: 已直接删除扫描到的外部模型文件: %1").arg(data.path));
            }
        }

        if (ok) {
            QMessageBox::information(this, "删除成功", QString("模型 '%1' 已删除").arg(data.name));
            refreshAllLists();
        } else {
            QMessageBox::warning(this, "删除失败", QString("删除模型 '%1' 失败").arg(data.name));
        }
        return;
    }

    // 已注册模型的事务性删除
    QString msg = QString("确认删除模型 '%1'？\n\n").arg(data.name);
    msg += "删除操作采用事务性机制：文件会被重命名为 .trash 后缀（可手动恢复）。\n";
    msg += "manifest.json 中会记录删除审计日志。\n";
    if (m_deleteRelatedCheck->isChecked()) {
        msg += "\n☑ 将同时清理关联数据（训练记录、评估报告、日志等）";
    } else {
        msg += "\n☐ 不清理关联数据";
    }

    auto ret = QMessageBox::question(this, "确认删除模型", msg,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    bool deleteRelated = m_deleteRelatedCheck->isChecked();
    bool ok = ModelManager::instance()->removeCustomModelTransactional(data.name, deleteRelated);

    if (ok) {
        QMessageBox::information(this, "删除成功",
            QString("模型 '%1' 已事务性删除（文件已重命名为 .trash）").arg(data.name));
        refreshAllLists();
    } else {
        QMessageBox::warning(this, "删除失败",
            QString("删除模型 '%1' 失败，请查看日志了解详情").arg(data.name));
    }
}

void ModelLibraryDialog::onVerifySelected() {
    QList<ModelListItemData> selected = selectedItemDatas();
    if (selected.isEmpty()) return;

    const ModelListItemData& data = selected.first();
    if (data.name.isEmpty()) return;

    setEnabled(false);
    QCoreApplication::processEvents();

    bool ok = ModelManager::instance()->verifyModelIntegrity(data.name);

    setEnabled(true);

    QFileInfo fi(data.path);
    QString detail = QString("%1 (大小: %2)").arg(fi.fileName()).arg(formatFileSize(fi.size()));
    showIntegrityResult(ok, detail);
}

void ModelLibraryDialog::onVerifyAll() {
    QJsonArray manifestModels = ModelManager::instance()->manifestModels();
    if (manifestModels.isEmpty()) {
        QMessageBox::information(this, "批量验证", "manifest 中无已注册模型");
        return;
    }

    setEnabled(false);
    QCoreApplication::processEvents();

    int total = 0, passed = 0, failed = 0, missing = 0;
    QStringList failures;
    QString modelsDir = ModelManager::instance()->defaultModelDirectory();

    for (const QJsonValue& value : manifestModels) {
        QJsonObject obj = value.toObject();
        QString fileName = obj["file_name"].toString();
        if (fileName.isEmpty()) continue;

        total++;
        QString modelId = QFileInfo(fileName).completeBaseName();
        QString fullPath = QDir(modelsDir).absoluteFilePath(fileName);

        if (!QFile::exists(fullPath)) {
            missing++;
            failures << QString("%1: 文件缺失").arg(modelId);
            continue;
        }

        if (ModelManager::instance()->verifyModelIntegrity(modelId)) {
            passed++;
        } else {
            failed++;
            failures << QString("%1: SHA256 或大小不匹配").arg(modelId);
        }
    }

    setEnabled(true);

    QString summary = QString("批量验证完成：共 %1 项，通过 %2，失败 %3，缺失 %4")
                          .arg(total).arg(passed).arg(failed).arg(missing);
    if (!failures.isEmpty()) {
        summary += "\n\n失败明细：\n" + failures.join("\n");
    }

    bool allOk = (failed == 0 && missing == 0);
    showIntegrityResult(allOk, QString("批量验证 %1 项").arg(total));

    QMessageBox::information(this, "批量验证结果", summary);
}

// =============================================================================
// v2.7.2 批量操作
// =============================================================================
void ModelLibraryDialog::onBatchRegister() {
    QList<ModelListItemData> selected = selectedItemDatas();
    QList<ModelListItemData> toRegister;
    for (const auto& data : selected) {
        if (data.source == "scanned") toRegister.append(data);
    }

    if (toRegister.isEmpty()) {
        QMessageBox::information(this, "批量注册", "请在扫描结果标签页中选择要注册的模型");
        return;
    }

    auto ret = QMessageBox::question(this, "批量注册",
        QString("确认将 %1 个扫描到的模型注册到模型库？\n"
                "注册时会自动复制到 models/ 目录并写入 manifest。")
            .arg(toRegister.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    setEnabled(false);
    QCoreApplication::processEvents();

    int success = 0, failed = 0;
    QStringList failedNames;
    for (const auto& data : toRegister) {
        QString displayName = QFileInfo(data.path).completeBaseName();
        if (ModelManager::instance()->addCustomModel(data.path, displayName)) {
            success++;
        } else {
            failed++;
            failedNames << displayName;
        }
    }

    setEnabled(true);

    QString msg = QString("批量注册完成：成功 %1，失败 %2").arg(success).arg(failed);
    if (!failedNames.isEmpty()) {
        msg += "\n\n失败列表：\n" + failedNames.join("\n");
    }
    QMessageBox::information(this, "批量注册结果", msg);
    refreshAllLists();
    m_tabWidget->setCurrentIndex(1);
}

void ModelLibraryDialog::onBatchDelete() {
    QList<ModelListItemData> selected = selectedItemDatas();
    if (selected.isEmpty()) return;

    int registeredCount = 0;
    int scannedCount = 0;
    for (const auto& data : selected) {
        if (data.source == "registered") registeredCount++;
        else if (data.source == "scanned") scannedCount++;
    }

    if (registeredCount == 0 && scannedCount == 0) {
        QMessageBox::information(this, "批量删除", "当前选中的模型不支持删除（仅支持已注册模型和扫描结果中的外部文件）");
        return;
    }

    QString modelsDir = ModelManager::instance()->defaultModelDirectory();
    int externalCount = 0;
    for (const auto& data : selected) {
        if (data.source == "scanned" && !data.path.startsWith(modelsDir + "/") && !data.path.startsWith(modelsDir + "\\")) {
            externalCount++;
        }
    }

    QString msg = QString("确认删除选中的 %1 个模型？\n").arg(selected.size());
    if (registeredCount > 0) {
        msg += QString("• %1 个已注册模型将事务性删除（重命名为 .trash，可恢复）\n").arg(registeredCount);
    }
    if (scannedCount > 0) {
        if (externalCount > 0) {
            msg += QString("• %1 个外部扫描文件将直接从磁盘永久删除（不可恢复）\n").arg(externalCount);
        }
        msg += QString("• models/ 目录内的扫描文件将事务性删除\n");
    }
    msg += "\n此操作不可撤销，是否继续？";

    auto ret = QMessageBox::warning(this, "批量删除", msg,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    setEnabled(false);
    QCoreApplication::processEvents();

    int success = 0, failed = 0;
    QStringList failedNames;

    for (const auto& data : selected) {
        bool ok = false;
        if (data.source == "registered") {
            ok = ModelManager::instance()->removeCustomModelTransactional(data.name, false);
        } else if (data.source == "scanned") {
            bool insideModelsDir = data.path.startsWith(modelsDir + "/") || data.path.startsWith(modelsDir + "\\");
            if (insideModelsDir) {
                ok = ModelManager::instance()->removeCustomModelTransactional(data.name, false);
            } else {
                ok = QFile::remove(data.path);
            }
        }
        if (ok) success++; else { failed++; failedNames << data.name; }
    }

    setEnabled(true);

    QString msg2 = QString("批量删除完成：成功 %1，失败 %2").arg(success).arg(failed);
    if (!failedNames.isEmpty()) {
        msg2 += "\n\n失败列表：\n" + failedNames.join("\n");
    }
    QMessageBox::information(this, "批量删除结果", msg2);
    refreshAllLists();
}

void ModelLibraryDialog::onBatchVerify() {
    QList<ModelListItemData> selected = selectedItemDatas();
    QList<ModelListItemData> toVerify;
    for (const auto& data : selected) {
        if (data.source == "registered" || data.source == "loaded") {
            toVerify.append(data);
        }
    }

    if (toVerify.isEmpty()) {
        QMessageBox::information(this, "批量验证", "请选择已注册或已加载的模型进行验证");
        return;
    }

    setEnabled(false);
    QCoreApplication::processEvents();

    int passed = 0, failed = 0, missing = 0;
    QStringList failures;

    for (const auto& data : toVerify) {
        if (!QFile::exists(data.path)) {
            missing++;
            failures << QString("%1: 文件缺失").arg(data.name);
            continue;
        }
        if (ModelManager::instance()->verifyModelIntegrity(data.name)) {
            passed++;
        } else {
            failed++;
            failures << QString("%1: SHA256 或大小不匹配").arg(data.name);
        }
    }

    setEnabled(true);

    QString summary = QString("批量验证完成：共 %1 项，通过 %2，失败 %3，缺失 %4")
                          .arg(toVerify.size()).arg(passed).arg(failed).arg(missing);
    if (!failures.isEmpty()) {
        summary += "\n\n失败明细：\n" + failures.join("\n");
    }

    bool allOk = (failed == 0 && missing == 0);
    showIntegrityResult(allOk, QString("批量验证 %1 项").arg(toVerify.size()));
    QMessageBox::information(this, "批量验证结果", summary);
}

void ModelLibraryDialog::onRegisterScannedModel() {
    QList<ModelListItemData> selected = selectedItemDatas();
    if (selected.isEmpty()) return;

    const ModelListItemData& data = selected.first();
    if (data.path.isEmpty()) return;

    QString defaultName = QFileInfo(data.path).completeBaseName();
    QString displayName = QInputDialog::getText(this,
        "注册到模型库",
        "模型显示名（用作模型 ID，重名时会自动追加时间戳）：",
        QLineEdit::Normal,
        defaultName);

    if (displayName.isEmpty()) return;

    setEnabled(false);
    QCoreApplication::processEvents();

    bool ok = ModelManager::instance()->addCustomModel(data.path, displayName);

    setEnabled(true);

    if (ok) {
        QMessageBox::information(this, "注册成功",
            QString("模型 '%1' 已成功注册到模型库").arg(displayName));
        refreshAllLists();
        m_tabWidget->setCurrentIndex(1);
    } else {
        QMessageBox::warning(this, "注册失败",
            QString("注册模型 '%1' 失败").arg(displayName));
    }
}

void ModelLibraryDialog::onModelDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    ModelListItemData data = itemData(item);
    if (data.source == "trash") return;
    m_selectedPath = data.path;
    m_selectedName = data.name;
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
