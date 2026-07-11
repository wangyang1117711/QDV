#ifndef BRANCHNODE_H
#define BRANCHNODE_H

#include <QString>
#include <QVariant>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QRegularExpression>
#include <QStringList>

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

        // P1-B4-H2 修复：兼容字符串格式的 trueBranch/falseBranch
        // 历史方案数据可能以字符串形式存储（如 "tool1,tool2"），需先按逗号/换行拆分再装填
        // 否则 QJsonValue::toArray() 对字符串返回空数组，导致分支节点失效
        trueBranchToolIds = deserializeToolIdList(data.value("trueBranch"));
        falseBranchToolIds = deserializeToolIdList(data.value("falseBranch"));
    }

private:
    // P1-B4-H2：将 JSON 值（数组或字符串）反序列化为工具 ID 列表
    static QList<QString> deserializeToolIdList(const QJsonValue& v) {
        QList<QString> result;
        if (v.isArray()) {
            const QJsonArray arr = v.toArray();
            for (const QJsonValue& item : arr) {
                const QString s = item.toString();
                if (!s.isEmpty()) result.append(s);
            }
        } else if (v.isString()) {
            const QString s = v.toString();
            const QStringList parts = s.split(
                QRegularExpression(QStringLiteral("[,，\n]")),
                Qt::SkipEmptyParts);
            for (const QString& p : parts) {
                const QString trimmed = p.trimmed();
                if (!trimmed.isEmpty()) result.append(trimmed);
            }
        }
        return result;
    }
};

#endif // BRANCHNODE_H