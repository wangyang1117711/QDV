#include "Vision/NormalizationTool.h"
#include "Core/Logger.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <sstream>

using namespace QDV;

namespace {

// ImageNet 预训练标准化常量
constexpr double IMAGENET_MEAN[] = {0.485, 0.456, 0.406};
constexpr double IMAGENET_STD[]  = {0.229, 0.224, 0.225};

// 将浮点矩阵按 [globalMin, globalMax] 线性映射到 [0, 255] 的 uint8 BGR/MONO 图
// 用于 overlayImage 显示与现有 PNG 保存流程兼容。
cv::Mat floatToDisplayUint8(const cv::Mat& src, double globalMin, double globalMax) {
    if (src.empty()) return cv::Mat();

    double range = globalMax - globalMin;
    if (range < 1e-12) range = 1.0;  // 避免除零，常量图映射到 128

    cv::Mat scaled;
    cv::convertScaleAbs(src, scaled, 255.0 / range, -globalMin * 255.0 / range);
    return scaled;
}

} // namespace

NormalizationTool::NormalizationTool() {
    m_name = QStringLiteral("均一化");
}

std::vector<double> NormalizationTool::parseFloatList(const QString& text) {
    std::vector<double> values;
    if (text.isEmpty()) return values;

    const QStringList parts = text.split(",", Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        double v = part.trimmed().toDouble(&ok);
        if (ok) values.push_back(v);
    }
    return values;
}

std::vector<double> NormalizationTool::broadcastToChannels(const std::vector<double>& values, int channels) {
    if (channels <= 0) return values;
    if (values.empty()) return std::vector<double>(channels, 0.0);
    if (static_cast<int>(values.size()) >= channels) {
        return std::vector<double>(values.begin(), values.begin() + channels);
    }

    std::vector<double> result;
    result.reserve(channels);
    if (values.size() == 1) {
        // 单值广播到所有通道
        result.assign(channels, values[0]);
    } else {
        // 通道数不足：先复制已有值，剩余用最后一个值填充
        result.insert(result.end(), values.begin(), values.end());
        const double last = values.back();
        while (static_cast<int>(result.size()) < channels) result.push_back(last);
    }
    return result;
}

cv::Mat NormalizationTool::toFloat32(const cv::Mat& input) {
    if (input.empty()) return cv::Mat();
    cv::Mat f;
    if (input.depth() == CV_32F) {
        f = input.clone();
    } else if (input.depth() == CV_64F) {
        input.convertTo(f, CV_32F);
    } else {
        // uint8/uint16 等整型：先转 float，保持像素值范围（不除以 255）
        input.convertTo(f, CV_32F);
    }
    return f;
}

void NormalizationTool::computeMeanStd(const cv::Mat& mat, double& mean, double& std, double epsilon) {
    cv::Scalar m, s;
    cv::meanStdDev(mat, m, s);
    mean = m[0];
    std = s[0] + epsilon;
    if (std < epsilon) std = epsilon;
}

bool NormalizationTool::configure(const QJsonObject& params) {
    if (params.contains("mode")) {
        const QString modeStr = params["mode"].toString().toLower();
        if (modeStr == "zscore" || modeStr == "z_score") {
            m_mode = Mode::ZScore;
        } else if (modeStr == "layernorm" || modeStr == "layer_norm") {
            m_mode = Mode::LayerNorm;
        } else if (modeStr == "instancenorm" || modeStr == "instance_norm") {
            m_mode = Mode::InstanceNorm;
        } else if (modeStr == "batchnorm" || modeStr == "batch_norm") {
            m_mode = Mode::BatchNorm;
        } else if (modeStr == "imagenet") {
            m_mode = Mode::ImageNet;
        } else {
            m_mode = Mode::MinMax;
        }
    }

    if (params.contains("targetRange")) {
        const QString tr = params["targetRange"].toString();
        m_targetRange = (tr == "minus1_1" || tr == "-1_1") ? "minus1_1" : "0_1";
    }

    if (params.contains("perChannel")) {
        m_perChannel = params["perChannel"].toBool(true);
    }

    if (params.contains("mean")) {
        m_meanValues = parseFloatList(params["mean"].toString());
    }

    if (params.contains("std")) {
        m_stdValues = parseFloatList(params["std"].toString());
    }

    if (params.contains("epsilon")) {
        m_epsilon = params["epsilon"].toDouble(1e-5);
        if (m_epsilon <= 0.0) {
            Logger::warn("NormalizationTool: epsilon <= 0，已回退为 1e-5");
            m_epsilon = 1e-5;
        }
    }

    m_params = params;
    return true;
}

bool NormalizationTool::normalize(const cv::Mat& input, cv::Mat& output, double& outMin, double& outMax) {
    if (input.empty()) return false;

    cv::Mat f = toFloat32(input);
    const int channels = f.channels();
    std::vector<cv::Mat> chs;
    if (channels > 1) {
        cv::split(f, chs);
    } else {
        chs.push_back(f);
    }

    m_usedMean.clear();
    m_usedStd.clear();
    m_usedMin = 0.0;
    m_usedMax = 0.0;

    std::vector<cv::Mat> outChs(chs.size());

    switch (m_mode) {
    case Mode::MinMax: {
        // 计算整体或逐通道的 min/max
        double globalMin = std::numeric_limits<double>::max();
        double globalMax = std::numeric_limits<double>::lowest();
        std::vector<double> mins(chs.size()), maxs(chs.size());

        for (size_t c = 0; c < chs.size(); ++c) {
            double minVal, maxVal;
            cv::minMaxLoc(chs[c], &minVal, &maxVal);
            mins[c] = minVal;
            maxs[c] = maxVal;
            if (!m_perChannel) {
                globalMin = std::min(globalMin, minVal);
                globalMax = std::max(globalMax, maxVal);
            }
        }

        if (!m_perChannel) {
            // 全局统一缩放
            for (size_t c = 0; c < chs.size(); ++c) {
                mins[c] = globalMin;
                maxs[c] = globalMax;
            }
        }

        const double tMin = (m_targetRange == "minus1_1") ? -1.0 : 0.0;
        const double tMax = (m_targetRange == "minus1_1") ? 1.0 : 1.0;

        for (size_t c = 0; c < chs.size(); ++c) {
            double range = maxs[c] - mins[c];
            if (range < m_epsilon) range = m_epsilon;
            cv::Mat tmp = (chs[c] - mins[c]) * ((tMax - tMin) / range) + tMin;
            outChs[c] = tmp;
            m_usedMean.push_back(mins[c]);
            m_usedStd.push_back(maxs[c]);
        }

        m_usedMin = tMin;
        m_usedMax = tMax;
        break;
    }

    case Mode::ZScore: {
        std::vector<double> meanList = broadcastToChannels(m_meanValues, static_cast<int>(chs.size()));
        std::vector<double> stdList  = broadcastToChannels(m_stdValues,  static_cast<int>(chs.size()));
        const bool autoMean = m_meanValues.empty();
        const bool autoStd  = m_stdValues.empty();

        for (size_t c = 0; c < chs.size(); ++c) {
            double mean = meanList[c];
            double std  = stdList[c];
            if (autoMean || autoStd) {
                double m, s;
                computeMeanStd(chs[c], m, s, m_epsilon);
                if (autoMean) mean = m;
                if (autoStd)  std  = s;
            }
            if (std < m_epsilon) std = m_epsilon;
            outChs[c] = (chs[c] - mean) / std;
            m_usedMean.push_back(mean);
            m_usedStd.push_back(std);
        }
        break;
    }

    case Mode::LayerNorm: {
        if (m_perChannel) {
            // 逐通道：每个样本的每个通道分别标准化
            for (size_t c = 0; c < chs.size(); ++c) {
                double mean, std;
                computeMeanStd(chs[c], mean, std, m_epsilon);
                outChs[c] = (chs[c] - mean) / std;
                m_usedMean.push_back(mean);
                m_usedStd.push_back(std);
            }
        } else {
            // 全特征：把整张图（所有通道）拉平后计算一个均值/标准差
            cv::Mat flat = f.reshape(1, 1);  // 1 x (C*H*W)
            double mean, std;
            computeMeanStd(flat, mean, std, m_epsilon);
            outChs.assign(chs.size(), (chs[0] - mean) / std);  // 各通道共享同一统计量
            // 实际 each channel 结果相同，但为了保持通道数，逐个赋值
            for (size_t c = 0; c < chs.size(); ++c) {
                outChs[c] = (chs[c] - mean) / std;
            }
            m_usedMean.assign(chs.size(), mean);
            m_usedStd.assign(chs.size(), std);
        }
        break;
    }

    case Mode::InstanceNorm: {
        // 实例均一化：逐通道独立计算
        for (size_t c = 0; c < chs.size(); ++c) {
            double mean, std;
            computeMeanStd(chs[c], mean, std, m_epsilon);
            outChs[c] = (chs[c] - mean) / std;
            m_usedMean.push_back(mean);
            m_usedStd.push_back(std);
        }
        break;
    }

    case Mode::BatchNorm: {
        // 单图场景下的批均一化：把整张图像所有像素视为一个 batch，
        // 计算全局均值和标准差后所有通道共享。
        cv::Mat flat = f.reshape(1, 1);
        double mean, std;
        computeMeanStd(flat, mean, std, m_epsilon);
        for (size_t c = 0; c < chs.size(); ++c) {
            outChs[c] = (chs[c] - mean) / std;
        }
        m_usedMean.assign(chs.size(), mean);
        m_usedStd.assign(chs.size(), std);
        break;
    }

    case Mode::ImageNet: {
        // x / 255.0，然后 (x - mean) / std
        std::vector<double> meanList = broadcastToChannels(m_meanValues.empty()
            ? std::vector<double>(std::begin(IMAGENET_MEAN), std::end(IMAGENET_MEAN))
            : m_meanValues, static_cast<int>(chs.size()));
        std::vector<double> stdList = broadcastToChannels(m_stdValues.empty()
            ? std::vector<double>(std::begin(IMAGENET_STD), std::end(IMAGENET_STD))
            : m_stdValues, static_cast<int>(chs.size()));

        for (size_t c = 0; c < chs.size(); ++c) {
            outChs[c] = (chs[c] / 255.0 - meanList[c]) / stdList[c];
            m_usedMean.push_back(meanList[c]);
            m_usedStd.push_back(stdList[c]);
        }
        break;
    }
    }

    // 合并通道
    if (outChs.size() > 1) {
        cv::merge(outChs, output);
    } else {
        output = outChs[0].clone();
    }

    // 计算输出全局 min/max（用于显示映射）
    cv::minMaxLoc(output, &outMin, &outMax);
    return true;
}

bool NormalizationTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        Logger::warn("NormalizationTool: empty input");
        result.ok = false;
        result.data["error"] = "Empty input";
        return false;
    }

    cv::Mat normalized;
    double outMin = 0.0, outMax = 0.0;
    if (!normalize(input, normalized, outMin, outMax)) {
        result.ok = false;
        result.data["error"] = "Normalization failed";
        return false;
    }

    // overlayImage 使用 uint8 显示图，兼容现有 PNG 保存与预览流程。
    // 实际标准化后的 float 结果通过 result.data 中的统计量反映。
    result.overlayImage = floatToDisplayUint8(normalized, outMin, outMax);

    result.ok = true;
    result.score = 1.0;

    // 元信息
    QJsonObject data;
    QString modeStr;
    switch (m_mode) {
    case Mode::MinMax:       modeStr = "minMax"; break;
    case Mode::ZScore:       modeStr = "zScore"; break;
    case Mode::LayerNorm:    modeStr = "layerNorm"; break;
    case Mode::InstanceNorm: modeStr = "instanceNorm"; break;
    case Mode::BatchNorm:    modeStr = "batchNorm"; break;
    case Mode::ImageNet:     modeStr = "imageNet"; break;
    }
    data["mode"] = modeStr;
    data["targetRange"] = m_targetRange;
    data["perChannel"] = m_perChannel;
    data["epsilon"] = m_epsilon;
    data["outputMin"] = outMin;
    data["outputMax"] = outMax;

    QJsonArray meanArr, stdArr;
    for (double v : m_usedMean) meanArr.append(v);
    for (double v : m_usedStd)  stdArr.append(v);
    data["mean"] = meanArr;
    data["std"] = stdArr;

    data["inputChannels"] = input.channels();
    data["inputDepth"] = input.depth();
    data["outputDepth"] = CV_32F;

    result.data = data;
    return true;
}

QJsonObject NormalizationTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    QString modeStr;
    switch (m_mode) {
    case Mode::MinMax:       modeStr = "minMax"; break;
    case Mode::ZScore:       modeStr = "zScore"; break;
    case Mode::LayerNorm:    modeStr = "layerNorm"; break;
    case Mode::InstanceNorm: modeStr = "instanceNorm"; break;
    case Mode::BatchNorm:    modeStr = "batchNorm"; break;
    case Mode::ImageNet:     modeStr = "imageNet"; break;
    }
    obj["mode"] = modeStr;
    obj["targetRange"] = m_targetRange;
    obj["perChannel"] = m_perChannel;
    obj["epsilon"] = m_epsilon;

    QStringList meanList, stdList;
    for (double v : m_meanValues) meanList.append(QString::number(v));
    for (double v : m_stdValues)  stdList.append(QString::number(v));
    obj["mean"] = meanList.join(",");
    obj["std"] = stdList.join(",");

    return obj;
}

bool NormalizationTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;
    return configure(data);
}
