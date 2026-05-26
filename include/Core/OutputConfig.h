#ifndef OUTPUTCONFIG_H
#define OUTPUTCONFIG_H

#include <QString>
#include <QJsonObject>

struct TCPConfig {
    QString host;
    int port = 5000;
    QString format = "json";
    
    QJsonObject serialize() const {
        QJsonObject obj;
        obj["host"] = host;
        obj["port"] = port;
        obj["format"] = format;
        return obj;
    }
    
    void deserialize(const QJsonObject& data) {
        host = data["host"].toString();
        port = data["port"].toInt();
        format = data["format"].toString();
    }
};

struct IOConfig {
    QString line0;
    QString line1;
    QString line2;
    QString line3;
    
    QJsonObject serialize() const {
        QJsonObject obj;
        obj["line0"] = line0;
        obj["line1"] = line1;
        obj["line2"] = line2;
        obj["line3"] = line3;
        return obj;
    }
    
    void deserialize(const QJsonObject& data) {
        line0 = data["line0"].toString();
        line1 = data["line1"].toString();
        line2 = data["line2"].toString();
        line3 = data["line3"].toString();
    }
};

class OutputConfig {
public:
    TCPConfig tcp;
    IOConfig io;
    bool ftpEnabled = false;
    QString ftpHost;
    QString ftpPath;
    
    QJsonObject serialize() const {
        QJsonObject obj;
        obj["tcp"] = tcp.serialize();
        obj["io"] = io.serialize();
        obj["ftpEnabled"] = ftpEnabled;
        obj["ftpHost"] = ftpHost;
        obj["ftpPath"] = ftpPath;
        return obj;
    }
    
    void deserialize(const QJsonObject& data) {
        tcp.deserialize(data["tcp"].toObject());
        io.deserialize(data["io"].toObject());
        ftpEnabled = data["ftpEnabled"].toBool();
        ftpHost = data["ftpHost"].toString();
        ftpPath = data["ftpPath"].toString();
    }
};

#endif // OUTPUTCONFIG_H