#ifndef CATEGORY_PANEL_H
#define CATEGORY_PANEL_H

#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QMenu>



class CategoryPanel : public QWidget {
    Q_OBJECT

public:
    explicit CategoryPanel(QWidget* parent = nullptr);

signals:
    void categorySelected(const QString& categoryId, const QString& categoryName);

private slots:
    void onAddCategory();
    void onEditCategory();
    void onDeleteCategory();
    void onSearchTextChanged(const QString& text);
    void onTreeContextMenu(const QPoint& pos);
    void onItemClicked(QTreeWidgetItem* item, int column);

private:
    void setupUI();
    void refreshTree(const QString& filter = QString());
    QTreeWidgetItem* createCategoryItem(const QString& name, const QString& id, QTreeWidgetItem* parent = nullptr);

    QTreeWidget* m_tree;
    QLineEdit* m_searchEdit;
    QPushButton* m_addBtn;
    QPushButton* m_editBtn;
    QPushButton* m_deleteBtn;
    QMenu* m_contextMenu;
};



#endif