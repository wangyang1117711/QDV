#ifndef CATEGORY_PANEL_H
#define CATEGORY_PANEL_H

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
    void refreshTree(const QString& filter = QString());
    QTreeWidgetItem* createCategoryItem(const QString& name, const QString& id, QTreeWidgetItem* parent = nullptr);
    void updateButtonForItem(QTreeWidgetItem* item, bool hasSelection);

    QTreeWidget* m_tree;
    QLineEdit* m_searchEdit;
    QPushButton* m_addBtn;
    QPushButton* m_editBtn;
    QPushButton* m_deleteBtn;
    QMenu* m_contextMenu;
};


#endif