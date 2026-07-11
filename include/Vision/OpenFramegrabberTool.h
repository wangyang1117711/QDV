#ifndef OPENFRAMEGRABBERTOOL_H
#define OPENFRAMEGRABBERTOOL_H

#include "Core/VisionTool.h"
#include "Core/CameraConfig.h"
#include <QMutex>
#include <QHash>
#include <QElapsedTimer>
#include <opencv2/videoio.hpp>

/**
 * @brief 打开相机算子（OpenFramegrabber）
 *
 * 通过 OpenCV VideoCapture 打开图像采集设备，支持 DirectShow / GigEVision /
 * USB3Vision / File 等接口。打开成功后将相机句柄注册到全局相机表，
 * 供后续 GrabImageTool 通过 acqHandle 引用。
 *
 * v5.3.2 增强：
 * - 支持分辨率/曝光/Gain/触发模式/像素格式/GigE IP 等相机参数配置
 * - 打开设备后通过 cap->set() 应用参数
 * - 可读取全局 CameraConfig 作为默认参数来源
 * - 打开后将实际参数回写 CameraConfig，实现双向同步
 *
 * 相机句柄命名格式：cam::{interfaceName}::{deviceIndex}
 */
class OpenFramegrabberTool : public QDV::VisionTool {
public:
    OpenFramegrabberTool();
    ~OpenFramegrabberTool() override;

    QString type() const override { return "OpenFramegrabber"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;

    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // ----- 参数访问器 -----
    void setInterfaceName(const QString& name) { m_interfaceName = name; }
    QString interfaceName() const { return m_interfaceName; }

    void setDeviceIndex(int idx) { m_deviceIndex = idx; }
    int deviceIndex() const { return m_deviceIndex; }

    // v5.3.2：相机参数访问器
    void setWidth(int w) { m_width = w; }
    int width() const { return m_width; }
    void setHeight(int h) { m_height = h; }
    int height() const { return m_height; }
    void setFps(double f) { m_fps = f; }
    double fps() const { return m_fps; }
    void setExposure(int e) { m_exposure = e; }
    int exposure() const { return m_exposure; }
    void setGain(double g) { m_gain = g; }
    double gain() const { return m_gain; }
    void setTriggerMode(const QString& t) { m_triggerMode = t; }
    QString triggerMode() const { return m_triggerMode; }
    void setPixelFormat(const QString& p) { m_pixelFormat = p; }
    QString pixelFormat() const { return m_pixelFormat; }
    void setIp(const QString& ip) { m_ip = ip; }
    QString ip() const { return m_ip; }
    void setUseCameraConfig(bool u) { m_useCameraConfig = u; }
    bool useCameraConfig() const { return m_useCameraConfig; }
    void setUseActiveCamera(bool u) { m_useActiveCamera = u; }
    bool useActiveCamera() const { return m_useActiveCamera; }

    // v5.3.2：CameraConfig 关联
    /// 从全局 CameraConfig 读取配置作为默认参数
    void applyCameraConfig(const CameraConfig& config);
    /// 将当前实际参数回写到 CameraConfig
    void fillCameraConfig(CameraConfig& config) const;

    // ----- 全局相机注册表（供 GrabImageTool 使用） -----

    /**
     * @brief 根据句柄获取已打开的相机（VideoCapture）。
     * @param handle 句柄名称（cam::{interface}::{index}）
     * @return 指向 VideoCapture 的指针，未找到返回 nullptr。
     * @note 返回的指针所有权归 OpenFramegrabberTool 管理，调用方不得 delete。
     */
    static cv::VideoCapture* acquireCamera(const QString& handle);

    /**
     * @brief 释放并关闭指定句柄的相机，从注册表移除。
     * @param handle 句柄名称
     * @return 成功释放返回 true，句柄不存在返回 false。
     */
    static bool releaseCamera(const QString& handle);

    /**
     * @brief 检查句柄对应的相机是否已连接（VideoCapture 是否已打开）。
     */
    static bool isCameraConnected(const QString& handle);

    /**
     * @brief 尝试重新连接已断开的相机。
     * @param handle 句柄名称
     * @return 重连成功返回 true。
     */
    static bool reconnectCamera(const QString& handle);

    /**
     * @brief 列出当前所有已注册的相机句柄。
     */
    static QStringList listCameraHandles();

    /**
     * @brief 注册外部拥有的相机句柄（如 CameraView 已打开的相机）。
     *        注册后可被 GrabImageTool 等算子通过 acquireCamera 获取。
     *        外部句柄不会被 releaseCamera 释放，需由注册方管理生命周期。
     * @param handle 句柄名称（如 cam::DirectShow::0）
     * @param cap 已打开的 VideoCapture 指针（所有权归外部）
     * @param interfaceName 接口名称
     * @param deviceIndex 设备编号
     * @return 注册成功返回 true，句柄已存在返回 false
     */
    static bool registerExternalCamera(const QString& handle, cv::VideoCapture* cap,
                                       const QString& interfaceName, int deviceIndex);

    /**
     * @brief 注销外部拥有的相机句柄（不 delete cap，由外部管理）。
     * @param handle 句柄名称
     * @return 注销成功返回 true，句柄不存在返回 false
     */
    static bool unregisterExternalCamera(const QString& handle);

    /**
     * @brief 生成相机句柄名称。
     */
    static QString makeHandle(const QString& interfaceName, int deviceIndex);

    /**
     * @brief 枚举可用的 DirectShow 设备数量（Windows）。
     *        逐个尝试打开设备索引，统计可用的数量。
     * @return 可用设备数量。
     */
    static int enumerateDirectShowDevices();

private:
    // v5.3.2：打开设备后应用相机参数（分辨率/曝光/Gain 等）
    void applyCameraParameters(cv::VideoCapture* cap);

    /**
     * @brief 根据 interfaceName 选择 OpenCV VideoCapture 后端 API。
     */
    static int selectBackend(const QString& interfaceName);

    /**
     * @brief 将相机注册到全局表，返回是否注册成功。
     */
    static bool registerCamera(const QString& handle, cv::VideoCapture* cap,
                               const QString& interfaceName, int deviceIndex);

    // 相机注册表条目
    struct CameraEntry {
        cv::VideoCapture* capture = nullptr;
        QString interfaceName;
        int deviceIndex = 0;
        bool externalOwned = false;  // 外部拥有（如 CameraView），releaseCamera 时不 delete
    };

    static QHash<QString, CameraEntry> s_cameras;  // 全局相机表
    static QMutex s_cameraMutex;                    // 线程安全锁

    QString m_interfaceName = "DirectShow";  // 采集接口名称
    int m_deviceIndex = 0;                    // 设备编号

    // v5.3.2：相机参数（打开设备后通过 cap->set() 应用）
    int m_width = 0;                          // 分辨率宽（0=不设置，使用默认）
    int m_height = 0;                         // 分辨率高（0=不设置）
    double m_fps = 0.0;                       // 帧率（0=不设置）
    int m_exposure = 0;                       // 曝光时间（μs，0=不设置）
    double m_gain = -1.0;                     // Gain（<0=不设置）
    QString m_triggerMode = "Continuous";     // 触发模式：Continuous/Software/Hardware
    QString m_pixelFormat = "Mono8";          // 像素格式：Mono8/Mono16/BayerRG8/RGB8 等
    QString m_ip;                             // GigE 相机 IP 地址（空=非 GigE）
    bool m_useCameraConfig = false;           // 是否从全局 CameraConfig 读取默认参数
    bool m_useActiveCamera = false;           // 是否使用 CameraView 已打开的相机句柄
};

#endif // OPENFRAMEGRABBERTOOL_H
