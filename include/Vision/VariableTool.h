#pragma once

#include "VisionTool.h"
#include "Core/VariableManager.h"
#include <QString>
#include <opencv2/core.hpp>

// 变量运算算子（P1-4c）
//
// 功能：数值/字符串运算 + 格式化输出。支持基本算术、字符串拼接、格式化输出。
//       用于在算子链中做轻量级数值/字符串处理（如单位换算后的阈值比较、
//       多结果拼接为文件名、按格式生成报告字段等）。
//
// 运算类型 operation：
// - "add"    : operand1 + operand2（数值加法）
// - "sub"    : operand1 - operand2
// - "mul"    : operand1 * operand2
// - "div"    : operand1 / operand2（除零返回 ok=false）
// - "mod"    : fmod(operand1, operand2)
// - "concat" : 字符串拼接 operand1 + operand2
// - "format" : 按 format 模板格式化（operand1 为主参数，operand2 为附加参数）
//
// v2.7.0 新增运算（接入 VariableManager 全局变量）：
// - "get"    : 读取全局变量（operand1=变量名），结果写入 result
// - "set"    : 写入全局变量（operand1=变量名, operand2=值，支持数值自动转换）
// - "define" : 创建全局变量（operand1=变量名, operand2=类型, format=初始值）
//              类型支持：int/double/string/bool/roi/region/points
// - "delete" : 删除全局变量（operand1=变量名）
//
// 输出端口：
// - result (Any) : 运算结果（数值或字符串，取决于 operation）
// - 同时以 outputVar 名称为 key 写入 ports（便于下游按变量名引用）
class VariableTool : public QDV::VisionTool {
public:
    VariableTool();

    QString type() const override { return "Variable"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

    /// v2.7.0：注入 VariableManager（由 EditViewBridge 在构建算子链时调用）
    void setVariableManager(QDV::VariableManager* vm) { m_variableManager = vm; }

private:
    // 尝试将字符串解析为 double；失败返回 false
    static bool tryParseDouble(const QString& s, double& out);

    QString m_operation  = "add";     // 运算类型
    QString m_operand1   = "0";       // 操作数1（数字或字符串）
    QString m_operand2   = "0";       // 操作数2
    QString m_format     = "%.2f";    // 格式化模板（format 模式用）
    QString m_outputVar  = "result";  // 输出变量名

    QDV::VariableManager* m_variableManager = nullptr;  ///< v2.7.0：全局变量管理器
};
