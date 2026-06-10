#include "TrainingInference/CategoryManager.h"
#include "Core/Logger.h"
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QTextStream>
#include <QSettings>

using namespace QDV;

CategoryManager* CategoryManager::s_instance = nullptr;

CategoryManager* CategoryManager::instance() {
    if (!s_instance) {
        s_instance = new CategoryManager();
    }
    return s_instance;
}

CategoryManager::CategoryManager(QObject* parent) : QObject(parent) {
}

QString CategoryManager::validateCategoryName(const QString& name, const QString& parentId) {
    Q_UNUSED(parentId)
    if (name.isEmpty()) {
        return QString::fromUtf8("类别名称不能为空");
    }
    if (name.length() > 50) {
        return QString::fromUtf8("类别名称不能超过50个字符");
    }
    static const QRegularExpression invalidChars(R"([<>:"/\|?*])");
    if (invalidChars.match(name).hasMatch()) {
        return QString::fromUtf8("类别名称不能包含特殊字符: <>:\"/\\|?*");
    }
    return QString();
}

QString CategoryManager::validateCategoryColor(const QString& color) {
    if (color.isEmpty()) {
        return QString::fromUtf8("颜色不能为空");
    }
    static const QRegularExpression hexColor(R"(^#[0-9A-Fa-f]{6}$)");
    if (!hexColor.match(color).hasMatch()) {
        return QString::fromUtf8("颜色格式错误，需为 #RRGGBB 格式（如 #FF5733）");
    }
    return QString();
}

QString CategoryManager::calculateNodeId(int nextId) {
    return QString("cat_%1_%2")
        .arg(nextId)
        .arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
}

int CategoryManager::calculateDepth(const QString& id) const {
    int depth = 0;
    QString currentId = id;
    while (m_categories.contains(currentId) && !m_categories[currentId].parentId.isEmpty()) {
        depth++;
        currentId = m_categories[currentId].parentId;
        if (depth > 10) break;
    }
    return depth;
}

bool CategoryManager::hasSiblingWithName(const QString& name, const QString& parentId, const QString& excludeId) const {
    for (auto it = m_categories.begin(); it != m_categories.end(); ++it) {
        if (it.value().parentId == parentId &&
            it.value().name.compare(name, Qt::CaseInsensitive) == 0 &&
            it.key() != excludeId) {
            return true;
        }
    }
    return false;
}

QString CategoryManager::createCategory(const QString& name,
                                         const QString& parentId,
                                         const QString& color,
                                         const QString& description,
                                         const QStringList& sampleImages,
                                         bool isPublic) {
    QString nameErr = validateCategoryName(name);
    if (!nameErr.isEmpty()) {
        Logger::warn("CategoryManager: invalid name - " + nameErr);
        return QString();
    }

    if (hasSiblingWithName(name, parentId)) {
        Logger::warn("CategoryManager: duplicate name under same parent - " + name);
        return QString();
    }

    if (!parentId.isEmpty() && !m_categories.contains(parentId)) {
        Logger::warn("CategoryManager: parent category not found - " + parentId);
        return QString();
    }

    int depth = calculateDepth(parentId);
    if (depth >= maxDepth()) {
        Logger::warn(QString("CategoryManager: max depth (%1) exceeded").arg(maxDepth()));
        return QString();
    }

    if (!color.isEmpty()) {
        QString colorErr = validateCategoryColor(color);
        if (!colorErr.isEmpty()) {
            Logger::warn("CategoryManager: invalid color - " + colorErr);
            return QString();
        }
    }

    QString id = calculateNodeId(m_nextId++);

    CategoryNode node;
    node.id = id;
    node.name = name;
    node.parentId = parentId;
    node.color = color.isEmpty() ? "#CCCCCC" : color;
    node.description = description;
    node.sampleImages = sampleImages.mid(0, 5);
    node.isPublic = isPublic;
    node.depth = depth;
    node.createdAt = QDateTime::currentDateTime();
    node.modifiedAt = node.createdAt;

    m_categories[id] = node;
    emit categoryCreated(id, name);
    return id;
}

bool CategoryManager::updateCategory(const QString& id,
                                      const QString& newName,
                                      const QString& newColor,
                                      const QString& newDescription,
                                      bool newIsPublic) {
    if (!m_categories.contains(id)) return false;

    CategoryNode& node = m_categories[id];

    QString nameErr = validateCategoryName(newName);
    if (!nameErr.isEmpty()) {
        Logger::warn("CategoryManager: update - " + nameErr);
        return false;
    }

    if (hasSiblingWithName(newName, node.parentId, id)) {
        Logger::warn("CategoryManager: update - duplicate name under same parent");
        return false;
    }

    node.name = newName;
    if (!newColor.isEmpty()) {
        QString colorErr = validateCategoryColor(newColor);
        if (colorErr.isEmpty()) {
            node.color = newColor;
        }
    }
    if (!newDescription.isNull()) {
        node.description = newDescription;
    }
    node.isPublic = newIsPublic;
    node.modifiedAt = QDateTime::currentDateTime();

    emit categoryUpdated(id, newName);
    return true;
}

bool CategoryManager::deleteCategory(const QString& id) {
    if (!m_categories.contains(id)) return false;

    QList<QString> toRemove;
    toRemove.append(id);

    QList<QString> toCheck = {id};
    while (!toCheck.isEmpty()) {
        QString current = toCheck.takeFirst();
        for (auto it = m_categories.begin(); it != m_categories.end(); ++it) {
            if (it.value().parentId == current && !toRemove.contains(it.key())) {
                toRemove.append(it.key());
                toCheck.append(it.key());
            }
        }
    }

    for (const QString& rid : toRemove) {
        m_categories.remove(rid);
        emit categoryDeleted(rid);
    }
    return true;
}

QList<CategoryNode> CategoryManager::allCategories() const {
    return buildTree();
}

QList<CategoryNode> CategoryManager::buildTree() const {
    QList<CategoryNode> roots;
    QList<CategoryNode> all = m_categories.values();

    for (const CategoryNode& node : all) {
        if (node.parentId.isEmpty()) {
            CategoryNode root = node;
            for (const CategoryNode& child : all) {
                if (child.parentId == root.id) {
                    CategoryNode childCopy = child;
                    for (const CategoryNode& grandChild : all) {
                        if (grandChild.parentId == child.id) {
                            childCopy.children.append(grandChild);
                        }
                    }
                    root.children.append(childCopy);
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
        if (it.value().name.contains(keyword, Qt::CaseInsensitive) ||
            it.value().description.contains(keyword, Qt::CaseInsensitive)) {
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

int CategoryManager::categoryCountByParent(const QString& parentId) const {
    int count = 0;
    for (auto it = m_categories.begin(); it != m_categories.end(); ++it) {
        if (it.value().parentId == parentId) count++;
    }
    return count;
}

bool CategoryManager::isAccessible(const QString& categoryId) const {
    if (!m_categories.contains(categoryId)) return false;
    if (m_categories[categoryId].isPublic) return true;
    QString currentUser = QSettings().value("User/currentUser").toString();
    return !currentUser.isEmpty();
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

const CategoryNode* CategoryManager::findNode(const QString& id, const QList<CategoryNode>& nodes) const {
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes[i].id == id) {
            return &nodes[i];
        }
        const CategoryNode* found = findNode(id, nodes[i].children);
        if (found) return found;
    }
    return nullptr;
}

void CategoryManager::rebuildChildren(QList<CategoryNode>& roots, const QList<CategoryNode>& all) const {
    for (auto& root : roots) {
        for (const auto& child : all) {
            if (child.parentId == root.id) {
                CategoryNode childCopy = child;
                QList<CategoryNode> grandChildren;
                for (const auto& gc : all) {
                    if (gc.parentId == childCopy.id) {
                        grandChildren.append(gc);
                    }
                }
                rebuildChildren(grandChildren, all);
                childCopy.children = grandChildren;
                root.children.append(childCopy);
            }
        }
    }
}

bool CategoryManager::saveToFile(const QString& filePath) {
    return exportToJSON(filePath);
}

bool CategoryManager::exportToJSON(const QString& filePath) {
    QJsonArray arr;
    for (auto it = m_categories.begin(); it != m_categories.end(); ++it) {
        QJsonObject obj;
        obj["id"] = it.value().id;
        obj["name"] = it.value().name;
        obj["parentId"] = it.value().parentId;
        obj["color"] = it.value().color;
        obj["description"] = it.value().description;
        obj["isPublic"] = it.value().isPublic;
        obj["depth"] = it.value().depth;
        QJsonArray samples;
        for (const QString& img : it.value().sampleImages) {
            samples.append(img);
        }
        obj["sampleImages"] = samples;
        obj["createdAt"] = it.value().createdAt.toString(Qt::ISODate);
        obj["modifiedAt"] = it.value().modifiedAt.toString(Qt::ISODate);
        arr.append(obj);
    }

    QJsonObject root;
    root["version"] = "1.0";
    root["exportTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["categories"] = arr;
    root["nextId"] = m_nextId;

    QJsonDocument doc(root);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        Logger::info("CategoryManager: exported " + QString::number(m_categories.size()) + " categories to " + filePath);
        return true;
    }
    return false;
}

bool CategoryManager::exportToCSV(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream stream(&file);
    stream << "id,name,parentId,color,description,isPublic,depth\n";

    for (auto it = m_categories.begin(); it != m_categories.end(); ++it) {
        const CategoryNode& node = it.value();
        QString escapedDesc = node.description;
        escapedDesc.replace("\"", "\"\"");
        if (escapedDesc.contains(',') || escapedDesc.contains('"') || escapedDesc.contains('\n')) {
            escapedDesc = "\"" + escapedDesc + "\"";
        }

        stream << node.id << ","
               << node.name << ","
               << node.parentId << ","
               << node.color << ","
               << escapedDesc << ","
               << (node.isPublic ? "true" : "false") << ","
               << node.depth << "\n";
    }

    file.close();
    Logger::info("CategoryManager: exported CSV to " + filePath);
    return true;
}

bool CategoryManager::loadFromFile(const QString& filePath) {
    ImportResult result = importFromJSON(filePath);
    return result.imported > 0 || (result.total > 0 && result.skipped == result.total);
}

ImportResult CategoryManager::importFromJSON(const QString& filePath) {
    ImportResult result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errors.append(QString::fromUtf8("无法打开文件: %1").arg(filePath));
        return result;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        result.errors.append(QString::fromUtf8("JSON解析错误: %1").arg(parseError.errorString()));
        return result;
    }

    QJsonObject root = doc.object();
    int fileNextId = root["nextId"].toInt(1);
    if (fileNextId > m_nextId) m_nextId = fileNextId;

    QJsonArray arr = root["categories"].toArray();
    result.total = arr.size();

    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        QString id = obj["id"].toString();
        QString name = obj["name"].toString();
        QString parentId = obj["parentId"].toString();

        if (id.isEmpty() || name.isEmpty()) {
            result.skipped++;
            result.errors.append(QString::fromUtf8("第%1项: 缺少id或name字段，已跳过").arg(i + 1));
            continue;
        }

        if (m_categories.contains(id)) {
            result.skipped++;
            result.errors.append(QString::fromUtf8("第%1项: ID已存在 (%2)，已跳过").arg(i + 1).arg(id));
            continue;
        }

        QString nameErr = validateCategoryName(name);
        if (!nameErr.isEmpty()) {
            result.skipped++;
            result.errors.append(QString::fromUtf8("第%1项: 名称无效 (%2)").arg(i + 1).arg(nameErr));
            continue;
        }

        if (hasSiblingWithName(name, parentId)) {
            name = name + " (2)";
        }

        QString color = obj["color"].toString("#CCCCCC");
        if (!color.isEmpty()) {
            QString colorErr = validateCategoryColor(color);
            if (!colorErr.isEmpty()) color = "#CCCCCC";
        }

        int depth = obj["depth"].toInt(0);
        if (depth > maxDepth()) {
            result.skipped++;
            result.errors.append(QString::fromUtf8("第%1项: 深度超限 (%2 > %3)，已跳过")
                .arg(i + 1).arg(depth).arg(maxDepth()));
            continue;
        }

        CategoryNode node;
        node.id = id;
        node.name = name;
        node.parentId = parentId;
        node.color = color;
        node.description = obj["description"].toString();
        node.isPublic = obj["isPublic"].toBool(true);
        node.depth = depth;

        QJsonArray samples = obj["sampleImages"].toArray();
        for (int j = 0; j < samples.size() && j < 5; ++j) {
            node.sampleImages.append(samples[j].toString());
        }

        node.createdAt = QDateTime::fromString(obj["createdAt"].toString(), Qt::ISODate);
        if (!node.createdAt.isValid()) node.createdAt = QDateTime::currentDateTime();
        node.modifiedAt = QDateTime::fromString(obj["modifiedAt"].toString(), Qt::ISODate);
        if (!node.modifiedAt.isValid()) node.modifiedAt = QDateTime::currentDateTime();

        m_categories[id] = node;
        result.imported++;
    }

    Logger::info(QString("CategoryManager: JSON import - total:%1 imported:%2 skipped:%3")
        .arg(result.total).arg(result.imported).arg(result.skipped));

    return result;
}

ImportResult CategoryManager::importFromCSV(const QString& filePath) {
    ImportResult result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errors.append(QString::fromUtf8("无法打开文件: %1").arg(filePath));
        return result;
    }

    QTextStream stream(&file);
    QString header = stream.readLine();
    if (!header.contains("id") || !header.contains("name")) {
        result.errors.append(QString::fromUtf8("CSV格式错误: 缺少id或name列"));
        file.close();
        return result;
    }

    QMap<QString, int> colMap;
    QStringList headers = header.split(',');
    for (int i = 0; i < headers.size(); ++i) {
        colMap[headers[i].trimmed()] = i;
    }

    int lineNum = 1;
    while (!stream.atEnd()) {
        lineNum++;
        QString line = stream.readLine().trimmed();
        if (line.isEmpty()) continue;

        QStringList fields;
        bool inQuotes = false;
        QString currentField;
        for (int i = 0; i < line.length(); ++i) {
            QChar c = line[i];
            if (c == '"') {
                inQuotes = !inQuotes;
            } else if (c == ',' && !inQuotes) {
                fields.append(currentField.trimmed());
                currentField.clear();
            } else {
                currentField += c;
            }
        }
        fields.append(currentField.trimmed());

        auto val = [&](const QString& key) -> QString {
            return colMap.contains(key) && colMap[key] < fields.size()
                ? fields[colMap[key]] : QString();
        };

        QString id = val("id");
        QString name = val("name");
        QString parentId = val("parentId");

        if (id.isEmpty() || name.isEmpty()) {
            result.skipped++;
            result.errors.append(QString::fromUtf8("第%1行: 缺少id或name字段，已跳过").arg(lineNum));
            continue;
        }

        if (m_categories.contains(id)) {
            result.skipped++;
            result.errors.append(QString::fromUtf8("第%1行: ID已存在 (%2)，已跳过").arg(lineNum).arg(id));
            continue;
        }

        QString nameErr = validateCategoryName(name);
        if (!nameErr.isEmpty()) {
            result.skipped++;
            result.errors.append(QString::fromUtf8("第%1行: 名称无效 (%2)").arg(lineNum).arg(nameErr));
            continue;
        }

        if (hasSiblingWithName(name, parentId)) {
            name = name + " (2)";
        }

        CategoryNode node;
        node.id = id;
        node.name = name;
        node.parentId = parentId;
        node.color = val("color").isEmpty() ? "#CCCCCC" : val("color");
        node.description = val("description");
        node.isPublic = val("isPublic").toLower() != "false";
        node.depth = val("depth").toInt();
        node.createdAt = QDateTime::currentDateTime();
        node.modifiedAt = node.createdAt;

        if (node.depth > maxDepth()) {
            result.skipped++;
            result.errors.append(QString::fromUtf8("第%1行: 深度超限 (%2 > %3)，已跳过")
                .arg(lineNum).arg(node.depth).arg(maxDepth()));
            continue;
        }

        m_categories[id] = node;
        result.imported++;
    }

    result.total = result.imported + result.skipped;
    file.close();

    Logger::info(QString("CategoryManager: CSV import - total:%1 imported:%2 skipped:%3")
        .arg(result.total).arg(result.imported).arg(result.skipped));

    return result;
}

int CategoryManager::loadPresetCategories(const QString& jsonFilePath) {
    QFile file(jsonFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        Logger::warn("CategoryManager: preset file not found - " + jsonFilePath);
        return 0;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QJsonObject root = doc.object();
    QJsonArray categories = root["categories"].toArray();

    int loaded = 0;
    for (int i = 0; i < categories.size(); ++i) {
        QJsonObject obj = categories[i].toObject();
        QString presetId = obj["id"].toString();

        if (m_categories.contains(presetId)) continue;

        CategoryNode node;
        node.id = presetId;
        node.name = obj["name"].toString();
        node.parentId = QString();
        node.color = "#42A5F5";
        node.description = obj.contains("zhName") ? obj["zhName"].toString() : obj["name"].toString();
        node.isPublic = true;
        node.depth = 0;
        node.createdAt = QDateTime::currentDateTime();
        node.modifiedAt = node.createdAt;

        m_categories[presetId] = node;
        loaded++;
    }

    Logger::info(QString("CategoryManager: loaded %1 preset categories from %2")
        .arg(loaded).arg(jsonFilePath));

    return loaded;
}