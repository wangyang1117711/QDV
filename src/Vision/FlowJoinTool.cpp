#include "FlowJoinTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>

using namespace QDV;

FlowJoinTool::FlowJoinTool() {
    m_name = "流程合并";
}

bool FlowJoinTool::configure(const QJsonObject& params) {
    if (params.contains("waitBranches")) {
        m_waitBranches = params["waitBranches"].toString();
    }
    if (params.contains("timeoutMs")) {
        int t = params["timeoutMs"].toInt();
        if (t > 0) m_timeoutMs = t;
    }
    m_params = params;
    return true;
}

bool FlowJoinTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        Logger::error("FlowJoinTool: input image is empty");
        result.ok = false;
        result.data["error"] = "Input image is empty";
        return false;
    }

    // FlowJoin 作为同步点，透传输入图像
    cv::Mat overlay;
    if (input.channels() == 3) {
        overlay = input.clone();
    } else if (input.channels() == 1) {
        cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
    } else {
        overlay = input.clone();
    }

    // 在 overlay 上标注 "JOIN" 文字
    cv::putText(overlay, "JOIN", cv::Point(10, 30),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);

    result.ok = true;
    result.overlayImage = overlay;
    result.data["waitBranches"] = m_waitBranches;
    result.data["timeoutMs"] = m_timeoutMs;
    result.ports["joined"] = true;
    result.ports["waitBranches"] = m_waitBranches;

    Logger::info(QString("FlowJoinTool: 等待分支 %1，超时 %2ms")
                  .arg(m_waitBranches).arg(m_timeoutMs));
    return true;
}

QJsonObject FlowJoinTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["waitBranches"] = m_waitBranches;
    obj["timeoutMs"] = m_timeoutMs;
    return obj;
}

bool FlowJoinTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;
    if (data.contains("waitBranches")) m_waitBranches = data["waitBranches"].toString();
    if (data.contains("timeoutMs")) {
        int t = data["timeoutMs"].toInt();
        if (t > 0) m_timeoutMs = t;
    }
    return true;
}

QList<PortDescriptor> FlowJoinTool::outputPorts() const {
    return {
        PortDescriptor{ "image",        "图像",      PortType::Image,  PortDirection::Out, "合并后透传的输入图像" },
        PortDescriptor{ "joined",       "已合并",    PortType::Bool,   PortDirection::Out, "所有等待分支是否已合并完成" },
        PortDescriptor{ "waitBranches", "等待分支",  PortType::String, PortDirection::Out, "等待的分支 ID 列表（逗号分隔）" },
    };
}

QList<PortDescriptor> FlowJoinTool::inputPorts() const {
    return {
        PortDescriptor{ "image", "图像", PortType::Image, PortDirection::In, "输入图像（透传到 overlay）" },
    };
}
