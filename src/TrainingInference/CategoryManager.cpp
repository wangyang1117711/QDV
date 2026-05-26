#include "TrainingInference/CategoryManager.h"
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>



CategoryManager* CategoryManager::s_instance = nullptr;

CategoryManager* CategoryManager::instance() {
    if (!s_instance) {
        s_instance = new CategoryManager();
    }
    return s_instance;
}

CategoryManager::CategoryManager(QObject* parent) : QObject(parent) {
}

QString CategoryManager::createCategory(const QString& name, const QString& parentId) {
    QString id = QString("cat_%1_%2").arg(m_nextId++).arg(QUuid::createUuid().toString(QUuid::Id128).left(8));

    CategoryNode node;
    node.id = id;
    node.name = name;
    node.parentId = parentId;

    m_categories[id] = node;
    emit categoryCreated(id, name);
    return id;
}

bool CategoryManager::updateCategory(const QString& id, const QString& newName) {
    if (!m_categories.contains(id)) return false;
    m_categories[id].name = newName;
    emit categoryUpdated(id, newName);
    return true;
}

bool CategoryManager::deleteCategory(const QString& id) {
    if (!m_categories.contains(id)) return false;

    QList<QString> toRemove;
    toRemove.append(id);

    for (auto it = m_categories.begin(); it != m_categories.end(); ++it) {
        if (it.value().parentId == id) {
            toRemove.append(it.key());
        }
    }

    for (const QString& rid : toRemove) {
        m_categories.remove(rid);
        emit categoryDeleted(rid);
    }
    return true;
}

QList<CategoryNode> CategoryManager::allCategories() const {
    QList<CategoryNode> roots;
    QList<CategoryNode> all = m_categories.values();

    for (const CategoryNode& node : all) {
        if (node.parentId.isEmpty()) {
            CategoryNode root = node;
            for (const CategoryNode& child : all) {
                if (child.parentId == root.id) {
                    root.children.append(child);
                }
            }
            roots.append(root);
        }
    }
    return roots;
}

QList<CategoryNode> CategoryManager::searchCategories(const QString& keyword) const {
    QList<CategoryNode> results;
    for (auto it = m_categories.begin(); it != m_categories.end(); ++it) {
        if (it.value().name.contains(keyword, Qt::CaseInsensitive)) {
            results.append(it.value());
        }
    }
    return results;
}

CategoryNode CategoryManager::categoryById(const QString& id) const {
    return m_categories.value(id);
}

int CategoryManager::categoryCount() const {
    return m_categories.size();
}

QList<CategoryNode> CategoryManager::flattenTree(const QList<CategoryNode>& nodes) const {
    QList<CategoryNode> result;
    for (const CategoryNode& node : nodes) {
        result.append(node);
        result.append(flattenTree(node.children));
    }
    return result;
}

CategoryNode* CategoryManager::findNode(const QString& id, QList<CategoryNode>& nodes) {
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes[i].id == id) {
            return &nodes[i];
        }
        CategoryNode* found = findNode(id, nodes[i].children);
        if (found) return found;
    }
    return nullptr;
}

bool CategoryManager::saveToFile(const QString& filePath) {
    QJsonArray arr;
    for (auto it = m_categories.begin(); it != m_categories.end(); ++it) {
        QJsonObject obj;
        obj["id"] = it.value().id;
        obj["name"] = it.value().name;
        obj["parentId"] = it.value().parentId;
        arr.append(obj);
    }

    QJsonObject root;
    root["categories"] = arr;
    root["nextId"] = m_nextId;

    QJsonDocument doc(root);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        return true;
    }
    return false;
}

bool CategoryManager::loadFromFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QJsonObject root = doc.object();
    m_nextId = root["nextId"].toInt(1);

    QJsonArray arr = root["categories"].toArray();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        CategoryNode node;
        node.id = obj["id"].toString();
        node.name = obj["name"].toString();
        node.parentId = obj["parentId"].toString();
        m_categories[node.id] = node;
    }
    return true;
}

