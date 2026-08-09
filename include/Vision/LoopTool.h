#pragma once

#include "VisionTool.h"
#include <QString>
#include <QVariantList>
#include <opencv2/core.hpp>

// 循环遍历算子（P1-4a）
//
// 功能：多 ROI / 多目标遍历。对输入图像按多个 ROI 区域循环执行子算子链。
//       本算子负责输出 ROI 列表和当前索引；实际子链执行由 ToolChainExecutor 编排。
//
// 遍历模式 mode：
// - "grid"      : 将输入图像按 gridCols×gridRows 切分（含 overlap），生成 ROI 列表
// - "list"      : 解析 roiList 字符串 "x,y,w,h;x,y,w,h;..."
// - "detection" : 按检测结果生成 ROI（当前无检测结果输入，降级为 grid）
//
// v2.7.0 新增模式：
// - "count"：计数循环（固定次数 N 次循环，输出 count 个占位 ROI）
// - "while"：条件循环（满足条件持续循环，直到条件不成立退出）
//            while 模式的条件通过 whileCondition 参数配置（如 "count>5"）
//
// 输出端口：
// - roiList (Points) : ROI 列表，每个元素 {x,y,w,h}
// - count   (Number) : ROI 总数
// - currentIndex（额外写入 data，不单独开端口，避免与 ToolChainExecutor 索引语义冲突）
class LoopTool : public QDV::VisionTool {
public:
    LoopTool();

    QString type() const override { return "Loop"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

private:
    // 将网格切分参数应用于图像尺寸，生成 ROI 列表
    QVariantList buildGridRois(int imgW, int imgH) const;

    // 解析 "x,y,w,h;x,y,w,h;..." 字符串为 ROI 列表
    static QVariantList parseRoiListString(const QString& text);

    // 在 overlay 上绘制 ROI 框（黄色）+ 索引文字
    static void drawRois(cv::Mat& overlay, const QVariantList& rois);

    QString m_mode         = "grid";   // 遍历模式：grid / list / detection / count / while
    int     m_gridCols     = 3;        // 网格列数
    int     m_gridRows     = 3;        // 网格行数
    int     m_gridOverlap  = 0;        // 网格重叠像素
    QString m_roiList      = "";       // 自定义 ROI 列表字符串
    int     m_startIndex   = 0;        // 起始索引
    int     m_maxIterations = 100;     // 最大迭代数（保护性上限）

    int     m_count               = 1;     ///< v2.7.0：count 模式的循环次数
    QString m_whileCondition      = "";    ///< v2.7.0：while 模式的条件表达式
    int     m_maxWhileIterations  = 1000;  ///< v2.7.0：while 模式最大迭代数（防死循环）
};
