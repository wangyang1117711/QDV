// ============================================================================
// DataCollectionWizard — 数据收集向导实现（spec v2 阶段五 Task 13）
//
// 4 步向导：
//   Page 1: 类别 + 输出目录（默认 D:\QDV\datasets\<className>）
//   Page 2: 选择图像目录 → 加载图像列表
//   Page 3: 矩形框标注（QGraphicsScene + 鼠标拖拽）
//   Page 4: 导出 YOLO 格式数据集 + 配置训练参数
//
// 导出 YOLO 格式目录结构：
//   <outputDir>/
//     images/
//       train/  (80% 图像)
//       val/    (20% 图像)
//     labels/
//       train/
//       val/
//     data.yaml
//
// 每个 .txt 文件格式（YOLO 归一化坐标）：
//   <classId> <cx> <cy> <w> <h>
//
// data.yaml 格式：
//   path: <outputDir>
//   train: images/train
//   val: images/val
//   names:
//     0: <className>
// ============================================================================

#include "UI/DataCollectionWizard.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QWizardPage>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsView>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>

namespace QDV {

// ============================================================================
// 构造 / 析构
// ============================================================================
DataCollectionWizard::DataCollectionWizard(QWidget* parent)
    : QWizard(parent)
{
    setWindowTitle(tr("数据收集向导 — 训练专用 YOLO 模型"));
    setMinimumSize(900, 650);
    setOption(QWizard::IndependentPages, false);  // 顺序页面，禁用自由跳转
    setOption(QWizard::HaveHelpButton, false);

    setupPage1ClassSelect();
    setupPage2ImageSelect();
    setupPage3Annotate();
    setupPage4Export();
}

DataCollectionWizard::~DataCollectionWizard() = default;

void DataCollectionWizard::setTargetEntry(const QVariantMap& entry) {
    m_entry = entry;

    // Step 1：自动填充类别名、提示词、中文名、默认输出目录
    QString name   = entry.value("name").toString();
    QString prompt = entry.value("prompt").toString();
    QString cnName = entry.value("cnName").toString();

    if (m_classNameEdit)   m_classNameEdit->setText(name.isEmpty() ? prompt : name);
    if (m_classPromptEdit) m_classPromptEdit->setText(prompt);
    if (m_cnNameEdit)      m_cnNameEdit->setText(cnName);

    // 默认输出目录：D:\QDV\datasets\<className>（遵守 AGENTS.md D 盘存储要求）
    QString safeName = (name.isEmpty() ? prompt : name);
    safeName.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_\\-]")), QStringLiteral("_"));
    if (m_outputDirEdit) {
        QString defaultDir = QStringLiteral("D:/QDV/datasets/") + safeName;
        m_outputDirEdit->setText(defaultDir);
    }

    // Step 4：默认训练输出目录（D 盘）
    if (m_trainOutputDirEdit) {
        QString trainOut = QStringLiteral("D:/QDV/models/") + safeName;
        m_trainOutputDirEdit->setText(trainOut);
    }
}

// ============================================================================
// Step 1: 类别 + 输出目录
// ============================================================================
void DataCollectionWizard::setupPage1ClassSelect() {
    m_page1 = new QWizardPage(this);
    m_page1->setTitle(tr("步骤 1 / 4：选择目标类别与输出目录"));
    m_page1->setSubTitle(tr("确认目标类别信息并选择数据集输出目录（默认 D 盘）"));

    QVBoxLayout* layout = new QVBoxLayout(m_page1);

    QFormLayout* form = new QFormLayout();
    m_classNameEdit = new QLineEdit(m_page1);
    m_classNameEdit->setPlaceholderText(tr("如: rust（英文标识）"));
    form->addRow(tr("类别名:"), m_classNameEdit);

    m_classPromptEdit = new QLineEdit(m_page1);
    m_classPromptEdit->setPlaceholderText(tr("如: rust（英文提示词）"));
    form->addRow(tr("提示词:"), m_classPromptEdit);

    m_cnNameEdit = new QLineEdit(m_page1);
    m_cnNameEdit->setPlaceholderText(tr("如: 锈点（中文显示名，可选）"));
    form->addRow(tr("中文名:"), m_cnNameEdit);

    QHBoxLayout* outRow = new QHBoxLayout();
    m_outputDirEdit = new QLineEdit(m_page1);
    m_outputDirEdit->setPlaceholderText(tr("默认 D:/QDV/datasets/<className>"));
    QPushButton* browseBtn = new QPushButton(tr("浏览..."), m_page1);
    connect(browseBtn, &QPushButton::clicked, this, &DataCollectionWizard::onBrowseOutputDir);
    outRow->addWidget(m_outputDirEdit, 1);
    outRow->addWidget(browseBtn);
    form->addRow(tr("数据集输出目录:"), outRow);

    layout->addLayout(form);

    QLabel* hint = new QLabel(tr(
        "<i>说明：数据集将输出为 YOLO 训练格式（images/labels/train/val + data.yaml）。\n"
        "默认存储在 D 盘，符合项目存储规范。</i>"), m_page1);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // 必填字段校验改为在 exportDataset 中手动进行（registerField 是 QWizardPage 的
    // protected 方法，只能在 QWizardPage 子类内部调用，不能从 QWizard 外部调用）
    // 必填项：className / classPrompt / outputDir（见 exportDataset 中的校验）

    addPage(m_page1);
}

void DataCollectionWizard::onBrowseOutputDir() {
    QString current = m_outputDirEdit ? m_outputDirEdit->text() : QString();
    if (current.isEmpty()) current = QStringLiteral("D:/QDV/datasets");
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("选择数据集输出目录"), current);
    if (!dir.isEmpty()) {
        m_outputDirEdit->setText(dir);
    }
}

// ============================================================================
// Step 2: 图像选择
// ============================================================================
void DataCollectionWizard::setupPage2ImageSelect() {
    m_page2 = new QWizardPage(this);
    m_page2->setTitle(tr("步骤 2 / 4：选择图像目录"));
    m_page2->setSubTitle(tr("选择包含待标注图像的目录（支持 png/jpg/jpeg/bmp/tif/tiff/webp）"));

    QVBoxLayout* layout = new QVBoxLayout(m_page2);

    QHBoxLayout* dirRow = new QHBoxLayout();
    m_imageDirEdit = new QLineEdit(m_page2);
    m_imageDirEdit->setPlaceholderText(tr("选择图像所在目录..."));
    m_browseImageBtn = new QPushButton(tr("浏览..."), m_page2);
    connect(m_browseImageBtn, &QPushButton::clicked, this, &DataCollectionWizard::onBrowseImageDir);
    dirRow->addWidget(new QLabel(tr("图像目录:"), m_page2));
    dirRow->addWidget(m_imageDirEdit, 1);
    dirRow->addWidget(m_browseImageBtn);
    layout->addLayout(dirRow);

    QPushButton* refreshBtn = new QPushButton(tr("刷新图像列表"), m_page2);
    connect(refreshBtn, &QPushButton::clicked, this, &DataCollectionWizard::onRefreshImageList);
    layout->addWidget(refreshBtn);

    m_imageCountLabel = new QLabel(tr("已加载 0 张图像"), m_page2);
    layout->addWidget(m_imageCountLabel);

    m_imageListWidget = new QListWidget(m_page2);
    m_imageListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_imageListWidget, 1);

    // 必填校验改为在 exportDataset 中手动进行（registerField 是 protected 方法）
    // 必填项：imageDir 且图像列表非空（见 exportDataset 中的校验）

    addPage(m_page2);
}

void DataCollectionWizard::onBrowseImageDir() {
    QString current = m_imageDirEdit ? m_imageDirEdit->text() : QString();
    if (current.isEmpty()) current = QStringLiteral("D:/QDV/images");
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("选择图像目录"), current);
    if (!dir.isEmpty()) {
        m_imageDirEdit->setText(dir);
        onRefreshImageList();
    }
}

void DataCollectionWizard::onRefreshImageList() {
    if (!m_imageListWidget) return;
    m_imageListWidget->clear();
    m_imagePaths.clear();
    m_annotations.clear();

    QString dir = m_imageDirEdit ? m_imageDirEdit->text().trimmed() : QString();
    if (dir.isEmpty()) return;

    QDir d(dir);
    if (!d.exists()) {
        if (m_imageCountLabel) m_imageCountLabel->setText(tr("目录不存在"));
        return;
    }
    const QStringList filters = {
        "*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tif", "*.tiff", "*.webp"
    };
    const QFileInfoList files = d.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const auto& f : files) {
        m_imagePaths << f.absoluteFilePath();
        m_imageListWidget->addItem(f.fileName());
    }
    if (m_imageCountLabel) {
        m_imageCountLabel->setText(tr("已加载 %1 张图像").arg(m_imagePaths.size()));
    }
}

// ============================================================================
// Step 3: 标注
// ============================================================================
void DataCollectionWizard::setupPage3Annotate() {
    m_page3 = new QWizardPage(this);
    m_page3->setTitle(tr("步骤 3 / 4：矩形框标注"));
    m_page3->setSubTitle(tr("鼠标拖拽画框，每张图可画多个框（classId 固定为 0）"));

    QVBoxLayout* layout = new QVBoxLayout(m_page3);

    // 工具栏：上一张/下一张/删除框/清空当前图/位置标签
    QHBoxLayout* toolbar = new QHBoxLayout();
    m_prevImgBtn = new QPushButton(tr("◀ 上一张"), m_page3);
    m_nextImgBtn = new QPushButton(tr("下一张 ▶"), m_page3);
    m_delBoxBtn  = new QPushButton(tr("删除选中框"), m_page3);
    m_clearBoxesBtn = new QPushButton(tr("清空当前图框"), m_page3);
    m_imgPosLabel = new QLabel(tr("- / -"), m_page3);
    m_annotStatusLabel = new QLabel(tr("提示：在图像上按住鼠标左键拖拽画框"), m_page3);

    toolbar->addWidget(m_prevImgBtn);
    toolbar->addWidget(m_nextImgBtn);
    toolbar->addWidget(m_imgPosLabel);
    toolbar->addStretch();
    toolbar->addWidget(m_delBoxBtn);
    toolbar->addWidget(m_clearBoxesBtn);
    toolbar->addStretch();
    toolbar->addWidget(m_annotStatusLabel);
    layout->addLayout(toolbar);

    // QGraphicsView + Scene
    m_graphicsView = new QGraphicsView(m_page3);
    m_graphicsView->setMinimumHeight(400);
    m_scene = new QGraphicsScene(this);
    m_graphicsView->setScene(m_scene);
    m_graphicsView->setRenderHint(QPainter::Antialiasing);
    layout->addWidget(m_graphicsView, 1);

    // 图像列表（右侧也可放置，这里放下方简洁）
    // 已在 Step 2 提供，此处仅显示当前位置
    connect(m_prevImgBtn, &QPushButton::clicked, this, &DataCollectionWizard::onPrevImage);
    connect(m_nextImgBtn, &QPushButton::clicked, this, &DataCollectionWizard::onNextImage);
    connect(m_delBoxBtn,  &QPushButton::clicked, this, &DataCollectionWizard::onDeleteSelectedBox);
    connect(m_clearBoxesBtn, &QPushButton::clicked, this, &DataCollectionWizard::onClearCurrentImageBoxes);

    // Scene 鼠标事件（用于画框）
    m_scene->installEventFilter(this);  // 通过 eventFilter 处理鼠标事件
    // 注：直接重写 QGraphicsScene 需子类化，这里用 eventFilter 简化

    addPage(m_page3);
}

void DataCollectionWizard::onImageSelectionChanged() {
    int idx = -1;
    if (m_imageListWidget && m_imageListWidget->currentRow() >= 0) {
        idx = m_imageListWidget->currentRow();
    }
    loadCurrentImage(idx);
}

void DataCollectionWizard::loadCurrentImage(int index) {
    if (index < 0 || index >= m_imagePaths.size()) {
        m_currentImageIndex = -1;
        if (m_imgPosLabel) m_imgPosLabel->setText(tr("- / -"));
        return;
    }
    m_currentImageIndex = index;

    // 加载图像
    QPixmap pix(m_imagePaths[index]);
    if (pix.isNull()) {
        if (m_annotStatusLabel) {
            m_annotStatusLabel->setText(tr("无法加载图像: %1").arg(m_imagePaths[index]));
        }
        return;
    }

    m_scene->clear();
    m_pixmapItem = m_scene->addPixmap(pix);
    m_scene->setSceneRect(pix.rect());

    // 自适应视图
    m_graphicsView->fitInView(m_pixmapItem, Qt::KeepAspectRatio);

    // 绘制已有标注框（如果有）
    redrawBoxesForCurrent(index);

    if (m_imgPosLabel) {
        m_imgPosLabel->setText(tr("%1 / %2").arg(index + 1).arg(m_imagePaths.size()));
    }
    if (m_annotStatusLabel) {
        int boxCount = m_annotations.value(m_imagePaths[index]).size();
        m_annotStatusLabel->setText(tr("当前图: %1 个框").arg(boxCount));
    }
}

void DataCollectionWizard::redrawBoxesForCurrent(int index) {
    if (index < 0 || index >= m_imagePaths.size()) return;
    const QList<QRectF>& boxes = m_annotations.value(m_imagePaths[index]);
    for (const QRectF& r : boxes) {
        QGraphicsRectItem* rectItem = m_scene->addRect(r,
            QPen(QColor(255, 80, 80, 220), 2),
            QBrush(QColor(255, 80, 80, 60)));
        rectItem->setFlag(QGraphicsItem::ItemIsSelectable, true);
        rectItem->setData(0, QStringLiteral("annotation_box"));  // 标记为标注框
    }
}

void DataCollectionWizard::onPrevImage() {
    if (m_imagePaths.isEmpty()) return;
    int idx = (m_currentImageIndex <= 0) ? m_currentImageIndex : m_currentImageIndex - 1;
    if (idx != m_currentImageIndex) {
        loadCurrentImage(idx);
    }
}

void DataCollectionWizard::onNextImage() {
    if (m_imagePaths.isEmpty()) return;
    int idx = (m_currentImageIndex >= m_imagePaths.size() - 1)
                  ? m_currentImageIndex
                  : m_currentImageIndex + 1;
    if (idx != m_currentImageIndex) {
        loadCurrentImage(idx);
    }
}

void DataCollectionWizard::onDeleteSelectedBox() {
    if (!m_scene || m_currentImageIndex < 0) return;
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("删除框"), tr("请先在图中点选要删除的标注框"));
        return;
    }
    QString currentPath = m_imagePaths[m_currentImageIndex];
    QList<QRectF>& boxes = m_annotations[currentPath];

    // 从场景中删除选中项，并同步到 m_annotations
    for (QGraphicsItem* item : selected) {
        QGraphicsRectItem* rectItem = qgraphicsitem_cast<QGraphicsRectItem*>(item);
        if (rectItem) {
            QRectF r = rectItem->rect();
            // 在 boxes 中查找匹配（按位置精确匹配）
            for (int i = boxes.size() - 1; i >= 0; --i) {
                if (qAbs(boxes[i].x() - r.x()) < 1.0 &&
                    qAbs(boxes[i].y() - r.y()) < 1.0 &&
                    qAbs(boxes[i].width() - r.width()) < 1.0 &&
                    qAbs(boxes[i].height() - r.height()) < 1.0) {
                    boxes.removeAt(i);
                    break;
                }
            }
            m_scene->removeItem(rectItem);
        }
    }
    if (m_annotStatusLabel) {
        m_annotStatusLabel->setText(tr("当前图: %1 个框").arg(boxes.size()));
    }
}

void DataCollectionWizard::onClearCurrentImageBoxes() {
    if (m_currentImageIndex < 0) return;
    QString currentPath = m_imagePaths[m_currentImageIndex];
    m_annotations.remove(currentPath);
    loadCurrentImage(m_currentImageIndex);  // 重新加载清空框
}

bool DataCollectionWizard::hasAnyAnnotation() const {
    for (auto it = m_annotations.constBegin(); it != m_annotations.constEnd(); ++it) {
        if (!it.value().isEmpty()) return true;
    }
    return false;
}

// ============================================================================
// Step 4: 导出 + 训练参数
// ============================================================================
void DataCollectionWizard::setupPage4Export() {
    m_page4 = new QWizardPage(this);
    m_page4->setTitle(tr("步骤 4 / 4：导出数据集并配置训练参数"));
    m_page4->setSubTitle(tr("导出 YOLO 格式数据集并设置训练超参数，点击 Finish 启动训练"));

    QVBoxLayout* layout = new QVBoxLayout(m_page4);

    QFormLayout* form = new QFormLayout();

    QHBoxLayout* outRow = new QHBoxLayout();
    m_trainOutputDirEdit = new QLineEdit(m_page4);
    m_trainOutputDirEdit->setPlaceholderText(tr("默认 D:/QDV/models/<className>"));
    QPushButton* browseBtn = new QPushButton(tr("浏览..."), m_page4);
    connect(browseBtn, &QPushButton::clicked, this, &DataCollectionWizard::onBrowseTrainOutputDir);
    outRow->addWidget(m_trainOutputDirEdit, 1);
    outRow->addWidget(browseBtn);
    form->addRow(tr("训练输出目录:"), outRow);

    m_modelTypeCombo = new QComboBox(m_page4);
    m_modelTypeCombo->addItems({
        QStringLiteral("yolov8n"),
        QStringLiteral("yolov8s"),
        QStringLiteral("yolov8m"),
        QStringLiteral("yolov5nu"),
        QStringLiteral("yolov5su"),
    });
    m_modelTypeCombo->setToolTip(tr("YOLO 模型类型，n=最小，s/m 依次增大"));
    form->addRow(tr("模型类型:"), m_modelTypeCombo);

    m_epochSpin = new QSpinBox(m_page4);
    m_epochSpin->setRange(1, 500);
    m_epochSpin->setValue(30);
    form->addRow(tr("训练轮数:"), m_epochSpin);

    m_batchSpin = new QSpinBox(m_page4);
    m_batchSpin->setRange(1, 128);
    m_batchSpin->setValue(8);
    form->addRow(tr("批次大小:"), m_batchSpin);

    m_lrSpin = new QDoubleSpinBox(m_page4);
    m_lrSpin->setRange(0.00001, 0.1);
    m_lrSpin->setDecimals(5);
    m_lrSpin->setValue(0.001);
    m_lrSpin->setSingleStep(0.0005);
    form->addRow(tr("学习率:"), m_lrSpin);

    m_valSplitSpin = new QDoubleSpinBox(m_page4);
    m_valSplitSpin->setRange(0.05, 0.5);
    m_valSplitSpin->setDecimals(2);
    m_valSplitSpin->setValue(0.20);
    m_valSplitSpin->setSingleStep(0.05);
    form->addRow(tr("验证集比例:"), m_valSplitSpin);

    layout->addLayout(form);

    m_exportHintLabel = new QLabel(tr(
        "<i>说明：点击 Finish 将\n"
        "  1) 导出 YOLO 数据集到步骤 1 的输出目录\n"
        "  2) 生成训练清单 manifest.json\n"
        "  3) 发射 datasetExported 信号，由外部调用 TrainingBridge 启动训练\n"
        "训练完成后将自动注册算子并标记该类别的专用模型。</i>"), m_page4);
    m_exportHintLabel->setWordWrap(true);
    layout->addWidget(m_exportHintLabel);

    // 必填校验改为在 exportDataset 中手动进行（registerField 是 protected 方法）
    // 必填项：trainOutputDir（见 exportDataset 中的校验）

    addPage(m_page4);
}

void DataCollectionWizard::onBrowseTrainOutputDir() {
    QString current = m_trainOutputDirEdit ? m_trainOutputDirEdit->text() : QString();
    if (current.isEmpty()) current = QStringLiteral("D:/QDV/models");
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("选择训练输出目录"), current);
    if (!dir.isEmpty()) {
        m_trainOutputDirEdit->setText(dir);
    }
}

// ============================================================================
// 导出 YOLO 格式数据集
// ============================================================================
bool DataCollectionWizard::exportDataset(QString* errOut) {
    QString datasetRoot = m_outputDirEdit ? m_outputDirEdit->text().trimmed() : QString();
    if (datasetRoot.isEmpty()) {
        if (errOut) *errOut = tr("数据集输出目录为空");
        return false;
    }

    // 必填字段校验（替代原 registerField 的 * 必填机制）
    QString className = m_classNameEdit ? m_classNameEdit->text().trimmed() : QString();
    if (className.isEmpty()) {
        if (errOut) *errOut = tr("类别名为空");
        return false;
    }
    QString classPrompt = m_classPromptEdit ? m_classPromptEdit->text().trimmed() : className;
    if (classPrompt.isEmpty()) {
        if (errOut) *errOut = tr("提示词为空");
        return false;
    }
    QString trainOutputDir = m_trainOutputDirEdit ? m_trainOutputDirEdit->text().trimmed() : QString();
    if (trainOutputDir.isEmpty()) {
        if (errOut) *errOut = tr("训练输出目录为空");
        return false;
    }

    QDir rootDir(datasetRoot);
    if (!rootDir.mkpath(QStringLiteral("images/train")) ||
        !rootDir.mkpath(QStringLiteral("images/val"))   ||
        !rootDir.mkpath(QStringLiteral("labels/train")) ||
        !rootDir.mkpath(QStringLiteral("labels/val"))) {
        if (errOut) *errOut = tr("无法创建数据集目录结构: %1").arg(datasetRoot);
        return false;
    }

    QString cnName = m_cnNameEdit ? m_cnNameEdit->text().trimmed() : QString();

    // 仅导出有标注的图像
    QList<QPair<QString, QList<QRectF>>> annotatedList;
    for (int i = 0; i < m_imagePaths.size(); ++i) {
        const QString& path = m_imagePaths[i];
        const QList<QRectF>& boxes = m_annotations.value(path);
        if (!boxes.isEmpty()) {
            annotatedList.append(qMakePair(path, boxes));
        }
    }
    if (annotatedList.isEmpty()) {
        if (errOut) *errOut = tr("没有任何图像被标注，无法导出数据集");
        return false;
    }

    // 按 valSplit 比例随机划分 train/val
    int total = annotatedList.size();
    int valCount = qMax(1, static_cast<int>(total * (m_valSplitSpin ? m_valSplitSpin->value() : 0.2)));
    if (valCount >= total) valCount = total / 4;  // 防止 val 占比过高
    if (valCount < 1) valCount = 1;

    // 生成随机索引
    QList<int> indices;
    for (int i = 0; i < total; ++i) indices << i;
    QRandomGenerator gen(QDateTime::currentDateTime().toMSecsSinceEpoch());
    std::shuffle(indices.begin(), indices.end(), gen);

    QSet<int> valSet;
    for (int i = 0; i < valCount; ++i) valSet.insert(indices[i]);

    // 复制图像并写 labels
    // 同时记录每张图的导出后路径（用于 manifest 的 imageLabelPairs）
    struct ExportedItem {
        QString originalPath;
        QString destImgPath;
        QString destLabelPath;
        bool isVal;
    };
    QList<ExportedItem> exportedItems;
    int trainIdx = 0, valIdx = 0;
    for (int i = 0; i < total; ++i) {
        const QString& imgPath = annotatedList[i].first;
        const QList<QRectF>& boxes = annotatedList[i].second;
        bool isVal = valSet.contains(i);

        QString subdir = isVal ? QStringLiteral("val") : QStringLiteral("train");
        QString baseName = QFileInfo(imgPath).completeBaseName();
        // 重命名避免冲突：train_0001_xxx.jpg / val_0001_xxx.jpg
        int seqNum = isVal ? ++valIdx : ++trainIdx;
        QString newName = QStringLiteral("%1_%2_%3")
                              .arg(subdir)
                              .arg(seqNum, 4, 10, QChar('0'))
                              .arg(baseName);
        QString imgExt = QFileInfo(imgPath).suffix();
        QString newImgName = newName + "." + imgExt;
        QString newLabelName = newName + ".txt";

        QString destImgPath = rootDir.absoluteFilePath(QStringLiteral("images/%1/%2").arg(subdir, newImgName));
        QString destLabelPath = rootDir.absoluteFilePath(QStringLiteral("labels/%1/%2").arg(subdir, newLabelName));

        // 复制图像（QFile::copy 不覆盖，先删后拷）
        if (QFile::exists(destImgPath)) QFile::remove(destImgPath);
        if (!QFile::copy(imgPath, destImgPath)) {
            if (errOut) *errOut = tr("无法复制图像 %1 到 %2").arg(imgPath).arg(destImgPath);
            return false;
        }

        // 加载图像获取尺寸（用于归一化坐标）
        QPixmap pix(imgPath);
        if (pix.isNull()) {
            if (errOut) *errOut = tr("无法加载图像以获取尺寸: %1").arg(imgPath);
            return false;
        }
        double imgW = pix.width();
        double imgH = pix.height();
        if (imgW < 1 || imgH < 1) {
            if (errOut) *errOut = tr("图像尺寸无效: %1").arg(imgPath);
            return false;
        }

        // 写 YOLO 标签（每行: classId cx cy w h，归一化 [0,1]）
        QFile labelFile(destLabelPath);
        if (!labelFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            if (errOut) *errOut = tr("无法写入标签文件: %1").arg(destLabelPath);
            return false;
        }
        QTextStream ts(&labelFile);
        ts.setEncoding(QStringConverter::Utf8);
        for (const QRectF& r : boxes) {
            double cx = (r.x() + r.width() / 2.0) / imgW;
            double cy = (r.y() + r.height() / 2.0) / imgH;
            double w  = r.width()  / imgW;
            double h  = r.height() / imgH;
            // 限制 [0,1]
            cx = qBound(0.0, cx, 1.0);
            cy = qBound(0.0, cy, 1.0);
            w  = qBound(0.0, w, 1.0);
            h  = qBound(0.0, h, 1.0);
            ts << QStringLiteral("0 %1 %2 %3 %4\n")
                  .arg(cx, 0, 'f', 6).arg(cy, 0, 'f', 6)
                  .arg(w, 0, 'f', 6).arg(h, 0, 'f', 6);
        }
        labelFile.close();

        exportedItems.append({imgPath, destImgPath, destLabelPath, isVal});
    }

    // 写 data.yaml
    QString yamlPath = rootDir.absoluteFilePath(QStringLiteral("data.yaml"));
    QFile yamlFile(yamlPath);
    if (!yamlFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errOut) *errOut = tr("无法写入 data.yaml: %1").arg(yamlPath);
        return false;
    }
    QTextStream yamlTs(&yamlFile);
    yamlTs.setEncoding(QStringConverter::Utf8);
    yamlTs << QStringLiteral("path: %1\n").arg(QDir::toNativeSeparators(datasetRoot));
    yamlTs << QStringLiteral("train: images/train\n");
    yamlTs << QStringLiteral("val: images/val\n");
    yamlTs << QStringLiteral("nc: 1\n");
    yamlTs << QStringLiteral("names:\n");
    yamlTs << QStringLiteral("  0: %1\n").arg(className.isEmpty() ? classPrompt : className);
    yamlFile.close();

    // 写 manifest.json（供 TrainingBridge::startTraining 使用）
    // manifest 包含 imageLabelPairs 与 allLabels
    QJsonObject manifestObj;
    manifestObj["datasetRoot"] = datasetRoot;
    manifestObj["className"]   = className;
    manifestObj["classPrompt"] = classPrompt;
    manifestObj["cnName"]      = cnName;
    manifestObj["trainCount"]  = trainIdx;
    manifestObj["valCount"]    = valIdx;
    manifestObj["valSplit"]    = m_valSplitSpin ? m_valSplitSpin->value() : 0.2;

    QJsonArray labelsArr;
    labelsArr.append(className.isEmpty() ? classPrompt : className);
    manifestObj["allLabels"] = labelsArr;

    // imageLabelPairs：每张图的标注数据（含图像路径与对应的归一化标签）
    QJsonArray pairsArr;
    for (const ExportedItem& item : exportedItems) {
        QJsonObject pair;
        pair["imagePath"] = item.destImgPath;  // 使用导出后路径（训练时直接读取）
        pair["labelPath"] = item.destLabelPath;
        pair["isVal"] = item.isVal;
        pairsArr.append(pair);
    }
    manifestObj["imageLabelPairs"] = pairsArr;

    QString manifestPath = rootDir.absoluteFilePath(QStringLiteral("manifest.json"));
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errOut) *errOut = tr("无法写入 manifest.json: %1").arg(manifestPath);
        return false;
    }
    manifestFile.write(QJsonDocument(manifestObj).toJson(QJsonDocument::Indented));
    manifestFile.close();

    // 填充训练参数
    m_trainParams.datasetManifestPath = manifestPath;
    m_trainParams.datasetRootDir      = datasetRoot;
    m_trainParams.outputDir           = m_trainOutputDirEdit ? m_trainOutputDirEdit->text().trimmed() : QString();
    m_trainParams.modelType           = m_modelTypeCombo ? m_modelTypeCombo->currentText() : QStringLiteral("yolov8n");
    m_trainParams.numEpochs           = m_epochSpin ? m_epochSpin->value() : 30;
    m_trainParams.batchSize           = m_batchSpin ? m_batchSpin->value() : 8;
    m_trainParams.learningRate        = m_lrSpin ? m_lrSpin->value() : 0.001;
    m_trainParams.valSplit            = m_valSplitSpin ? m_valSplitSpin->value() : 0.2;
    m_trainParams.className           = className.isEmpty() ? classPrompt : className;
    m_trainParams.allLabels           = QStringList{m_trainParams.className};

    return true;
}

// ============================================================================
// eventFilter：拦截 QGraphicsScene 鼠标事件实现拖拽画框
// ============================================================================
bool DataCollectionWizard::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_scene) {
        if (event->type() == QEvent::GraphicsSceneMousePress) {
            auto* me = static_cast<QGraphicsSceneMouseEvent*>(event);
            // 仅左键开始画框
            if (me->button() == Qt::LeftButton && m_currentImageIndex >= 0) {
                m_drawStartPos = me->scenePos();
                // 创建临时矩形项（绿色半透明）
                m_currentDrawingRect = m_scene->addRect(
                    QRectF(m_drawStartPos, QSizeF(0, 0)),
                    QPen(QColor(80, 255, 80, 220), 2),
                    QBrush(QColor(80, 255, 80, 60)));
                m_currentDrawingRect->setData(0, QStringLiteral("drawing_box"));
                return true;  // 事件已处理
            }
        } else if (event->type() == QEvent::GraphicsSceneMouseMove) {
            auto* me = static_cast<QGraphicsSceneMouseEvent*>(event);
            if (m_currentDrawingRect) {
                // 实时调整矩形大小
                QRectF newRect(m_drawStartPos, me->scenePos());
                newRect = newRect.normalized();  // 处理反向拖拽
                m_currentDrawingRect->setRect(newRect);
                return true;
            }
        } else if (event->type() == QEvent::GraphicsSceneMouseRelease) {
            auto* me = static_cast<QGraphicsSceneMouseEvent*>(event);
            if (m_currentDrawingRect && m_currentImageIndex >= 0) {
                QRectF finalRect(m_drawStartPos, me->scenePos());
                finalRect = finalRect.normalized();
                // 移除临时绘制项
                m_scene->removeItem(m_currentDrawingRect);
                m_currentDrawingRect = nullptr;

                // 过滤过小的框（误点击）
                if (finalRect.width() < 5 || finalRect.height() < 5) {
                    return true;
                }

                // 限制在图像范围内
                if (m_pixmapItem) {
                    finalRect = finalRect.intersected(m_pixmapItem->boundingRect());
                    if (finalRect.width() < 5 || finalRect.height() < 5) {
                        return true;  // 框完全在图像外
                    }
                }

                // 保存到 m_annotations
                QString currentPath = m_imagePaths[m_currentImageIndex];
                m_annotations[currentPath].append(finalRect);

                // 重新绘制当前图（统一渲染样式）
                loadCurrentImage(m_currentImageIndex);
                return true;
            }
        }
    }
    return QWizard::eventFilter(obj, event);
}

} // namespace QDV
