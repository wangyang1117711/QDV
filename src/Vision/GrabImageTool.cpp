#include "Vision/GrabImageTool.h"
#include "Vision/OpenFramegrabberTool.h"
#include "Core/Logger.h"
#include <QDateTime>
#include <QJsonArray>
#include <QThread>

using namespace QDV;

GrabImageTool::GrabImageTool() {
    m_name = "采集图像";
}

GrabImageTool::~GrabImageTool() = default;

// ----- 参数配置 -----
bool GrabImageTool::configure(const QJsonObject& params) {
    if (params.contains("acqHandle")) {
        setAcqHandle(params["acqHandle"].toString());
    }
    if (params.contains("timeout")) {
        int ms = params["timeout"].toInt(5000);
        if (ms < 100) ms = 100;       // 最小 100ms
        if (ms > 60000) ms = 60000;   // 最大 60s
        setTimeout(ms);
    }
    if (params.contains("triggerMode")) {
        setTriggerMode(params["triggerMode"].toString());
    }
    if (params.contains("triggerInterval")) {
        int ms = params["triggerInterval"].toInt(1000);
        if (ms < 10) ms = 10;
        setTriggerInterval(ms);
    }
    m_params = params;
    return true;
}

// ----- 执行：从相机采集一帧 -----
bool GrabImageTool::execute(const cv::Mat& input, ToolResult& result) {
    Q_UNUSED(input);

    // v5.3.7：acqHandle 为空时自动查找已注册的相机句柄
    // 用户无需手动填写 cam::DirectShow::0 这样的句柄名
    QString handle = m_acqHandle;
    if (handle.isEmpty()) {
        QStringList handles = OpenFramegrabberTool::listCameraHandles();
        for (const QString& h : handles) {
            if (OpenFramegrabberTool::isCameraConnected(h)) {
                handle = h;
                break;
            }
        }
        if (handle.isEmpty()) {
            Logger::warn("GrabImageTool: no acqHandle configured and no connected camera found");
            result.ok = false;
            result.data["error"] = "No acqHandle configured and no connected camera found";
            result.data["hint"] = "Please run OpenFramegrabber first to open the camera";
            return false;
        }
        Logger::info(QString("GrabImageTool: auto-selected camera handle: %1").arg(handle));
    }

    // 定时触发模式：未到间隔时间则跳过本次采集
    if (m_triggerMode == "timer" && !timerTriggerReady()) {
        result.ok = false;
        result.data["error"] = "Timer trigger interval not reached";
        result.data["skipped"] = true;
        return false;
    }

    // 获取相机
    cv::VideoCapture* cap = OpenFramegrabberTool::acquireCamera(handle);
    if (!cap) {
        Logger::error(QString("GrabImageTool: camera handle not found: %1").arg(handle));
        result.ok = false;
        result.data["error"] = QString("Camera handle not found: %1").arg(handle);
        result.data["hint"] = "Please run OpenFramegrabber first to open the camera";
        return false;
    }

    // 检查相机连接状态，断开则尝试重连
    if (!cap->isOpened()) {
        Logger::warn(QString("GrabImageTool: camera disconnected, attempting reconnect: %1")
                         .arg(handle));
        if (!OpenFramegrabberTool::reconnectCamera(handle)) {
            Logger::error(QString("GrabImageTool: reconnect failed: %1").arg(handle));
            result.ok = false;
            result.data["error"] = QString("Camera disconnected and reconnect failed: %1")
                                       .arg(handle);
            return false;
        }
        // 重连后重新获取指针（注册表条目可能更新）
        cap = OpenFramegrabberTool::acquireCamera(handle);
        if (!cap || !cap->isOpened()) {
            result.ok = false;
            result.data["error"] = "Camera reconnected but still not accessible";
            return false;
        }
    }

    // 采集帧（带超时与重试）
    cv::Mat frame;
    if (!grabFrameWithRetry(cap, frame)) {
        result.ok = false;
        result.data["error"] = QString("Frame grab failed (timeout=%1ms)").arg(m_timeout);
        result.data["acqHandle"] = handle;
        return false;
    }

    if (frame.empty()) {
        Logger::error(QString("GrabImageTool: empty frame from: %1").arg(handle));
        result.ok = false;
        result.data["error"] = "Captured frame is empty";
        result.data["acqHandle"] = handle;
        return false;
    }

    // 帧号自增
    ++m_frameNumber;
    // 记录时间戳（Unix 纪元毫秒）
    qint64 timestampMs = QDateTime::currentMSecsSinceEpoch();
    m_lastGrabMs = timestampMs;

    // 填充结果
    result.ok = true;
    result.score = 1.0;
    result.overlayImage = frame.clone();

    result.data["acqHandle"] = handle;
    result.data["frameNumber"] = static_cast<qint64>(m_frameNumber);
    result.data["timestamp"] = timestampMs;
    result.data["width"] = frame.cols;
    result.data["height"] = frame.rows;
    result.data["channels"] = frame.channels();
    result.data["triggerMode"] = m_triggerMode;

    m_results["lastFrameNumber"] = static_cast<qint64>(m_frameNumber);
    m_results["lastTimestamp"] = timestampMs;

    Logger::info(QString("GrabImageTool: frame #%1 grabbed (%2x%3, %4ch) from %5")
                     .arg(m_frameNumber)
                     .arg(frame.cols)
                     .arg(frame.rows)
                     .arg(frame.channels())
                     .arg(m_acqHandle));

    return true;
}

// ----- 序列化/反序列化 -----
QJsonObject GrabImageTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["acqHandle"] = m_acqHandle;
    obj["timeout"] = m_timeout;
    obj["triggerMode"] = m_triggerMode;
    obj["triggerInterval"] = m_triggerInterval;
    return obj;
}

bool GrabImageTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;
    return configure(data);
}

// ----- 定时触发判断 -----
bool GrabImageTool::timerTriggerReady() {
    if (!m_timerStarted) {
        m_timer.start();
        m_timerStarted = true;
        return true;  // 首次触发立即采集
    }

    qint64 elapsed = m_timer.elapsed();
    if (elapsed >= m_triggerInterval) {
        m_timer.restart();
        return true;
    }
    return false;
}

// ----- 采集帧（带超时与重试） -----
bool GrabImageTool::grabFrameWithRetry(cv::VideoCapture* cap, cv::Mat& frame) {
    QElapsedTimer timer;
    timer.start();

    // 轮询 grab()，超时则放弃
    while (timer.elapsed() < m_timeout) {
        if (cap->grab()) {
            if (cap->retrieve(frame)) {
                return true;
            }
        }

        // grab 失败可能是设备断开
        if (!cap->isOpened()) {
            Logger::warn(QString("GrabImageTool: device disconnected during grab, retrying..."));
            // 尝试重连一次
            if (OpenFramegrabberTool::reconnectCamera(m_acqHandle)) {
                cap = OpenFramegrabberTool::acquireCamera(m_acqHandle);
                if (!cap || !cap->isOpened()) {
                    return false;
                }
                // 重连后立即尝试采集
                continue;
            } else {
                return false;
            }
        }

        // 短暂等待后重试（避免 CPU 空转）
        QThread::msleep(10);
    }

    return false;  // 超时
}
