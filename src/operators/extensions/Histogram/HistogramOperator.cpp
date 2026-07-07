#include "HistogramOperator.h"
#include <QJsonArray>
#include <vector>

namespace QDV {

// ============================================================
// configure：解析 mode/bins/rangeMin/rangeMax
// ============================================================
bool HistogramOperator::configure(const QJsonObject& params) {
    if (params.contains("mode")) {
        QString m = params["mode"].toString();
        // 只接受合法模式
        if (m == "gray" || m == "color" || m == "equalize") {
            m_mode = m;
        }
    }
    if (params.contains("bins")) {
        int b = params["bins"].toInt(256);
        // RT-013: bins 超范围 → 使用默认 256
        if (b <= 0 || b > 1024) {
            m_bins = 256;
        } else {
            m_bins = b;
        }
    }
    if (params.contains("rangeMin")) {
        m_rangeMin = static_cast<float>(params["rangeMin"].toDouble(0.0));
    }
    if (params.contains("rangeMax")) {
        m_rangeMax = static_cast<float>(params["rangeMax"].toDouble(256.0));
    }
    m_params = params;
    return true;
}

// ============================================================
// execute：根据 mode 分发
// ============================================================
bool HistogramOperator::execute(const cv::Mat& input, ToolResult& result) {
    // RT-011: 空图像返回 ok=false，不崩溃
    if (input.empty()) {
        result.ok = false;
        result.data["error"] = "Empty input image";
        return false;
    }

    // RT-012: 非常规图像尺寸优雅处理
    if (input.cols <= 0 || input.rows <= 0) {
        result.ok = false;
        result.data["error"] = "Invalid image size";
        return false;
    }

    if (m_mode == "gray") {
        return executeGray(input, result);
    } else if (m_mode == "color") {
        return executeColor(input, result);
    } else if (m_mode == "equalize") {
        return executeEqualize(input, result);
    }

    result.ok = false;
    result.data["error"] = "Unknown mode: " + m_mode;
    return false;
}

// ============================================================
// executeGray：灰度直方图
// ============================================================
bool HistogramOperator::executeGray(const cv::Mat& input, ToolResult& result) {
    cv::Mat gray;
    // RT-014: 3 通道彩色图请求 gray 模式 → 自动转灰度
    if (input.channels() == 3 || input.channels() == 4) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }

    // RT-013: bins 已在 configure 中校验，此处保险再判一次
    int bins = m_bins;
    if (bins <= 0 || bins > 1024) bins = 256;

    float range[] = { m_rangeMin, m_rangeMax };
    const float* ranges[] = { range };
    int histSize[] = { bins };
    int channels[] = { 0 };

    cv::Mat hist;
    cv::calcHist(&gray, 1, channels, cv::Mat(), hist, 1, histSize, ranges, true, false);

    // 转换为 QJsonArray
    QJsonArray histArr;
    for (int i = 0; i < bins; ++i) {
        histArr.append(static_cast<double>(hist.at<float>(i)));
    }

    result.ok = true;
    result.data["mode"] = "gray";
    result.data["bins"] = bins;
    result.data["rangeMin"] = static_cast<double>(m_rangeMin);
    result.data["rangeMax"] = static_cast<double>(m_rangeMax);
    result.data["histogram"] = histArr;
    result.data["channels"] = 1;
    return true;
}

// ============================================================
// executeColor：彩色直方图（BGR 三通道）
// ============================================================
bool HistogramOperator::executeColor(const cv::Mat& input, ToolResult& result) {
    if (input.channels() < 3) {
        // 单通道图无彩色直方图概念，降级为 gray
        return executeGray(input, result);
    }

    int bins = m_bins;
    if (bins <= 0 || bins > 1024) bins = 256;

    float range[] = { m_rangeMin, m_rangeMax };
    const float* ranges[] = { range };
    int histSize[] = { bins };

    // BGR 三通道分别计算
    std::vector<cv::Mat> bgrPlanes;
    cv::split(input, bgrPlanes);

    QJsonArray histB, histG, histR;
    const char* keys[] = { "histogram_b", "histogram_g", "histogram_r" };
    QJsonArray* arrs[] = { &histB, &histG, &histR };

    for (int c = 0; c < 3; ++c) {
        int channels[] = { c };
        cv::Mat hist;
        cv::calcHist(&input, 1, channels, cv::Mat(), hist, 1, histSize, ranges, true, false);
        for (int i = 0; i < bins; ++i) {
            arrs[c]->append(static_cast<double>(hist.at<float>(i)));
        }
    }

    result.ok = true;
    result.data["mode"] = "color";
    result.data["bins"] = bins;
    result.data["rangeMin"] = static_cast<double>(m_rangeMin);
    result.data["rangeMax"] = static_cast<double>(m_rangeMax);
    result.data["histogram_b"] = histB;
    result.data["histogram_g"] = histG;
    result.data["histogram_r"] = histR;
    result.data["channels"] = 3;
    return true;
}

// ============================================================
// executeEqualize：直方图均衡化（RT-015）
// ============================================================
bool HistogramOperator::executeEqualize(const cv::Mat& input, ToolResult& result) {
    cv::Mat gray;
    // 均衡化要求 8 位单通道
    if (input.channels() == 3 || input.channels() == 4) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }
    if (gray.type() != CV_8U) {
        gray.convertTo(gray, CV_8U);
    }

    cv::Mat equalized;
    cv::equalizeHist(gray, equalized);

    // 计算均衡化前后直方图，验证分布更均匀（RT-015）
    float range[] = { 0, 256 };
    const float* ranges[] = { range };
    int histSize[] = { 256 };
    int channels[] = { 0 };
    cv::Mat histBefore, histAfter;
    cv::calcHist(&gray, 1, channels, cv::Mat(), histBefore, 1, histSize, ranges, true, false);
    cv::calcHist(&equalized, 1, channels, cv::Mat(), histAfter, 1, histSize, ranges, true, false);

    QJsonArray beforeArr, afterArr;
    for (int i = 0; i < 256; ++i) {
        beforeArr.append(static_cast<double>(histBefore.at<float>(i)));
        afterArr.append(static_cast<double>(histAfter.at<float>(i)));
    }

    result.ok = true;
    result.data["mode"] = "equalize";
    result.data["histogram_before"] = beforeArr;
    result.data["histogram_after"] = afterArr;
    result.overlayImage = equalized;  // RT-015: overlayImage 非空
    return true;
}

// ============================================================
// serialize / deserialize
// ============================================================
QJsonObject HistogramOperator::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["mode"] = m_mode;
    obj["bins"] = m_bins;
    obj["rangeMin"] = static_cast<double>(m_rangeMin);
    obj["rangeMax"] = static_cast<double>(m_rangeMax);
    return obj;
}

bool HistogramOperator::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;
    return configure(data);
}

} // namespace QDV

// ============================================================
// 动态库导出 C API
// ============================================================
extern "C" {
    QDV_EXPORT const char* operator_type() { return "Histogram"; }
    QDV_EXPORT const char* operator_version() { return "1.0.0"; }
    QDV_EXPORT QDV::IOperator* create_operator() { return new QDV::HistogramOperator(); }
}
