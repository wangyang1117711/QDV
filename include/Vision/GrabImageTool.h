#ifndef GRABIMAGETOOL_H
#define GRABIMAGETOOL_H

#include "Core/VisionTool.h"
#include <QElapsedTimer>
#include <opencv2/videoio.hpp>

/**
 * @brief 采集图像算子（GrabImage）
 *
 * 通过 OpenFramegrabberTool 打开的相机句柄采集单帧图像。
 * 支持多种触发模式：单次 / 连续 / 定时 / 外部触发。
 * 帧号自增、时间戳记录，超时与断开重连错误处理。
 *
 * 采集源由 acqHandle 指定，对应 OpenFramegrabberTool 注册的相机句柄。
 */
class GrabImageTool : public QDV::VisionTool {
public:
    GrabImageTool();
    ~GrabImageTool() override;

    QString type() const override { return "GrabImage"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;

    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // ----- 参数访问器 -----
    void setAcqHandle(const QString& handle) { m_acqHandle = handle; }
    QString acqHandle() const { return m_acqHandle; }

    void setTimeout(int ms) { m_timeout = ms; }
    int timeout() const { return m_timeout; }

    void setTriggerMode(const QString& mode) { m_triggerMode = mode; }
    QString triggerMode() const { return m_triggerMode; }

    void setTriggerInterval(int ms) { m_triggerInterval = ms; }
    int triggerInterval() const { return m_triggerInterval; }

    // 帧号管理
    quint64 frameNumber() const { return m_frameNumber; }
    void resetFrameNumber() { m_frameNumber = 0; }

private:
    /**
     * @brief 定时触发模式下，判断是否到达采集时刻。
     * @return 到达间隔返回 true，否则返回 false。
     */
    bool timerTriggerReady();

    /**
     * @brief 从相机采集一帧，处理超时与断开重连。
     * @param cap 相机 VideoCapture 指针
     * @param frame 输出帧
     * @return 采集成功返回 true。
     */
    bool grabFrameWithRetry(cv::VideoCapture* cap, cv::Mat& frame);

    QString m_acqHandle;             // 相机句柄（来自 OpenFramegrabberTool）
    int m_timeout = 5000;            // 采集超时（ms）
    QString m_triggerMode = "single";    // 触发模式：single/continuous/timer/external
    int m_triggerInterval = 1000;    // 定时采集间隔（ms）

    quint64 m_frameNumber = 0;       // 帧号计数器（自增）
    QElapsedTimer m_timer;           // 定时触发计时器
    bool m_timerStarted = false;     // 计时器是否已启动
    qint64 m_lastGrabMs = 0;         // 上次采集的时间戳（ms）
};

#endif // GRABIMAGETOOL_H
