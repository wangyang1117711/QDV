#pragma once

#include "VisionTool.h"
#include <QString>
#include <opencv2/core.hpp>

// 相机标定算子（P0-5）
//
// 功能：张正友法相机内参标定（cv::calibrateCamera），输出标定文件供 UnitConvert 使用。
//
// 工作流程：
// 1. 从 imagePaths（分号 ';' 分隔）加载多张棋盘格标定图像
// 2. 对每张图用 cv::findChessboardCorners 检测内角点
// 3. 用 cv::cornerSubPix 亚像素精细化
// 4. 积累 >=3 张有效图像后调用 cv::calibrateCamera 标定内参 + 畸变
// 5. 用 cv::projectPoints 逐图对比计算重投影误差
// 6. 输出 fx/fy/cx/cy/重投影误差，并写入 outputFile（.yml，cv::FileStorage）
//
// overlayImage：在输入图像（或首张标定图）上绘制检测到的角点（cv::drawChessboardCorners）
class CameraCalibTool : public QDV::VisionTool {
public:
    CameraCalibTool();

    QString type() const override { return "CameraCalib"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports：声明端口供连线期类型校验与 QML 端口可视化
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

private:
    int     m_boardW     = 9;        // 棋盘格内角点列数（>=2）
    int     m_boardH     = 6;        // 棋盘格内角点行数（>=2）
    double  m_squareSize = 25.0;     // 棋盘格方格边长（mm，>0）
    QString m_imagePaths;            // 标定图像路径，分号 ';' 分隔
    QString m_outputFile;            // 标定结果输出文件路径（.yml）
    int     m_maxImages = 20;        // 最大处理图像数
};
