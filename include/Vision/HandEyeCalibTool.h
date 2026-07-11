#ifndef HANDEYECALIBTOOL_H
#define HANDEYECALIBTOOL_H

#include "VisionTool.h"
#include <QHash>
#include <QQueue>
#include <opencv2/core.hpp>

// 手眼标定：支持两种工作模式
//
// 1. 文件模式（传统）：配置 imagePaths + poseFilePath，一次性批量标定
// 2. 实时配对模式（v5.3 新增）：从上游图像端口接收实时帧，
//    从 RobotPoseTool 的全局 PoseBuffer 按 FIFO 取位姿进行帧号配对，
//    积累到 minPairs 组后自动触发标定。未配对帧缓存等待，
//    超时（pairTimeoutMs）后丢弃。
//
// 支持 eye_in_hand / eye_to_hand 双模式，输出 4x4 cam2gripper 齐次矩阵。
class HandEyeCalibTool : public QDV::VisionTool {
public:
    HandEyeCalibTool();

    QString type() const override { return "HandEyeCalib"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // v5.3：重置配对缓存（开始新一轮标定时调用）
    void resetPairs();

    // v5.3：当前已配对的帧数
    int pairedCount() const;

private:
    QString m_mode = "eye_in_hand"; // 模式：eye_in_hand / eye_to_hand
    int m_boardW = 9;               // 棋盘格内角点列数（>=2）
    int m_boardH = 6;               // 棋盘格内角点行数（>=2）
    double m_squareSize = 25.0;     // 棋盘格方格边长（mm，>0）
    QString m_imagePaths;           // 标定图像路径，分号 ';' 分隔（文件模式）
    QString m_poseFilePath;         // 机械臂位姿文件路径（文件模式）

    // v5.3：实时配对模式参数
    int m_minPairs = 8;             // 触发标定的最小配对数（>=3）
    int m_pairTimeoutMs = 5000;     // 未配对帧超时（ms）
    int m_maxCacheSize = 100;       // 配对缓存上限

    // v5.3：已配对的（图像角点 + 位姿）对
    struct CalibPair {
        std::vector<cv::Point2f> imageCorners;   // 图像角点
        cv::Mat R_gripper2base;                   // 旋转矩阵
        cv::Mat t_gripper2base;                   // 平移向量
        qint64 frameId = 0;                       // 帧号
        qint64 timestampMs = 0;                   // 时间戳
    };
    QList<CalibPair> m_pairs;       // 已配对的标定数据

    // v5.3：待配对的图像帧缓存（位姿未到达时暂存）
    struct PendingFrame {
        cv::Mat image;              // 图像帧
        std::vector<cv::Point2f> corners;  // 检测到的角点
        qint64 frameId = 0;
        qint64 timestampMs = 0;
        bool cornersFound = false;
    };
    QQueue<PendingFrame> m_pendingFrames;

    // v5.3：执行实时配对标定（从上游帧 + PoseBuffer）
    bool executeRealtimeCalib(const cv::Mat& input, ToolResult& result);

    // v5.3：执行文件模式标定（原有逻辑）
    bool executeFileCalib(ToolResult& result);

    // v5.3：检测棋盘格角点
    bool detectCorners(const cv::Mat& img, std::vector<cv::Point2f>& corners) const;

    // v5.3：执行标定计算（共有逻辑）
    bool runCalibration(const QList<CalibPair>& pairs, cv::Size imageSize,
                        ToolResult& result) const;
};

#endif // HANDEYECALIBTOOL_H
