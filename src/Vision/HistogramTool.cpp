#include "HistogramTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <QJsonArray>
#include <algorithm>

using namespace QDV;

namespace {
constexpr int kBinsMin = 1;
constexpr int kBinsMax = 1024;
constexpr int kCanvasSize = 256;   // 直方图画布尺寸 256x256
}

HistogramTool::HistogramTool() {
    m_name = "直方图工具";
}

bool HistogramTool::configure(const QJsonObject& params) {
    if (params.contains("mode")) {
        const QString v = params["mode"].toString();
        if (v == "gray" || v == "color" || v == "equalize") {
            m_mode = v;
        } else {
            Logger::warn(QString("HistogramTool: mode '%1' 非法，回退为 'gray'").arg(v));
            m_mode = "gray";
        }
    }
    if (params.contains("bins")) {
        const int v = params["bins"].toInt();
        if (v < kBinsMin || v > kBinsMax) {
            Logger::warn(QString("HistogramTool: bins %1 超出范围 [%2,%3]，已钳制")
                .arg(v).arg(kBinsMin).arg(kBinsMax));
            m_bins = std::clamp(v, kBinsMin, kBinsMax);
        } else {
            m_bins = v;
        }
    }
    if (params.contains("rangeMin")) {
        m_rangeMin = static_cast<float>(params["rangeMin"].toDouble());
    }
    if (params.contains("rangeMax")) {
        m_rangeMax = static_cast<float>(params["rangeMax"].toDouble());
    }
    // 范围上下限校验
    if (m_rangeMax <= m_rangeMin) {
        Logger::warn("HistogramTool: rangeMax <= rangeMin，已重置为默认 [0,256]");
        m_rangeMin = 0.0f;
        m_rangeMax = 256.0f;
    }
    return true;
}

namespace {
// 绘制单通道直方图到深色背景画布，按最大值归一化绘制绿色柱状线
void drawHistogram(cv::Mat& canvas, const cv::Mat& hist, const cv::Scalar& color) {
    if (hist.empty()) return;
    double maxVal = 0.0;
    cv::minMaxLoc(hist, nullptr, &maxVal, nullptr, nullptr);
    if (maxVal <= 0.0) return;
    const int bins = hist.rows;
    const double binW = static_cast<double>(kCanvasSize) / bins;
    for (int i = 0; i < bins; ++i) {
        const float val = hist.at<float>(i);
        const int h = static_cast<int>(val / maxVal * (kCanvasSize - 1));
        const int x1 = static_cast<int>(i * binW);
        cv::line(canvas,
                 cv::Point(x1, kCanvasSize - 1),
                 cv::Point(x1, kCanvasSize - 1 - h),
                 color,
                 std::max(1, static_cast<int>(binW)));
    }
}
} // namespace

bool HistogramTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // 转灰度
    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }

    const int histSize = m_bins;
    const float hranges[] = { m_rangeMin, m_rangeMax };
    const float* ranges[] = { hranges };
    const int channels = 0;

    if (m_mode == "gray") {
        cv::Mat hist;
        cv::calcHist(&gray, 1, &channels, cv::Mat(), hist, 1, &histSize, ranges);
        // 画布：深色背景，绿色柱状
        cv::Mat canvas(kCanvasSize, kCanvasSize, CV_8UC3, cv::Scalar(30, 30, 30));
        drawHistogram(canvas, hist, cv::Scalar(0, 255, 0));
        result.overlayImage = canvas;

        QJsonArray arr;
        for (int i = 0; i < histSize; ++i) {
            arr.append(static_cast<double>(hist.at<float>(i)));
        }
        result.data["histogram"] = arr;
        result.data["mode"] = "gray";
    } else if (m_mode == "color") {
        if (input.channels() < 3) {
            // 通道不足按灰度处理
            Logger::warn("HistogramTool: color 模式需要 BGR 输入，回退为 gray");
            cv::Mat hist;
            cv::calcHist(&gray, 1, &channels, cv::Mat(), hist, 1, &histSize, ranges);
            cv::Mat canvas(kCanvasSize, kCanvasSize, CV_8UC3, cv::Scalar(30, 30, 30));
            drawHistogram(canvas, hist, cv::Scalar(0, 255, 0));
            result.overlayImage = canvas;
            result.data["mode"] = "gray";
        } else {
            // BGR 三通道分别计算（split 后每个 Mat 为单通道，calcHist 的 channel 索引统一为 0）
            const cv::Scalar colors[3] = {
                cv::Scalar(255, 0, 0),    // B 蓝
                cv::Scalar(0, 255, 0),    // G 绿
                cv::Scalar(0, 0, 255)     // R 红
            };
            const QString keys[3] = { "histogram_b", "histogram_g", "histogram_r" };

            cv::Mat canvas(kCanvasSize, kCanvasSize, CV_8UC3, cv::Scalar(30, 30, 30));
            std::vector<cv::Mat> bgr;
            cv::split(input, bgr);

            const int ch = 0;  // 单通道 Mat 的通道索引
            for (int c = 0; c < 3; ++c) {
                cv::Mat hist;
                cv::calcHist(&bgr[c], 1, &ch, cv::Mat(), hist, 1, &histSize, ranges);
                drawHistogram(canvas, hist, colors[c]);
                QJsonArray arr;
                for (int i = 0; i < histSize; ++i) {
                    arr.append(static_cast<double>(hist.at<float>(i)));
                }
                result.data[keys[c]] = arr;
            }
            result.overlayImage = canvas;
            result.data["mode"] = "color";
        }
    } else if (m_mode == "equalize") {
        // 均衡化前直方图
        cv::Mat histBefore;
        cv::calcHist(&gray, 1, &channels, cv::Mat(), histBefore, 1, &histSize, ranges);
        // 均衡化
        cv::Mat eq;
        cv::equalizeHist(gray, eq);
        cv::Mat histAfter;
        cv::calcHist(&eq, 1, &channels, cv::Mat(), histAfter, 1, &histSize, ranges);
        // overlay = 均衡化后图转 BGR
        cv::cvtColor(eq, result.overlayImage, cv::COLOR_GRAY2BGR);

        QJsonArray beforeArr, afterArr;
        for (int i = 0; i < histSize; ++i) {
            beforeArr.append(static_cast<double>(histBefore.at<float>(i)));
            afterArr.append(static_cast<double>(histAfter.at<float>(i)));
        }
        result.data["histogram_before"] = beforeArr;
        result.data["histogram_after"] = afterArr;
        result.data["mode"] = "equalize";
    }

    result.ok = true;
    result.data["bins"] = m_bins;
    result.data["rangeMin"] = static_cast<double>(m_rangeMin);
    result.data["rangeMax"] = static_cast<double>(m_rangeMax);
    return true;
}

QJsonObject HistogramTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["mode"] = m_mode;
    obj["bins"] = m_bins;
    obj["rangeMin"] = static_cast<double>(m_rangeMin);
    obj["rangeMax"] = static_cast<double>(m_rangeMax);
    return obj;
}

bool HistogramTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("mode")) {
        const QString v = data["mode"].toString();
        if (v == "gray" || v == "color" || v == "equalize") {
            m_mode = v;
        }
    }
    if (data.contains("bins")) {
        const int v = data["bins"].toInt();
        m_bins = std::clamp(v, kBinsMin, kBinsMax);
    }
    if (data.contains("rangeMin")) m_rangeMin = static_cast<float>(data["rangeMin"].toDouble());
    if (data.contains("rangeMax")) m_rangeMax = static_cast<float>(data["rangeMax"].toDouble());
    if (m_rangeMax <= m_rangeMin) {
        m_rangeMin = 0.0f;
        m_rangeMax = 256.0f;
    }
    return true;
}
