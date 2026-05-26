#include "TrainingInference/CategoryPanel.h"
#include "TrainingInference/CategoryManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QLabel>



CategoryPanel::CategoryPanel(QWidget* parent) : QWidget(parent) {
    setupUI();
    connect(CategoryManager::instance(), &CategoryManager::categoryCreated, this, [this]() { refreshTree(); });
    connect(CategoryManager::instance(), &CategoryManager::categoryUpdated, this, [this]() { refreshTree(); });
    connect(CategoryManager::instance(), &CategoryManager::categoryDeleted, this, [this]() { refreshTree(); });
}

void CategoryPanel::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    QLabel* titleLabel = new QLabel("类别管理");
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e0e0e0; background: transparent;");
    layout->addWidget(titleLabel);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("搜索类别...");
    m_searchEdit->setStyleSheet(R"(
        QLineEdit {
            background-color: #2d2d2d;
            border: 1px solid #555;
            border-radius: 4px;
            padding: 6px 10px;
            color: #e0e0e0;
            font-size: 13px;
        }
        QLineEdit:focus { border-color: #660874; }
    )");
    connect(m_searchEdit, &QLineEdit::textChanged, this, &CategoryPanel::onSearchTextChanged);
    layout->addWidget(m_searchEdit);

    m_tree = new QTreeWidget();
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setIndentation(20);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setStyleSheet(R"(
        QTreeWidget {
            background-color: #252525;
            border: 1px solid #444;
            border-radius: 4px;
            color: #e0e0e0;
        }
        QTreeWidget::item { padding: 4px 6px; }
        QTreeWidget::item:selected { background-color: #660874; color: #fff; }
        QTreeWidget::item:hover { background-color: #333; }
    )");
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &CategoryPanel::onTreeContextMenu);
    connect(m_tree, &QTreeWidget::itemClicked, this, &CategoryPanel::onItemClicked);
    layout->addWidget(m_tree, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(6);

    QString btnStyle = R"(
        QPushButton {
            background-color: #3d3d3d;
            color: #e0e0e0;
            border: 1px solid #555;
            border-radius: 4px;
            padding: 6px 14px;
            font-size: 12px;
        }
        QPushButton:hover { background-color: #555; }
        QPushButton:disabled { background-color: #2a2a2a; color: #666; }
    )";

    m_addBtn = new QPushButton("添加");
    m_addBtn->setStyleSheet(btnStyle);
    connect(m_addBtn, &QPushButton::clicked, this, &CategoryPanel::onAddCategory);

    m_editBtn = new QPushButton("编辑");
    m_editBtn->setStyleSheet(btnStyle);
    m_editBtn->setEnabled(false);
    connect(m_editBtn, &QPushButton::clicked, this, &CategoryPanel::onEditCategory);

    m_deleteBtn = new QPushButton("删除");
    m_deleteBtn->setStyleSheet(btnStyle);
    m_deleteBtn->setEnabled(false);
    connect(m_deleteBtn, &QPushButton::clicked, this, &CategoryPanel::onDeleteCategory);

    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_editBtn);
    btnLayout->addWidget(m_deleteBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    m_contextMenu = new QMenu(this);
    m_contextMenu->setStyleSheet(R"(
        QMenu { background-color: #3d3d3d; color: #ddd; border: 1px solid #555; padding: 4px 0; }
        QMenu::item { padding: 6px 24px; }
        QMenu::item:selected { background-color: #660874; }
    )");
    m_contextMenu->addAction("添加子类别", this, &CategoryPanel::onAddCategory);
    m_contextMenu->addAction("编辑", this, &CategoryPanel::onEditCategory);
    m_contextMenu->addAction("删除", this, &CategoryPanel::onDeleteCategory);

    refreshTree();
}

void CategoryPanel::refreshTree(const QString& filter) {
    m_tree->clear();
    auto cats = filter.isEmpty() ? CategoryManager::instance()->allCategories()
                                 : CategoryManager::instance()->searchCategories(filter);

    auto addItem = [this](QTreeWidgetItem* parent, const CategoryNode& node, auto&& self) -> void {
        QTreeWidgetItem* item = createCategoryItem(node.name, node.id, parent);
        for (const CategoryNode& child : node.children) {
            self(item, child, self);
        }
    };

    for (const CategoryNode& node : cats) {
        addItem(nullptr, node, addItem);
    }

    m_tree->expandAll();
}

QTreeWidgetItem* CategoryPanel::createCategoryItem(const QString& name, const QString& id, QTreeWidgetItem* parent) {
    QTreeWidgetItem* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    item->setText(0, name);
    item->setData(0, Qt::UserRole, id);
    return item;
}

void CategoryPanel::onAddCategory() {
    bool ok;
    QString name = QInputDialog::getText(this, "添加类别", "类别名称:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QString parentId;
    QTreeWidgetItem* current = m_tree->currentItem();
    if (current) {
        parentId = current->data(0, Qt::UserRole).toString();
    }

    CategoryManager::instance()->createCategory(name.trimmed(), parentId);
}

void CategoryPanel::onEditCategory() {
    QTreeWidgetItem* current = m_tree->currentItem();
    if (!current) return;

    QString id = current->data(0, Qt::UserRole).toString();
    bool ok;
    QString name = QInputDialog::getText(this, "编辑类别", "新名称:",
                                         QLineEdit::Normal, current->text(0), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    CategoryManager::instance()->updateCategory(id, name.trimmed());
}

void CategoryPanel::onDeleteCategory() {
    QTreeWidgetItem* current = m_tree->currentItem();
    if (!current) return;

    QString id = current->data(0, Qt::UserRole).toString();
    auto result = QMessageBox::question(this, "确认删除",
        QString("确定要删除类别 \"%1\" 及其子类别吗？").arg(current->text(0)));
    if (result == QMessageBox::Yes) {
        CategoryManager::instance()->deleteCategory(id);
    }
}

void CategoryPanel::onSearchTextChanged(const QString& text) {
    refreshTree(text);
}

void CategoryPanel::onTreeContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (item) {
        m_tree->setCurrentItem(item);
        m_contextMenu->exec(m_tree->viewport()->mapToGlobal(pos));
    }
}

void CategoryPanel::onItemClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    bool hasSelection = item != nullptr;
    m_editBtn->setEnabled(hasSelection);
    m_deleteBtn->setEnabled(hasSelection);

    if (item) {
        QString id = item->data(0, Qt::UserRole).toString();
        emit categorySelected(id, item->text(0));
    }
}

