#ifndef TRIGGERCONFIG_H
#define TRIGGERCONFIG_H

#include <QString>
#include <QJsonObject>

class TriggerConfig {
public:
    QString mode = "Software";
    int delay = 0;
    int pulseWidth = 100;
    bool debounce = false;
    int debounceTime = 10;
    
    QJsonObject serialize() const {
        QJsonObject obj;
        obj["mode"] = mode;
        obj["delay"] = delay;
        obj["pulseWidth"] = pulseWidth;
        obj["debounce"] = debounce;
        obj["debounceTime"] = debounceTime;
        return obj;
    }
    
    void deserialize(const QJsonObject& data) {
        mode = data["mode"].toString();
        delay = data["delay"].toInt();
        pulseWidth = data["pulseWidth"].toInt();
        debounce = data["debounce"].toBool();
        debounceTime = data["debounceTime"].toInt();
    }
};

#endif // TRIGGERCONFIG_H