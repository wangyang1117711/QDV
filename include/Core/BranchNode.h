#ifndef BRANCHNODE_H
#define BRANCHNODE_H

#include <QString>
#include <QVariant>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>

struct ToolResult;

class BranchNode {
public:
    QString id;
    QString sourceToolId;
    QString conditionOp;
    QVariant conditionValue;
    QList<QString> trueBranchToolIds;
    QList<QString> falseBranchToolIds;
    
    bool evaluate(const ToolResult& result) const;
    bool isValid() const;
    
    QJsonObject serialize() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["source"] = sourceToolId;
        obj["op"] = conditionOp;
        obj["value"] = QJsonValue::fromVariant(conditionValue);
        
        QJsonArray trueArray;
        for (const QString& tid : trueBranchToolIds) {
            trueArray.append(tid);
        }
        obj["trueBranch"] = trueArray;
        
        QJsonArray falseArray;
        for (const QString& tid : falseBranchToolIds) {
            falseArray.append(tid);
        }
        obj["falseBranch"] = falseArray;
        
        return obj;
    }
    
    void deserialize(const QJsonObject& data) {
        id = data["id"].toString();
        sourceToolId = data["source"].toString();
        conditionOp = data["op"].toString();
        conditionValue = data["value"].toVariant();
        
        QJsonArray trueArray = data["trueBranch"].toArray();
        for (const QJsonValue& v : trueArray) {
            trueBranchToolIds.append(v.toString());
        }
        
        QJsonArray falseArray = data["falseBranch"].toArray();
        for (const QJsonValue& v : falseArray) {
            falseBranchToolIds.append(v.toString());
        }
    }
};

#endif // BRANCHNODE_H