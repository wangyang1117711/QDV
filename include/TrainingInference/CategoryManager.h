#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

struct CategoryNode {
    QString   id;
    QString   name;
    QString   parentId;
    QString   color       = "#CCCCCC";
    QString   description;
    QList<CategoryNode> children;
    QStringList sampleImages;
    bool      isPublic    = true;
    int       depth       = 0;
    QDateTime createdAt;
    QDateTime modifiedAt;
};

struct ImportResult {
    int     total       = 0;
    int     imported    = 0;
    int     skipped     = 0;
    QStringList errors;
};

class CategoryManager : public QObject {
    Q_OBJECT

public:
    static CategoryManager* instance();

    QString createCategory(const QString& name,
                           const QString& parentId = QString(),
                           const QString& color = "#CCCCCC",
                           const QString& description = QString(),
                           const QStringList& sampleImages = QStringList(),
                           bool isPublic = true);
    bool updateCategory(const QString& id,
                        const QString& newName,
                        const QString& newColor = QString(),
                        const QString& newDescription = QString(),
                        bool newIsPublic = true);
    bool deleteCategory(const QString& id);
    QList<CategoryNode> allCategories() const;
    QList<CategoryNode> searchCategories(const QString& keyword) const;
    CategoryNode categoryById(const QString& id) const;
    int categoryCount() const;
    int categoryCountByParent(const QString& parentId) const;

    bool hasSiblingWithName(const QString& name, const QString& parentId, const QString& excludeId = QString()) const;

    bool saveToFile(const QString& filePath);
    bool loadFromFile(const QString& filePath);

    ImportResult importFromJSON(const QString& filePath);
    ImportResult importFromCSV(const QString& filePath);
    bool exportToJSON(const QString& filePath);
    // 导出类别树为 QJsonObject（用于项目保存，复用 exportToJSON 逻辑）
    QJsonObject exportToJsonObject() const;
    // 从 QJsonObject 导入类别（用于项目加载，复用 importFromJSON 逻辑）
    ImportResult importFromJsonObject(const QJsonObject& root);
    // 清空所有类别（用于项目加载前清空）
    void clearCategories();
    bool exportToCSV(const QString& filePath);

    int loadPresetCategories(const QString& jsonFilePath);
    bool isAccessible(const QString& categoryId) const;

    static QString validateCategoryName(const QString& name, const QString& parentId = QString());
    static QString validateCategoryColor(const QString& color);
    static QString calculateNodeId(int nextId);

    int maxDepth() const { return 2; }

signals:
    void categoryCreated(const QString& id, const QString& name);
    void categoryUpdated(const QString& id, const QString& newName);
    void categoryDeleted(const QString& id);

private:
    CategoryManager(QObject* parent = nullptr);
    QList<CategoryNode> flattenTree(const QList<CategoryNode>& nodes) const;
    CategoryNode* findNode(const QString& id, QList<CategoryNode>& nodes);
    const CategoryNode* findNode(const QString& id, const QList<CategoryNode>& nodes) const;
    int calculateDepth(const QString& id) const;
    void rebuildChildren(QList<CategoryNode>& roots, const QList<CategoryNode>& all) const;
    QList<CategoryNode> buildTree() const;

    QMap<QString, CategoryNode> m_categories;
    int m_nextId = 1;
    static CategoryManager* s_instance;
};