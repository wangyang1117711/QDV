#include "Vision/LightControlTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

using namespace QDV;

LightControlTool::LightControlTool() {
    m_name = "光源控制";
}

// ---------------------------------------------------------------
// 参数配置：钳制到合理范围
// ---------------------------------------------------------------
bool LightControlTool::configure(const QJsonObject& params) {
    if (params.contains("channel")) {
        m_channel = std::clamp(params["channel"].toInt(0), 0, 63);
    }
    if (params.contains("intensity")) {
        m_intensity = std::clamp(params["intensity"].toInt(255), 0, 255);
    }
    if (params.contains("mode")) {
        m_mode = params["mode"].toString("continuous");
    }
    if (params.contains("strobePeriodMs")) {
        m_strobePeriodMs = std::clamp(params["strobePeriodMs"].toInt(100), 1, 100000);
    }
    if (params.contains("strobeDutyCycle")) {
        m_strobeDutyCycle = std::clamp(params["strobeDutyCycle"].toDouble(0.5), 0.0, 1.0);
    }
    if (params.contains("triggerDelay")) {
        m_triggerDelay = std::clamp(params["triggerDelay"].toInt(0), 0, 100000);
    }
    if (params.contains("deviceType")) {
        m_deviceType = params["deviceType"].toString("io");
    }
    if (params.contains("devicePort")) {
        m_devicePort = params["devicePort"].toString();
    }
    m_params = params;
    return true;
}

// ---------------------------------------------------------------
// 构造控制指令字符串（供 serial/tcp 发送或日志记录）
// ---------------------------------------------------------------
QString LightControlTool::buildCommand() const {
    QString cmd;
    if (m_mode == "off") {
        cmd = QString("SET_LIGHT %1 OFF").arg(m_channel);
    } else if (m_mode == "strobe") {
        // 计算频闪高电平持续时间（占空比 × 周期）
        const int onMs  = std::max(1, static_cast<int>(m_strobePeriodMs * m_strobeDutyCycle));
        const int offMs = std::max(0, m_strobePeriodMs - onMs);
        cmd = QString("SET_LIGHT %1 STROBE intensity=%2 period=%3ms on=%4ms off=%5ms delay=%6ms")
                  .arg(m_channel).arg(m_intensity)
                  .arg(m_strobePeriodMs).arg(onMs).arg(offMs).arg(m_triggerDelay);
    } else {
        // continuous（持续）
        cmd = QString("SET_LIGHT %1 CONTINUOUS intensity=%2 delay=%3ms")
                  .arg(m_channel).arg(m_intensity).arg(m_triggerDelay);
    }
    return cmd;
}

// ---------------------------------------------------------------
// execute：根据 deviceType 选择控制方式，无硬件时记录指令并返回 ok
// 注意：QTimer 无法在 execute 同步循环中使用，频闪的实际循环由硬件或外部控制，
//       此处仅触发一次脉冲（strobe 模式）或记录参数。
// ---------------------------------------------------------------
bool LightControlTool::execute(const cv::Mat& input, ToolResult& result) {
    // overlay 透传 input（光源控制不改变图像）
    if (!input.empty()) {
        if (input.channels() == 1) {
            cv::cvtColor(input, result.overlayImage, cv::COLOR_GRAY2BGR);
        } else {
            result.overlayImage = input.clone();
        }
    }

    try {
        const QString command = buildCommand();
        bool success = false;       // 实际硬件执行是否成功
        QString warning;            // 降级提示

        if (m_deviceType == "io") {
            // IO 控制：复用 IOController
            if (m_io) {
                // 设置通道为输出模式（忽略失败，继续尝试 setOutput/sendPulse）
                m_io->setLineMode(m_channel, IOController::OutputMode);
                if (m_mode == "off") {
                    // 关闭通道
                    success = m_io->setOutput(m_channel, false);
                } else if (m_mode == "strobe") {
                    // 频闪：触发一次脉冲（高电平持续 onMs）
                    // 持续频闪需硬件自激或外部定时器，此处仅触发单次
                    const int onMs = std::max(1, static_cast<int>(m_strobePeriodMs * m_strobeDutyCycle));
                    success = m_io->sendPulse(m_channel, onMs);
                } else {
                    // continuous：intensity>0 视为开（IOController 仅 bool 输出，无模拟量）
                    success = m_io->setOutput(m_channel, m_intensity > 0);
                }
                if (!success) {
                    warning = "IO 控制执行失败，已记录指令";
                }
            } else {
                // 无硬件连接：记录指令，优雅降级
                warning = "未连接硬件，仅记录指令";
            }
        } else if (m_deviceType == "serial" || m_deviceType == "tcp") {
            // serial/tcp：仅构造指令字符串，实际发送由外部通信模块处理
            // IOController 不支持 serial/tcp，此处记录指令
            warning = (m_deviceType == "serial" || m_deviceType == "tcp")
                          ? "serial/tcp 设备需外部通信模块，仅记录指令"
                          : "未知设备类型，仅记录指令";
        } else {
            warning = "未知设备类型 " + m_deviceType + "，仅记录指令";
        }

        // 填充 result.data
        result.data["command"]    = command;
        result.data["channel"]    = m_channel;
        result.data["intensity"]  = m_intensity;
        result.data["mode"]       = m_mode;
        result.data["deviceType"] = m_deviceType;
        result.data["devicePort"] = m_devicePort;
        if (!warning.isEmpty()) {
            result.data["warning"] = warning;
        }

        // typed ports 输出
        result.ports["channel"]   = m_channel;
        result.ports["intensity"] = m_intensity;
        result.ports["mode"]      = m_mode;
        result.ports["success"]   = success;
        result.ports["command"]   = command;

        // 算子执行成功（即使无硬件，也记录了指令）
        result.ok = true;
        result.score = success ? 1.0 : 0.0;

        // 记录最近执行结果
        m_results["lastSuccess"] = success;
        m_results["lastCommand"] = command;
        return true;
    } catch (const std::exception& e) {
        Logger::error(QString("LightControlTool: 异常: %1").arg(e.what()));
        result.ok = false;
        result.data["error"] = e.what();
        return false;
    }
}

// ---------------------------------------------------------------
// typed ports 声明
// ---------------------------------------------------------------
QList<PortDescriptor> LightControlTool::outputPorts() const {
    return {
        { QStringLiteral("success"),  QStringLiteral("执行成功"), PortType::Bool,   PortDirection::Out,
          QStringLiteral("实际硬件执行是否成功") },
        { QStringLiteral("command"),  QStringLiteral("控制指令"), PortType::String, PortDirection::Out,
          QStringLiteral("生成的控制指令字符串") },
    };
}

QList<PortDescriptor> LightControlTool::inputPorts() const {
    return {
        { QStringLiteral("image"), QStringLiteral("图像"), PortType::Image, PortDirection::In,
          QStringLiteral("输入图像（透传，光源控制不改变图像）") },
    };
}

// ---------------------------------------------------------------
// 序列化/反序列化
// ---------------------------------------------------------------
QJsonObject LightControlTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["channel"]         = m_channel;
    obj["intensity"]       = m_intensity;
    obj["mode"]            = m_mode;
    obj["strobePeriodMs"]  = m_strobePeriodMs;
    obj["strobeDutyCycle"] = m_strobeDutyCycle;
    obj["triggerDelay"]    = m_triggerDelay;
    obj["deviceType"]      = m_deviceType;
    obj["devicePort"]      = m_devicePort;
    return obj;
}

bool LightControlTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;
    return configure(data);
}
