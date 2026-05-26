#include "EditView.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QSplitter>
#include <QLabel>
#include <QScrollArea>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QGroupBox>
#include <QUndoStack>
#include <QUndoCommand>
#include <QInputDialog>
#include <QMessageBox>

class AddToolCommand : public QUndoCommand {
public:
    AddToolCommand(QTreeWidget* tree, const QStringList& toolData, const QString& text)
        : m_tree(tree), m_toolData(toolData), m_text(text) {
        setText("添加工具: " + text);
    }
    void undo() override {
        for (int i = m_tree->topLevelItemCount() - 1; i >= 0; --i) {
            if (m_tree->topLevelItem(i)->text(0) == m_text) {
                delete m_tree->takeTopLevelItem(i);
                break;
            }
        }
    }
    void redo() override {
        QTreeWidgetItem* item = new QTreeWidgetItem();
        item->setText(0, m_text);
        for (int i = 0; i < m_toolData.size() && i < 2; ++i) {
            item->setText(i + 1, m_toolData[i]);
        }
        m_tree->addTopLevelItem(item);
    }
private:
    QTreeWidget* m_tree;
    QStringList m_toolData;
    QString m_text;
};

EditView::EditView(QWidget* parent) : QWidget(parent) {
    m_undoStack = new QUndoStack(this);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QToolBar* toolbar = new QToolBar();
    toolbar->setStyleSheet(R"(
        QToolBar {
            background-color: #2d2d2d;
            border-bottom: 1px solid #444;
            padding: 4px 8px;
            spacing: 6px;
        }
        QToolBar QToolButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 6px 12px;
            color: #e0e0e0;
            font-size: 13px;
        }
        QToolBar QToolButton:hover {
            background-color: #555;
            border-color: #555;
        }
        QToolBar QToolButton:disabled {
            color: #666;
        }
    )");

    m_undoAction = toolbar->addAction("撤销");
    m_undoAction->setEnabled(false);
    m_redoAction = toolbar->addAction("重做");
    m_redoAction->setEnabled(false);
    toolbar->addSeparator();
    QAction* runAction = toolbar->addAction("运行检测");
    toolbar->addSeparator();
    m_deleteAction = toolbar->addAction("删除工具");

    mainLayout->addWidget(toolbar);

    m_mainSplitter = new QSplitter(Qt::Horizontal);

    setupToolBox(m_mainSplitter);
    setupCanvas(m_mainSplitter);
    setupPropertyPanel(m_mainSplitter);

    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 2);
    m_mainSplitter->setStretchFactor(2, 1);
    m_mainSplitter->setSizes({250, 500, 250});

    mainLayout->addWidget(m_mainSplitter);

    connect(m_undoAction, &QAction::triggered, this, &EditView::onUndo);
    connect(m_redoAction, &QAction::triggered, this, &EditView::onRedo);
    connect(m_deleteAction, &QAction::triggered, this, &EditView::onDeleteTool);
    connect(runAction, &QAction::triggered, this, &EditView::requestRunDetection);

    connect(m_toolBoxTree, &QTreeWidget::itemDoubleClicked, this, &EditView::onToolDoubleClicked);
    connect(m_toolChainTree, &QTreeWidget::itemSelectionChanged, this, &EditView::onToolChainSelectionChanged);

    connect(m_undoStack, &QUndoStack::canUndoChanged, m_undoAction, &QAction::setEnabled);
    connect(m_undoStack, &QUndoStack::canRedoChanged, m_redoAction, &QAction::setEnabled);
}

EditView::~EditView() = default;

void EditView::setupToolBox(QWidget* parent) {
    QWidget* toolBoxPanel = new QWidget();
    QVBoxLayout* toolboxLayout = new QVBoxLayout(toolBoxPanel);
    toolboxLayout->setContentsMargins(0, 0, 0, 0);
    toolboxLayout->setSpacing(0);

    QLabel* toolboxTitle = new QLabel("  工具库");
    toolboxTitle->setFixedHeight(32);
    toolboxTitle->setStyleSheet("background-color: #333; color: #e0e0e0; font-weight: bold; font-size: 13px; padding: 4px 8px;");

    m_toolBoxTree = new QTreeWidget();
    m_toolBoxTree->setHeaderLabels({"工具名称", "类型"});
    m_toolBoxTree->setColumnWidth(0, 150);
    m_toolBoxTree->setHeaderHidden(false);
    m_toolBoxTree->setDragEnabled(true);

    struct ToolEntry {
        QString name;
        QString category;
    };

    QList<ToolEntry> tools = {
        {"图像预处理", "预处理"},
        {"边缘检测", "特征提取"},
        {"轮廓分析", "特征提取"},
        {"阈值分割", "分割"},
        {"模板匹配", "定位"},
        {"斑点检测", "特征提取"},
        {"颜色检测", "检测"},
        {"几何测量", "测量"},
        {"线圆检测", "特征提取"},
        {"图像运算", "运算"},
        {"图像变换", "变换"},
        {"图像合并", "合并"},
        {"分支控制", "流程"},
    };

    QMap<QString, QTreeWidgetItem*> categories;
    for (const auto& tool : tools) {
        if (!categories.contains(tool.category)) {
            QTreeWidgetItem* catItem = new QTreeWidgetItem();
            catItem->setText(0, tool.category);
            catItem->setFlags(catItem->flags() & ~Qt::ItemIsDragEnabled);
            QFont font = catItem->font(0);
            font.setBold(true);
            catItem->setFont(0, font);
            m_toolBoxTree->addTopLevelItem(catItem);
            categories[tool.category] = catItem;
        }

        QTreeWidgetItem* toolItem = new QTreeWidgetItem();
        toolItem->setText(0, tool.name);
        toolItem->setText(1, tool.category);
        categories[tool.category]->addChild(toolItem);
    }

    m_toolBoxTree->expandAll();

    QGroupBox* chainGroup = new QGroupBox("工具链");
    QVBoxLayout* chainLayout = new QVBoxLayout(chainGroup);
    chainLayout->setContentsMargins(4, 12, 4, 4);

    m_toolChainTree = new QTreeWidget();
    m_toolChainTree->setHeaderLabels({"序号", "工具"});
    m_toolChainTree->setColumnWidth(0, 50);
    m_toolChainTree->setAcceptDrops(true);
    m_toolChainTree->setDragDropMode(QAbstractItemView::InternalMove);

    chainLayout->addWidget(m_toolChainTree);

    toolboxLayout->addWidget(toolboxTitle);
    toolboxLayout->addWidget(m_toolBoxTree);
    toolboxLayout->addWidget(chainGroup);

    QSplitter* splitter = qobject_cast<QSplitter*>(parent);
    if (splitter) {
        splitter->addWidget(toolBoxPanel);
    }
}

void EditView::setupCanvas(QWidget* parent) {
    m_canvasArea = new QScrollArea();
    m_canvasArea->setStyleSheet("background-color: #1e1e1e; border: 1px solid #444;");
    m_canvasArea->setWidgetResizable(true);

    m_canvasLabel = new QLabel();
    m_canvasLabel->setAlignment(Qt::AlignCenter);
    m_canvasLabel->setMinimumSize(400, 300);
    m_canvasLabel->setStyleSheet("color: #777; font-size: 14px; background: transparent; border: none;");
    m_canvasLabel->setText("画布区域\n请从工具库双击添加工具到工具链");

    m_canvasArea->setWidget(m_canvasLabel);

    QSplitter* splitter = qobject_cast<QSplitter*>(parent);
    if (splitter) {
        splitter->addWidget(m_canvasArea);
    }
}

void EditView::setupPropertyPanel(QWidget* parent) {
    m_propertyArea = new QScrollArea();
    m_propertyArea->setWidgetResizable(true);
    m_propertyArea->setStyleSheet("background-color: #252525; border: none;");

    m_propertyPanel = new QWidget();
    m_propertyLayout = new QFormLayout(m_propertyPanel);
    m_propertyLayout->setContentsMargins(12, 12, 12, 12);
    m_propertyLayout->setSpacing(8);

    QLabel* propTitle = new QLabel("属性面板");
    propTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #e0e0e0; padding: 4px 0;");
    m_propertyLayout->addRow(propTitle);

    m_propertyLayout->addRow(new QLabel("选择工具链中的工具以查看属性"));
    m_propertyPanel->setLayout(m_propertyLayout);

    m_propertyArea->setWidget(m_propertyPanel);

    QSplitter* splitter = qobject_cast<QSplitter*>(parent);
    if (splitter) {
        splitter->addWidget(m_propertyArea);
    }
}

void EditView::onToolDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (!item || item->parent() == nullptr) return;

    QString toolName = item->text(0);
    QString toolType = item->text(1);

    int seq = m_toolChainTree->topLevelItemCount() + 1;
    QTreeWidgetItem* chainItem = new QTreeWidgetItem();
    chainItem->setText(0, QString::number(seq));
    chainItem->setText(1, toolName);
    chainItem->setData(0, Qt::UserRole, toolType);
    m_toolChainTree->addTopLevelItem(chainItem);

    QStringList toolData = {toolName, toolType};
    m_undoStack->push(new AddToolCommand(m_toolChainTree, toolData, toolName));

    emit toolCountChanged(m_toolChainTree->topLevelItemCount());
    emit schemeModified();
}

void EditView::onUndo() {
    m_undoStack->undo();
    emit schemeModified();
}

void EditView::onRedo() {
    m_undoStack->redo();
    emit schemeModified();
}

void EditView::onDeleteTool() {
    QTreeWidgetItem* selected = m_toolChainTree->currentItem();
    if (!selected) return;

    delete selected;

    for (int i = 0; i < m_toolChainTree->topLevelItemCount(); ++i) {
        m_toolChainTree->topLevelItem(i)->setText(0, QString::number(i + 1));
    }

    emit toolCountChanged(m_toolChainTree->topLevelItemCount());
    emit schemeModified();
}

void EditView::onToolChainSelectionChanged() {
    QTreeWidgetItem* selected = m_toolChainTree->currentItem();
    if (!selected) return;

    QString toolName = selected->text(1);
    updatePropertyPanelForTool(toolName);
}

void EditView::updatePropertyPanelForTool(const QString& toolType) {
    QLayoutItem* item;
    while ((item = m_propertyLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    QLabel* propTitle = new QLabel("属性 - " + toolType);
    propTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #e0e0e0; padding: 4px 0;");
    m_propertyLayout->addRow(propTitle);

    QLabel* nameLabel = new QLabel("名称:");
    QLineEdit* nameEdit = new QLineEdit(toolType);
    m_propertyLayout->addRow(nameLabel, nameEdit);

    QLabel* thresholdLabel = new QLabel("阈值:");
    QSpinBox* thresholdSpin = new QSpinBox();
    thresholdSpin->setRange(0, 255);
    thresholdSpin->setValue(128);
    m_propertyLayout->addRow(thresholdLabel, thresholdSpin);

    QLabel* scaleLabel = new QLabel("缩放比例:");
    QDoubleSpinBox* scaleSpin = new QDoubleSpinBox();
    scaleSpin->setRange(0.1, 10.0);
    scaleSpin->setValue(1.0);
    scaleSpin->setSingleStep(0.1);
    m_propertyLayout->addRow(scaleLabel, scaleSpin);

    QLabel* modeLabel = new QLabel("模式:");
    QComboBox* modeCombo = new QComboBox();
    modeCombo->addItems({"标准", "快速", "高精度"});
    m_propertyLayout->addRow(modeLabel, modeCombo);

    QLabel* roiLabel = new QLabel("ROI区域:");
    QLineEdit* roiEdit = new QLineEdit("全图");
    m_propertyLayout->addRow(roiLabel, roiEdit);

    QPushButton* applyBtn = new QPushButton("应用");
    applyBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #660874;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 8px 16px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #7d1a8f;
        }
    )");
    m_propertyLayout->addRow(applyBtn);

    connect(applyBtn, &QPushButton::clicked, [this]() {
        emit schemeModified();
    });
}