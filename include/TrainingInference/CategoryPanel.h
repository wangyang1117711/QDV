#ifndef CATEGORY_PANEL_H
#define CATEGORY_PANEL_H

#include "TrainingInference/CategoryManager.h"
#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QMenu>
#include <QHBoxLayout>
#include <QLabel>



class CategoryPanel : public QWidget {
    Q_OBJECT

public:
    explicit CategoryPanel(QWidget* parent = nullptr);

signals:
    void categorySelected(const QString& categoryId, const QString& categoryName);
    void addToCategoryRequested(const QString& categoryId, const QString& categoryName);

public slots:
    void updateAddToCategoryButtons(bool hasSelection);
    // 刷新类别树视图（加载项目后需手动调用，因 QSignalBlocker 会阻塞自动刷新）
    void refreshTree(const QString& filter = QString());

private slots:
    void onAddCategory();
    void onEditCategory();
    void onDeleteCategory();
    void onSearchTextChanged(const QString& text);
    void onTreeContextMenu(const QPoint& pos);
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onAddToCategoryClicked();

private:
    void setupUI();
    QTreeWidgetItem* createCategoryItem(const QString& name, const QString& id, int annotatedCount, QTreeWidgetItem* parent = nullptr);
    void updateButtonForItem(QTreeWidgetItem* item, bool hasSelection);
    int countAnnotatedImages(const CategoryNode& node) const;
    void deleteCategoryItemWidgets(QTreeWidgetItem* item);

    QTreeWidget* m_tree;
    QLineEdit* m_searchEdit;
    QPushButton* m_addBtn;
    QPushButton* m_editBtn;
    QPushButton* m_deleteBtn;
    QMenu* m_contextMenu;
};


#endif