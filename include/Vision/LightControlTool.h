#pragma once

#include "Core/VisionTool.h"
#include "Communication/IOController.h"
#include <QString>

// 光源控制算子（P0-6e）
// 通过 IOController 控制外部光源通道（持续/频闪/关闭），
// 或对 serial/tcp 设备构造控制指令字符串（实际发送由外部通信模块处理）。
// 无硬件连接时记录控制指令到 result.data["command"] 并返回 ok=true（优雅降级）。
class LightControlTool : public QDV::VisionTool {
public:
    LightControlTool();
    ~LightControlTool() override = default;

    QString type() const override { return "LightControl"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports：声明输入/输出端口元数据
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

    // 依赖注入：注入已初始化的 IOController（不拥有所有权）
    void setIOController(IOController* ctrl) { m_io = ctrl; }
    IOController* ioController() const { return m_io; }

private:
    // ----- 参数 -----
    int     m_channel         = 0;            // 光源通道号
    int     m_intensity       = 255;          // 亮度 0-255
    QString m_mode            = "continuous"; // continuous/strobe/off
    int     m_strobePeriodMs  = 100;          // 频闪周期（ms）
    double  m_strobeDutyCycle = 0.5;          // 频闪占空比（0-1）
    int     m_triggerDelay    = 0;            // 触发延迟（ms）
    QString m_deviceType      = "io";         // io/serial/tcp
    QString m_devicePort      = "";           // 设备端口/地址

    IOController* m_io = nullptr;             // 注入的 IO 控制器（不拥有）

    // 构造控制指令字符串（供 serial/tcp 发送或日志记录）
    QString buildCommand() const;
};
