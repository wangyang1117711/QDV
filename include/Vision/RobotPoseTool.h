#ifndef ROBOTPOSETOOL_H
#define ROBOTPOSETOOL_H

#include "Core/VisionTool.h"
#include <QMutex>
#include <QHash>
#include <QQueue>
#include <QElapsedTimer>
#include <chrono>

/**
 * @brief 机器人位姿数据结构
 *
 * 表示某一时刻机械臂末端在基坐标系下的位姿。
 * 平移 (x,y,z) 单位 mm，旋转 (rx,ry,rz) 为 Rodrigues 向量（弧度）。
 */
struct RobotPoseData {
    qint64 frameId = 0;        ///< 帧号（全局递增，由 RobotPoseTool 分配）
    qint64 timestampMs = 0;    ///< Unix 毫秒时间戳
    double x = 0.0, y = 0.0, z = 0.0;       ///< 平移分量（mm）
    double rx = 0.0, ry = 0.0, rz = 0.0;    ///< Rodrigues 旋转向量（弧度）
    bool valid = false;        ///< 是否有效
};

/**
 * @brief 机器人位姿算子（RobotPose）
 *
 * 从 TCP / 串口 / 文件 三种数据源读取机械臂当前位姿，
 * 写入全局 PoseBuffer 供 HandEyeCalibTool 按帧号配对使用。
 *
 * 数据源通过参数 sourceType 切换：
 *   - "tcp"    : TCP 连接机器人控制器，按 ASCII 协议读取位姿行
 *   - "serial" : 串口读取位姿行
 *   - "file"   : 轮询读取本地文件（外部进程持续追加）
 *
 * 位姿行格式：x y z rx ry rz（空格/逗号/分号分隔，6 个浮点数，弧度）
 * 可选第 7 列 frameId；若不提供则由算子内部全局递增分配。
 */
class RobotPoseTool : public QDV::VisionTool {
public:
    RobotPoseTool();
    ~RobotPoseTool() override;

    QString type() const override { return "RobotPose"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;

    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // ----- 参数访问器 -----
    void setSourceType(const QString& t) { m_sourceType = t; }
    QString sourceType() const { return m_sourceType; }

    void setHost(const QString& h) { m_host = h; }
    QString host() const { return m_host; }

    void setPort(int p) { m_port = p; }
    int port() const { return m_port; }

    void setSerialPort(const QString& p) { m_serialPort = p; }
    QString serialPort() const { return m_serialPort; }

    void setBaudRate(int b) { m_baudRate = b; }
    int baudRate() const { return m_baudRate; }

    void setFilePath(const QString& p) { m_filePath = p; }
    QString filePath() const { return m_filePath; }

    // ----- 全局 PoseBuffer（供 HandEyeCalibTool 使用） -----

    /**
     * @brief 向 PoseBuffer 推入一条位姿（由 RobotPoseTool::execute 调用）。
     * 线程安全。缓冲区满时丢弃最旧数据。
     */
    static void pushPose(const RobotPoseData& pose);

    /**
     * @brief 取出最旧的一条未配对位姿（FIFO），从缓冲区移除。
     * 线程安全。缓冲区为空返回 invalid pose（valid=false）。
     */
    static RobotPoseData popOldestPose();

    /**
     * @brief 按 frameId 取出指定位姿。若不存在返回 invalid pose。
     */
    static RobotPoseData takePose(qint64 frameId);

    /**
     * @brief 当前缓冲区中未配对位姿数量。
     */
    static int poseBufferSize();

    /**
     * @brief 清空 PoseBuffer。
     */
    static void clearPoseBuffer();

    /**
     * @brief 清理超时未配对的位姿（默认超时 5 秒）。
     * 由 HandEyeCalibTool 在配对前调用，防止旧位姿堆积。
     */
    static void purgeExpiredPoses(qint64 timeoutMs = 5000);

    /**
     * @brief 获取下一个全局帧号（递增）。
     */
    static qint64 nextFrameId();

private:
    QString m_sourceType = "file";   ///< tcp / serial / file
    QString m_host = "127.0.0.1";    ///< TCP 主机
    int     m_port = 30003;          ///< TCP 端口（UR 默认 30003）
    QString m_serialPort = "COM1";   ///< 串口名
    int     m_baudRate = 115200;     ///< 波特率
    QString m_filePath;              ///< 位姿文件路径

    // TCP / 串口 连接状态（延迟初始化）
    void* m_tcpSocket = nullptr;     ///< QTcpSocket*（避免头文件依赖）
    void* m_serial = nullptr;        ///< QSerialPort*（避免头文件依赖）
    qint64 m_lastFilePos = 0;        ///< 文件轮询上次读取位置

    // 内部方法
    bool readPoseFromTcp(RobotPoseData& out);
    bool readPoseFromSerial(RobotPoseData& out);
    bool readPoseFromFile(RobotPoseData& out);

    /// 解析一行文本为 RobotPoseData（格式：x y z rx ry rz [frameId]）
    static bool parsePoseLine(const QString& line, RobotPoseData& out);

    // 全局 PoseBuffer（静态成员）
    static QMutex s_poseMutex;
    static QQueue<RobotPoseData> s_poseBuffer;
    static qint64 s_nextFrameId;
    static constexpr int POSE_BUFFER_MAX = 100;  ///< 缓冲区上限
};

#endif // ROBOTPOSETOOL_H
