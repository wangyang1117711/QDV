#pragma once

#include "VisionTool.h"
#include <QString>
#include <opencv2/core.hpp>

// 单位换算算子（P0-3）
//
// 功能：消费相机标定文件（内参 + 畸变），将像素测量结果换算为物理单位（mm），
//       或反向将 mm 转换为像素。与 HandEyeCalibTool / CameraCalibTool 共用标定数据。
//
// 工作模式：
// 1. 标定文件模式：从 calibFile 加载 fx, fy, cx, cy + 工作距离（workDistance），
//    推算像素当量 pixelScale = workDistance / mean(fx, fy)（mm/px）。
// 2. 手动模式：标定文件缺失或加载失败时，回退到用户配置的 m_pixelScale（mm/px）。
//
// 输入类型 inputType：
// - "distance"：一维长度（像素 ↔ mm）
// - "area"    ：二维面积（像素² ↔ mm²）
//
// 输出单位 outputUnit：
// - "mm"：输入视为像素，输出 mm（距离/面积）
// - "px"：输入视为 mm，输出像素（距离/面积）
class UnitConvertTool : public QDV::VisionTool {
public:
    UnitConvertTool();

    QString type() const override { return "UnitConvert"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports：声明端口供连线期类型校验与 QML 端口可视化
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

private:
    // 从标定文件加载 fx, fy, cx, cy 和工作距离，推算 pixelScale（mm/px）
    // 成功返回 true 并写出 fx/fy/cx/cy/workDistance/scale；
    // 失败返回 false（调用方回退到手动 m_pixelScale）
    bool loadCalibFile(double& fx, double& fy, double& cx, double& cy,
                       double& workDistance, double& scale) const;

    QString m_calibFile;                 // 相机标定文件路径（.yml/.json）
    double  m_inputValue  = 0.0;         // 输入数值（像素或 mm）
    QString m_inputType   = "distance";  // 输入类型：distance / area
    QString m_outputUnit  = "mm";        // 输出单位：mm / px
    double  m_pixelScale  = 1.0;         // 手动像素当量（mm/px，标定文件缺失时使用）
};
