#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>

struct CategoryNode {
    QString   id;
    QString   name;
    QString   parentId;
    QList<CategoryNode> children;
    QStringList sampleImages;
};

class CategoryManager : public QObject {
    Q_OBJECT

public:
    static CategoryManager* instance();

    QString createCategory(const QString& name, const QString& parentId = QString());
    bool updateCategory(const QString& id, const QString& newName);
    bool deleteCategory(const QString& id);
    QList<CategoryNode> allCategories() const;
    QList<CategoryNode> searchCategories(const QString& keyword) const;
    CategoryNode categoryById(const QString& id) const;
    int categoryCount() const;
    bool saveToFile(const QString& filePath);
    bool loadFromFile(const QString& filePath);

signals:
    void categoryCreated(const QString& id, const QString& name);
    void categoryUpdated(const QString& id, const QString& newName);
    void categoryDeleted(const QString& id);

private:
    CategoryManager(QObject* parent = nullptr);
    QList<CategoryNode> flattenTree(const QList<CategoryNode>& nodes) const;
    CategoryNode* findNode(const QString& id, QList<CategoryNode>& nodes);
    QMap<QString, CategoryNode> m_categories;
    int m_nextId = 1;
    static CategoryManager* s_instance;
};