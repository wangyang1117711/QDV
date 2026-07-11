#include "WatershedTool.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>
#include <algorithm>

using namespace QDV;

WatershedTool::WatershedTool() {
    m_name = "分水岭分割";
}

// 将核大小钳制为 [1, 21] 范围内的奇数
static int clampKernelSize(int ks) {
    if (ks < 1) ks = 1;
    if (ks > 21) ks = 21;
    if (ks % 2 == 0) {
        ks += 1;
        if (ks > 21) ks = 21;
    }
    return ks;
}

bool WatershedTool::configure(const QJsonObject& params) {
    if (params.contains("threshold")) m_threshold = params["threshold"].toDouble();
    if (params.contains("kernelSize")) m_kernelSize = params["kernelSize"].toInt();

    // 钳制阈值与核尺寸
    m_threshold = std::clamp(m_threshold, 0.0, 255.0);
    m_kernelSize = clampKernelSize(m_kernelSize);

    return true;
}

bool WatershedTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // 分水岭算法要求输入为 3 通道图像
    cv::Mat colorInput;
    if (input.channels() == 1) {
        cv::cvtColor(input, colorInput, cv::COLOR_GRAY2BGR);
    } else {
        colorInput = input.clone();
    }

    cv::Mat gray;
    cv::cvtColor(colorInput, gray, cv::COLOR_BGR2GRAY);

    // 1. 二值化得到前景区域
    cv::Mat binary;
    cv::threshold(gray, binary, m_threshold, 255, cv::THRESH_BINARY);
    if (cv::countNonZero(binary) == 0) {
        result.ok = false;
        result.data["error"] = QStringLiteral("二值化后前景为空");
        return false;
    }

    // 2. 形态学开运算去噪
    cv::Mat kernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE, cv::Size(m_kernelSize, m_kernelSize));
    cv::Mat opening;
    cv::morphologyEx(binary, opening, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), 2);

    // 3. 确定背景：对开运算结果膨胀
    cv::Mat sureBg;
    cv::dilate(opening, sureBg, kernel, cv::Point(-1, -1), 3);

    // 4. 确定前景：距离变换 + 阈值
    cv::Mat distTransform;
    cv::distanceTransform(opening, distTransform, cv::DIST_L2, 3);
    double maxDist = 0.0;
    cv::minMaxLoc(distTransform, nullptr, &maxDist);
    cv::Mat sureFg;
    double fgThresh = (maxDist > 0.0) ? (0.5 * maxDist) : 0.0;
    cv::threshold(distTransform, sureFg, fgThresh, 255, cv::THRESH_BINARY);
    sureFg.convertTo(sureFg, CV_8U);

    // 5. 未知区域 = 确定背景 - 确定前景
    cv::Mat unknown;
    cv::subtract(sureBg, sureFg, unknown);

    // 6. 连通组件标记确定前景，背景标签为 0
    cv::Mat markers;
    int numLabels = cv::connectedComponents(sureFg, markers, 8);
    if (numLabels <= 1) {
        result.ok = false;
        result.data["error"] = QStringLiteral("未找到确定前景区域");
        return false;
    }
    // watershed 约定：背景为 1，未知区域为 0，故整体 +1
    markers = markers + 1;
    markers.setTo(0, unknown == 255);

    // 7. 执行分水岭
    cv::watershed(colorInput, markers);

    // 8. 按标记着色：边界红色，各区域伪彩色半透明叠加
    cv::Mat overlay = colorInput.clone();
    for (int y = 0; y < markers.rows; ++y) {
        const int* pMarker = markers.ptr<int>(y);
        cv::Vec3b* pOverlay = overlay.ptr<cv::Vec3b>(y);
        for (int x = 0; x < markers.cols; ++x) {
            int label = pMarker[x];
            if (label == -1) {
                // 分水岭边界标记为红色
                pOverlay[x] = cv::Vec3b(0, 0, 255);
            } else if (label > 1) {
                // 各区域用基于标签的伪彩色，与原图半透明混合
                cv::Vec3b color = cv::Vec3b(
                    static_cast<uchar>((label * 67) % 256),
                    static_cast<uchar>((label * 113) % 256),
                    static_cast<uchar>((label * 197) % 256));
                cv::Vec3b orig = pOverlay[x];
                pOverlay[x] = cv::Vec3b(
                    static_cast<uchar>(orig[0] * 0.5 + color[0] * 0.5),
                    static_cast<uchar>(orig[1] * 0.5 + color[1] * 0.5),
                    static_cast<uchar>(orig[2] * 0.5 + color[2] * 0.5));
            }
        }
    }

    result.overlayImage = overlay;
    result.ok = true;
    // 实际分割区域数 = 连通组件数 - 1（去除背景）
    result.data["regionCount"] = (numLabels - 1);
    result.data["threshold"] = m_threshold;
    result.data["kernelSize"] = m_kernelSize;

    return true;
}

QJsonObject WatershedTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["threshold"] = m_threshold;
    obj["kernelSize"] = m_kernelSize;
    return obj;
}

bool WatershedTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("threshold")) m_threshold = data["threshold"].toDouble();
    if (data.contains("kernelSize")) m_kernelSize = data["kernelSize"].toInt();

    // 反序列化后同样钳制，保证状态一致
    m_threshold = std::clamp(m_threshold, 0.0, 255.0);
    m_kernelSize = clampKernelSize(m_kernelSize);

    return true;
}
