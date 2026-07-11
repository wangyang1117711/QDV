#include "Vision/OpenFramegrabberTool.h"
#include "Core/Logger.h"
#include <QJsonArray>

using namespace QDV;

// 静态成员初始化
QHash<QString, OpenFramegrabberTool::CameraEntry> OpenFramegrabberTool::s_cameras;
QMutex OpenFramegrabberTool::s_cameraMutex;

OpenFramegrabberTool::OpenFramegrabberTool() {
    m_name = "打开相机";
}

OpenFramegrabberTool::~OpenFramegrabberTool() {
    // 析构时不自动释放相机：相机生命周期应跨越多个算子实例，
    // 由 GrabImageTool 或显式调用 releaseCamera 管理释放。
}

// ----- 参数配置 -----
bool OpenFramegrabberTool::configure(const QJsonObject& params) {
    if (params.contains("interfaceName")) {
        setInterfaceName(params["interfaceName"].toString());
    }
    if (params.contains("deviceIndex")) {
        int idx = params["deviceIndex"].toInt(0);
        if (idx < 0) idx = 0;
        setDeviceIndex(idx);
    }
    // v5.3.2：解析新增相机参数
    if (params.contains("width"))       setWidth(params["width"].toInt(0));
    if (params.contains("height"))      setHeight(params["height"].toInt(0));
    if (params.contains("fps"))         setFps(params["fps"].toDouble(0.0));
    if (params.contains("exposure"))    setExposure(params["exposure"].toInt(0));
    if (params.contains("gain"))        setGain(params["gain"].toDouble(-1.0));
    if (params.contains("triggerMode")) setTriggerMode(params["triggerMode"].toString());
    if (params.contains("pixelFormat")) setPixelFormat(params["pixelFormat"].toString());
    if (params.contains("ip"))          setIp(params["ip"].toString());
    if (params.contains("useCameraConfig")) setUseCameraConfig(params["useCameraConfig"].toBool(false));
    if (params.contains("useActiveCamera")) setUseActiveCamera(params["useActiveCamera"].toBool(false));
    m_params = params;
    return true;
}

// ----- 执行：打开相机设备 -----
bool OpenFramegrabberTool::execute(const cv::Mat& input, ToolResult& result) {
    Q_UNUSED(input);

    // v5.3.7：若启用 useActiveCamera，直接检查全局相机表中是否已有 CameraView 注册的句柄
    if (m_useActiveCamera) {
        // 遍历 s_cameras 查找任意已连接的外部相机
        QString foundHandle;
        {
            QMutexLocker locker(&s_cameraMutex);
            for (auto it = s_cameras.begin(); it != s_cameras.end(); ++it) {
                if (it.value().externalOwned && it.value().capture && it.value().capture->isOpened()) {
                    foundHandle = it.key();
                    break;
                }
            }
        }
        if (!foundHandle.isEmpty()) {
            Logger::info(QString("OpenFramegrabberTool: using active camera from CameraView: %1").arg(foundHandle));
            result.ok = true;
            result.score = 1.0;
            result.data["handle"] = foundHandle;
            result.data["interfaceName"] = m_interfaceName;
            result.data["deviceIndex"] = m_deviceIndex;
            result.data["connected"] = true;
            result.data["shared"] = true;
            return true;
        }
        // 未找到外部相机，回退到正常打开流程
        Logger::warn("OpenFramegrabberTool: useActiveCamera=true but no external camera found, fallback to normal open");
    }

    // v5.3.3：若启用 CameraConfig 关联，从全局配置读取默认参数
    if (m_useCameraConfig) {
        CameraConfig cfg = CameraConfig::getGlobal();
        applyCameraConfig(cfg);
        Logger::info("OpenFramegrabberTool: 已从全局 CameraConfig 加载参数");
    }

    // 生成相机句柄
    QString handle = makeHandle(m_interfaceName, m_deviceIndex);

    // 若该句柄的相机已打开，直接返回成功（幂等操作）
    if (isCameraConnected(handle)) {
        Logger::info(QString("OpenFramegrabberTool: camera already open: %1").arg(handle));
        result.ok = true;
        result.score = 1.0;
        result.data["handle"] = handle;
        result.data["interfaceName"] = m_interfaceName;
        result.data["deviceIndex"] = m_deviceIndex;
        result.data["connected"] = true;
        result.data["reopened"] = false;
        return true;
    }

    // 若注册表中存在但已断开，先尝试重连
    {
        QMutexLocker locker(&s_cameraMutex);
        auto it = s_cameras.find(handle);
        if (it != s_cameras.end() && it.value().capture && !it.value().capture->isOpened()) {
            locker.unlock();
            if (reconnectCamera(handle)) {
                Logger::info(QString("OpenFramegrabberTool: reconnected camera: %1").arg(handle));
                result.ok = true;
                result.score = 1.0;
                result.data["handle"] = handle;
                result.data["interfaceName"] = m_interfaceName;
                result.data["deviceIndex"] = m_deviceIndex;
                result.data["connected"] = true;
                result.data["reopened"] = true;
                return true;
            }
            locker.relock();
        }
    }

    // 选择后端并打开设备
    int backend = selectBackend(m_interfaceName);
    cv::VideoCapture* cap = new cv::VideoCapture();

    bool opened = false;
    if (backend >= 0) {
        opened = cap->open(m_deviceIndex, backend);
    } else {
        opened = cap->open(m_deviceIndex);
    }

    if (!opened || !cap->isOpened()) {
        // v5.3.2：增加可用设备数量提示，帮助诊断 device index 越界
        int available = 0;
        if (m_interfaceName == "DirectShow") {
            available = enumerateDirectShowDevices();
        }
        const QString hint = available > 0
            ? QStringLiteral("（当前可用 %1 个 DirectShow 设备，编号 0~%2）")
                  .arg(available).arg(available - 1)
            : QStringLiteral("（未检测到可用设备，请检查相机连接或驱动）");
        Logger::error(QString("OpenFramegrabberTool: failed to open device %1 (%2) %3")
                          .arg(m_deviceIndex)
                          .arg(m_interfaceName)
                          .arg(hint));
        delete cap;
        result.ok = false;
        result.data["error"] = QString("Failed to open camera device %1 via %2 %3")
                                   .arg(m_deviceIndex)
                                   .arg(m_interfaceName)
                                   .arg(hint);
        result.data["handle"] = handle;
        result.data["connected"] = false;
        return false;
    }

    // 注册到全局相机表
    if (!registerCamera(handle, cap, m_interfaceName, m_deviceIndex)) {
        Logger::error(QString("OpenFramegrabberTool: handle conflict: %1").arg(handle));
        cap->release();
        delete cap;
        result.ok = false;
        result.data["error"] = QString("Camera handle already in use: %1").arg(handle);
        return false;
    }

    // v5.3.2：应用相机参数（仅在用户显式设置时才 set，避免覆盖驱动默认值）
    applyCameraParameters(cap);

    // 获取相机实际属性（set 之后读取，反映实际生效值）
    int width  = static_cast<int>(cap->get(cv::CAP_PROP_FRAME_WIDTH));
    int height = static_cast<int>(cap->get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cap->get(cv::CAP_PROP_FPS);

    Logger::info(QString("OpenFramegrabberTool: opened %1 (%2x%3 @ %4fps)")
                     .arg(handle)
                     .arg(width)
                     .arg(height)
                     .arg(fps, 0, 'f', 1));

    result.ok = true;
    result.score = 1.0;
    result.data["handle"] = handle;
    result.data["interfaceName"] = m_interfaceName;
    result.data["deviceIndex"] = m_deviceIndex;
    result.data["connected"] = true;
    result.data["reopened"] = false;
    result.data["width"] = width;
    result.data["height"] = height;
    result.data["fps"] = fps;
    result.data["exposure"] = m_exposure;
    result.data["gain"] = m_gain;
    result.data["triggerMode"] = m_triggerMode;
    result.data["pixelFormat"] = m_pixelFormat;

    m_results["lastHandle"] = handle;
    m_results["lastConnected"] = true;

    return true;
}

// v5.3.2：应用相机参数到 VideoCapture
void OpenFramegrabberTool::applyCameraParameters(cv::VideoCapture* cap) {
    if (!cap || !cap->isOpened()) return;

    // 分辨率
    if (m_width > 0) {
        cap->set(cv::CAP_PROP_FRAME_WIDTH, static_cast<double>(m_width));
        Logger::info(QString("OpenFramegrabberTool: set width=%1").arg(m_width));
    }
    if (m_height > 0) {
        cap->set(cv::CAP_PROP_FRAME_HEIGHT, static_cast<double>(m_height));
        Logger::info(QString("OpenFramegrabberTool: set height=%1").arg(m_height));
    }
    // 帧率
    if (m_fps > 0.0) {
        cap->set(cv::CAP_PROP_FPS, m_fps);
        Logger::info(QString("OpenFramegrabberTool: set fps=%1").arg(m_fps, 0, 'f', 1));
    }
    // 曝光（CAP_PROP_EXPOSURE 在 DirectShow 后端通常为 1~10000，具体语义依后端）
    if (m_exposure > 0) {
        // 关闭自动曝光后才能手动设置
        cap->set(cv::CAP_PROP_AUTO_EXPOSURE, 1.0);  // 1=手动模式
        cap->set(cv::CAP_PROP_EXPOSURE, static_cast<double>(m_exposure));
        Logger::info(QString("OpenFramegrabberTool: set exposure=%1us").arg(m_exposure));
    }
    // Gain
    if (m_gain >= 0.0) {
        cap->set(cv::CAP_PROP_GAIN, m_gain);
        Logger::info(QString("OpenFramegrabberTool: set gain=%1").arg(m_gain, 0, 'f', 2));
    }
    // 触发模式（OpenCV 对触发模式支持有限，这里仅记录日志，实际触发由相机 SDK 管理）
    if (m_triggerMode != "Continuous") {
        Logger::info(QString("OpenFramegrabberTool: triggerMode=%1 (需相机 SDK 支持)")
                         .arg(m_triggerMode));
    }
}

// ----- 序列化/反序列化 -----
QJsonObject OpenFramegrabberTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["interfaceName"] = m_interfaceName;
    obj["deviceIndex"] = m_deviceIndex;
    // v5.3.2：序列化新增相机参数
    obj["width"] = m_width;
    obj["height"] = m_height;
    obj["fps"] = m_fps;
    obj["exposure"] = m_exposure;
    obj["gain"] = m_gain;
    obj["triggerMode"] = m_triggerMode;
    obj["pixelFormat"] = m_pixelFormat;
    obj["ip"] = m_ip;
    obj["useCameraConfig"] = m_useCameraConfig;
    return obj;
}

bool OpenFramegrabberTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;
    return configure(data);
}

// v5.3.2：CameraConfig 关联实现
void OpenFramegrabberTool::applyCameraConfig(const CameraConfig& config) {
    // 从全局 CameraConfig 读取配置作为默认参数（仅当当前值为默认/未设置时）
    if (m_width <= 0)       m_width = config.width;
    if (m_height <= 0)      m_height = config.height;
    if (m_exposure <= 0)    m_exposure = config.exposure;
    if (m_gain < 0.0)       m_gain = config.gain;
    if (m_triggerMode.isEmpty() || m_triggerMode == "Continuous") {
        m_triggerMode = config.triggerMode;
    }
    if (m_pixelFormat.isEmpty() || m_pixelFormat == "Mono8") {
        m_pixelFormat = config.pixelFormat;
    }
    if (m_ip.isEmpty())     m_ip = config.ip;
    Logger::info(QString("OpenFramegrabberTool: applied CameraConfig (w=%1 h=%2 exp=%3 gain=%4)")
                     .arg(m_width).arg(m_height).arg(m_exposure).arg(m_gain, 0, 'f', 2));
}

void OpenFramegrabberTool::fillCameraConfig(CameraConfig& config) const {
    config.width = m_width;
    config.height = m_height;
    config.exposure = m_exposure;
    config.gain = m_gain;
    config.triggerMode = m_triggerMode;
    config.pixelFormat = m_pixelFormat;
    config.ip = m_ip;
}

// ----- 全局相机注册表实现 -----

QString OpenFramegrabberTool::makeHandle(const QString& interfaceName, int deviceIndex) {
    return QString("cam::%1::%2").arg(interfaceName).arg(deviceIndex);
}

cv::VideoCapture* OpenFramegrabberTool::acquireCamera(const QString& handle) {
    QMutexLocker locker(&s_cameraMutex);
    auto it = s_cameras.find(handle);
    if (it != s_cameras.end()) {
        return it.value().capture;
    }
    return nullptr;
}

bool OpenFramegrabberTool::registerCamera(const QString& handle, cv::VideoCapture* cap,
                                           const QString& interfaceName, int deviceIndex) {
    QMutexLocker locker(&s_cameraMutex);
    if (s_cameras.contains(handle)) {
        return false;  // 句柄已存在
    }
    CameraEntry entry;
    entry.capture = cap;
    entry.interfaceName = interfaceName;
    entry.deviceIndex = deviceIndex;
    s_cameras.insert(handle, entry);
    return true;
}

bool OpenFramegrabberTool::releaseCamera(const QString& handle) {
    QMutexLocker locker(&s_cameraMutex);
    auto it = s_cameras.find(handle);
    if (it == s_cameras.end()) {
        return false;
    }
    if (it.value().capture) {
        it.value().capture->release();
        // v5.3.7：外部拥有的句柄不 delete，由注册方（如 CameraView）管理生命周期
        if (!it.value().externalOwned) {
            delete it.value().capture;
        }
    }
    s_cameras.erase(it);
    Logger::info(QString("OpenFramegrabberTool: released camera: %1 (external=%2)")
                     .arg(handle).arg(it.value().externalOwned));
    return true;
}

// v5.3.7：注册外部拥有的相机句柄（如 CameraView 已打开的相机）
bool OpenFramegrabberTool::registerExternalCamera(const QString& handle, cv::VideoCapture* cap,
                                                   const QString& interfaceName, int deviceIndex) {
    if (!cap) return false;
    QMutexLocker locker(&s_cameraMutex);
    if (s_cameras.contains(handle)) {
        // 句柄已存在：如果是同一个 cap 则更新状态，否则拒绝
        if (s_cameras[handle].capture == cap) {
            s_cameras[handle].externalOwned = true;
            return true;
        }
        return false;
    }
    CameraEntry entry;
    entry.capture = cap;
    entry.interfaceName = interfaceName;
    entry.deviceIndex = deviceIndex;
    entry.externalOwned = true;
    s_cameras[handle] = entry;
    Logger::info(QString("OpenFramegrabberTool: registered external camera: %1 (%2::%3)")
                     .arg(handle).arg(interfaceName).arg(deviceIndex));
    return true;
}

// v5.3.7：注销外部拥有的相机句柄（不 delete cap）
bool OpenFramegrabberTool::unregisterExternalCamera(const QString& handle) {
    QMutexLocker locker(&s_cameraMutex);
    auto it = s_cameras.find(handle);
    if (it == s_cameras.end()) {
        return false;
    }
    // 仅注销外部拥有的句柄，不 release/delete cap
    s_cameras.erase(it);
    Logger::info(QString("OpenFramegrabberTool: unregistered external camera: %1").arg(handle));
    return true;
}

bool OpenFramegrabberTool::isCameraConnected(const QString& handle) {
    QMutexLocker locker(&s_cameraMutex);
    auto it = s_cameras.find(handle);
    if (it == s_cameras.end()) {
        return false;
    }
    return it.value().capture && it.value().capture->isOpened();
}

bool OpenFramegrabberTool::reconnectCamera(const QString& handle) {
    QMutexLocker locker(&s_cameraMutex);
    auto it = s_cameras.find(handle);
    if (it == s_cameras.end() || !it.value().capture) {
        return false;
    }

    CameraEntry entry = it.value();
    // 先释放旧的，再重新打开
    entry.capture->release();

    int backend = selectBackend(entry.interfaceName);
    bool opened = false;
    if (backend >= 0) {
        opened = entry.capture->open(entry.deviceIndex, backend);
    } else {
        opened = entry.capture->open(entry.deviceIndex);
    }

    if (!opened) {
        Logger::warn(QString("OpenFramegrabberTool: reconnect failed: %1").arg(handle));
        return false;
    }

    Logger::info(QString("OpenFramegrabberTool: reconnect succeeded: %1").arg(handle));
    return true;
}

QStringList OpenFramegrabberTool::listCameraHandles() {
    QMutexLocker locker(&s_cameraMutex);
    return s_cameras.keys();
}

int OpenFramegrabberTool::selectBackend(const QString& interfaceName) {
    // 根据接口名称映射 OpenCV VideoCapture 后端 API
    if (interfaceName == "DirectShow") {
        return cv::CAP_DSHOW;       // Windows DirectShow
    } else if (interfaceName == "GigEVision") {
        return cv::CAP_GIGANETIX;   // GigE Vision（需 OpenCV 编译支持）
    } else if (interfaceName == "USB3Vision") {
        return cv::CAP_ANY;         // 使用默认后端自动检测 USB3 设备
    } else if (interfaceName == "File") {
        return cv::CAP_IMAGES;      // 文件序列模式
    }
    return cv::CAP_ANY;             // 默认自动检测
}

int OpenFramegrabberTool::enumerateDirectShowDevices() {
    // Windows 下逐个尝试打开 DirectShow 设备索引，统计可用数量
    int count = 0;
    for (int i = 0; i < 16; ++i) {
        cv::VideoCapture cap(i, cv::CAP_DSHOW);
        if (cap.isOpened()) {
            ++count;
            cap.release();
        } else {
            break;  // 连续失败则停止
        }
    }
    return count;
}
