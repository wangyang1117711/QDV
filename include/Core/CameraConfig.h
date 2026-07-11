#ifndef CAMERACONFIG_H
#define CAMERACONFIG_H

#include <QString>
#include <QJsonObject>
#include <QMutex>

/**
 * @brief 相机配置（v5.3.3 增强为全局共享）
 *
 * v5.3.3：增加全局实例 + 线程安全访问，作为 CameraView 与
 * OpenFramegrabberTool 之间的配置桥梁。
 * - CameraView 在"相机设置"对话框中写入全局配置
 * - OpenFramegrabberTool（useCameraConfig=true 时）读取全局配置作为默认参数
 */
class CameraConfig {
public:
    QString ip;
    int exposure = 5000;
    double gain = 1.0;
    QString triggerMode = "Continuous";
    int width = 1920;
    int height = 1080;
    QString pixelFormat = "Mono8";
    double fps = 30.0;
    double brightness = 128.0;
    double contrast = 128.0;

    QJsonObject serialize() const {
        QJsonObject obj;
        obj["ip"] = ip;
        obj["exposure"] = exposure;
        obj["gain"] = gain;
        obj["triggerMode"] = triggerMode;
        obj["width"] = width;
        obj["height"] = height;
        obj["pixelFormat"] = pixelFormat;
        obj["fps"] = fps;
        obj["brightness"] = brightness;
        obj["contrast"] = contrast;
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
        fps = data["fps"].toDouble(30.0);
        brightness = data["brightness"].toDouble(128.0);
        contrast = data["contrast"].toDouble(128.0);
    }

    // ===== v5.3.3：全局实例访问（线程安全） =====

    /// 获取全局 CameraConfig 实例
    static CameraConfig& global() {
        static CameraConfig instance;
        return instance;
    }

    /// 线程安全读取全局配置副本
    static CameraConfig getGlobal() {
        QMutexLocker locker(&s_mutex);
        return global();
    }

    /// 线程安全写入全局配置
    static void setGlobal(const CameraConfig& config) {
        QMutexLocker locker(&s_mutex);
        global() = config;
    }

private:
    static QMutex s_mutex;
};

#endif // CAMERACONFIG_H
