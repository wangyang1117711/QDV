#ifndef CAMERACONFIG_H
#define CAMERACONFIG_H

#include <QString>
#include <QJsonObject>

class CameraConfig {
public:
    QString ip;
    int exposure = 5000;
    double gain = 1.0;
    QString triggerMode = "Software";
    int width = 1920;
    int height = 1080;
    QString pixelFormat = "Mono8";
    
    QJsonObject serialize() const {
        QJsonObject obj;
        obj["ip"] = ip;
        obj["exposure"] = exposure;
        obj["gain"] = gain;
        obj["triggerMode"] = triggerMode;
        obj["width"] = width;
        obj["height"] = height;
        obj["pixelFormat"] = pixelFormat;
        return obj;
    }
    
    void deserialize(const QJsonObject& data) {
        ip = data["ip"].toString();
        exposure = data["exposure"].toInt();
        gain = data["gain"].toDouble();
        triggerMode = data["triggerMode"].toString();
        width = data["width"].toInt();
        height = data["height"].toInt();
        pixelFormat = data["pixelFormat"].toString();
    }
};

#endif // CAMERACONFIG_H