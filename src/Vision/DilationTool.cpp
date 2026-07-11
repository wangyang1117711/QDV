#include "DilationTool.h"
#include <opencv2/imgproc.hpp>

using namespace QDV;

namespace {
// 将字符串形状映射为 OpenCV 形状枚举
cv::MorphShapes toMorphShape(const QString& shape) {
    if (shape == "cross") return cv::MORPH_CROSS;
    if (shape == "ellipse") return cv::MORPH_ELLIPSE;
    return cv::MORPH_RECT; // 默认矩形
}

// 钳制核尺寸到 [1,31] 并保证为奇数
int clampKernelSize(int ksize) {
    if (ksize < 1) ksize = 1;
    if (ksize > 31) ksize = 31;
    if (ksize % 2 == 0) ksize += 1; // 偶数调整为奇数
    return ksize;
}
}

DilationTool::DilationTool() {
    m_name = "膨胀";
}

bool DilationTool::configure(const QJsonObject& params) {
    if (params.contains("kernelSize")) {
        m_kernelSize = clampKernelSize(params["kernelSize"].toInt());
    }
    if (params.contains("kernelShape")) {
        QString shape = params["kernelShape"].toString();
        // 仅接受合法形状，否则保持默认
        if (shape == "rect" || shape == "cross" || shape == "ellipse") {
            m_kernelShape = shape;
        }
    }
    return true;
}

bool DilationTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // 形态学在灰度图上进行
    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }

    int ksize = clampKernelSize(m_kernelSize);
    cv::Mat element = cv::getStructuringElement(toMorphShape(m_kernelShape),
                                                cv::Size(ksize, ksize));

    // 膨胀操作
    cv::Mat dst;
    cv::dilate(gray, dst, element);

    // 像素统计
    int nonZeroPixels = cv::countNonZero(dst);
    int totalPixels = dst.rows * dst.cols;

    result.ok = true;
    result.score = totalPixels > 0 ? static_cast<double>(nonZeroPixels) / totalPixels : 0.0;

    // overlayImage 必须为 BGR：准备 BGR 底图
    cv::Mat baseBgr;
    if (input.channels() == 3) {
        baseBgr = input.clone();
    } else {
        cv::cvtColor(input, baseBgr, cv::COLOR_GRAY2BGR);
    }
    result.overlayImage = baseBgr;

    // 灰度结果转 BGR，用绿色高亮非零区域，再与底图混合
    cv::Mat coloredDst;
    cv::cvtColor(dst, coloredDst, cv::COLOR_GRAY2BGR);
    coloredDst.setTo(cv::Scalar(0, 255, 0), dst > 0);
    cv::addWeighted(result.overlayImage, 0.7, coloredDst, 0.3, 0, result.overlayImage);

    // 记录结果数据
    result.data["kernelSize"] = ksize;
    result.data["kernelShape"] = m_kernelShape;
    result.data["nonZeroPixels"] = nonZeroPixels;
    result.data["totalPixels"] = totalPixels;
    result.data["width"] = dst.cols;
    result.data["height"] = dst.rows;

    return true;
}

QJsonObject DilationTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["kernelSize"] = m_kernelSize;
    obj["kernelShape"] = m_kernelShape;
    return obj;
}

bool DilationTool::deserialize(const QJsonObject& data) {
    // 使用 contains 检查，避免缺失字段覆盖为默认值
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("kernelSize")) {
        m_kernelSize = clampKernelSize(data["kernelSize"].toInt());
    }
    if (data.contains("kernelShape")) {
        QString shape = data["kernelShape"].toString();
        if (shape == "rect" || shape == "cross" || shape == "ellipse") {
            m_kernelShape = shape;
        }
    }
    return true;
}
