#ifndef MODELBINDING_H
#define MODELBINDING_H

#include <QString>
#include <QJsonObject>

class ModelBinding {
public:
    QString modelPath;
    QString modelType;
    double threshold = 0.85;
    bool enabled = false;
    
    QJsonObject serialize() const {
        QJsonObject obj;
        obj["model"] = modelPath;
        obj["type"] = modelType;
        obj["threshold"] = threshold;
        obj["enabled"] = enabled;
        return obj;
    }
    
    void deserialize(const QJsonObject& data) {
        modelPath = data["model"].toString();
        modelType = data["type"].toString();
        threshold = data["threshold"].toDouble();
        enabled = data["enabled"].toBool();
    }
};

#endif // MODELBINDING_H