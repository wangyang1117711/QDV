#include "OcrTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QStringList>
#include <vector>
#include <string>

using namespace QDV;

OcrTool::OcrTool() {
    m_name = "OCR字符识别";
}

bool OcrTool::configure(const QJsonObject& params) {
    if (params.contains("modelPath")) {
        m_modelPath = params["modelPath"].toString();
    }
    if (params.contains("charsetPath")) {
        m_charsetPath = params["charsetPath"].toString();
    }
    if (params.contains("imgHeight")) {
        const int v = params["imgHeight"].toInt();
        if (v < 8 || v > 256) {
            Logger::warn(QString("OcrTool: imgHeight %1 超出合理范围 [8,256]，已钳制").arg(v));
            m_imgHeight = std::clamp(v, 8, 256);
        } else {
            m_imgHeight = v;
        }
    }
    if (params.contains("confThreshold")) {
        m_confThreshold = params["confThreshold"].toDouble();
    }
    return true;
}

namespace {
// 优雅降级：未配置或加载失败时，overlay 提示并返回成功
void gracefulFallback(const cv::Mat& input, cv::Mat& overlay,
                      const QString& reason) {
    if (input.channels() == 1) {
        cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
    } else {
        overlay = input.clone();
    }
    cv::putText(overlay, "未配置OCR模型文件", cv::Point(10, 30),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 200, 255), 2);
    Q_UNUSED(reason);
}

// 读取 charset 文件（每行一个字符，UTF8），返回字符列表
QStringList loadCharset(const QString& path) {
    QStringList charset;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return charset;
    }
    QTextStream in(&f);
    // Qt6: QTextStream 默认 UTF-8，无需 setCodec
    while (!in.atEnd()) {
        QString line = in.readLine();
        // 兼容每行 "idx\tchar" 格式，取最后一列
        if (line.contains('\t')) {
            line = line.section('\t', -1);
        }
        if (line.contains(' ')) {
            line = line.section(' ', -1);
        }
        if (!line.isEmpty()) {
            charset.append(line);
        }
    }
    return charset;
}
} // namespace

bool OcrTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // typed ports 初始化：text 输出（所有路径均填充，保证变量管理取到值）
    result.ports["text"] = QString();

    // 模型未配置或文件不存在：优雅降级
    if (m_modelPath.isEmpty() || !QFileInfo::exists(m_modelPath)) {
        gracefulFallback(input, result.overlayImage, "model not configured");
        result.ok = true;
        result.data["text"] = "";
        result.data["error"] = "model not configured";
        return true;
    }

    // overlay 必须为 BGR
    cv::Mat overlay;
    if (input.channels() == 1) {
        cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
    } else {
        overlay = input.clone();
    }

    // 加载模型（用 try/catch 包裹，失败优雅降级）
    cv::dnn::Net net;
    try {
        net = cv::dnn::readNetFromONNX(m_modelPath.toStdString());
    } catch (const cv::Exception& e) {
        Logger::warn(QString("OcrTool: 加载模型失败: %1").arg(QString::fromStdString(e.what())));
        gracefulFallback(overlay, overlay, "model load failed");
        result.overlayImage = overlay;
        result.ok = true;
        result.data["text"] = "";
        result.data["error"] = "model load failed";
        return true;
    }

    // 读取 charset
    QStringList charset = loadCharset(m_charsetPath);
    if (charset.isEmpty()) {
        Logger::warn("OcrTool: charset 为空或读取失败");
        gracefulFallback(overlay, overlay, "charset missing");
        result.overlayImage = overlay;
        result.ok = true;
        result.data["text"] = "";
        result.data["error"] = "charset missing";
        return true;
    }

    // 预处理：转灰度，resize 到 height=m_imgHeight, width=保持比例
    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }
    const int targetH = m_imgHeight;
    const int targetW = std::max(1, static_cast<int>(std::round(
        static_cast<double>(gray.cols) * targetH / gray.rows)));
    cv::Mat resized;
    cv::resize(gray, resized, cv::Size(targetW, targetH));

    // 归一化到 [0,1]，构造 blob (1,1,H,W)
    cv::Mat blob = cv::dnn::blobFromImage(resized, 1.0 / 255.0, cv::Size(targetW, targetH),
                                          cv::Scalar(0), false, false, CV_32F);

    // 推理
    cv::Mat output;
    try {
        net.setInput(blob);
        output = net.forward();
    } catch (const cv::Exception& e) {
        Logger::warn(QString("OcrTool: 推理失败: %1").arg(QString::fromStdString(e.what())));
        gracefulFallback(overlay, overlay, "inference failed");
        result.overlayImage = overlay;
        result.ok = true;
        result.data["text"] = "";
        result.data["error"] = "inference failed";
        return true;
    }

    // CTC 解码：output 形状 (T, 1, C) 或 (1, T, C)
    // 取每帧 argmax，合并重复，去掉 blank(blank 通常为最后一位)
    std::string decoded;
    double confSum = 0.0;
    int confCount = 0;
    int lastIdx = -1;
    // 判断输出布局：size[0]==1 表示 (1, T, C)，否则视为 (T, 1, C)
    const bool layoutBatchFirst = (output.dims >= 3 && output.size[0] == 1);
    const int T = layoutBatchFirst ? output.size[1] : output.size[0];
    const int C = output.size[output.dims - 1];

    for (int t = 0; t < T; ++t) {
        const float* ptr = nullptr;
        if (layoutBatchFirst) {
            // shape (1, T, C)
            ptr = output.ptr<float>(0, t);
        } else {
            // shape (T, 1, C)
            ptr = output.ptr<float>(t, 0);
        }
        if (ptr == nullptr) continue;
        int maxIdx = 0;
        float maxVal = ptr[0];
        for (int c = 1; c < C; ++c) {
            if (ptr[c] > maxVal) {
                maxVal = ptr[c];
                maxIdx = c;
            }
        }
        // blank 通常为最后一类
        if (maxIdx != C - 1 && maxIdx != lastIdx) {
            if (maxIdx < charset.size()) {
                decoded += charset[maxIdx].toStdString();
            }
            confSum += maxVal;
            ++confCount;
        }
        lastIdx = maxIdx;
    }

    const double confidence = confCount > 0 ? confSum / confCount : 0.0;

    // overlay 显示识别文本
    cv::putText(overlay, decoded, cv::Point(10, 30),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

    result.overlayImage = overlay;
    result.ok = true;
    result.data["text"] = QString::fromStdString(decoded);
    result.data["confidence"] = confidence;
    // typed ports 填充识别文本
    result.ports["text"] = QString::fromStdString(decoded);
    return true;
}

QList<PortDescriptor> OcrTool::outputPorts() const {
    QList<PortDescriptor> ports;
    PortDescriptor text;
    text.name   = "text";
    text.cnName = QStringLiteral("识别文本");
    text.type   = PortType::String;
    text.dir    = PortDirection::Out;
    text.desc   = QStringLiteral("OCR 识别结果字符串");
    ports << text;
    return ports;
}

QList<PortDescriptor> OcrTool::inputPorts() const {
    QList<PortDescriptor> ports;
    PortDescriptor img;
    img.name   = "image";
    img.cnName = QStringLiteral("输入图像");
    img.type   = PortType::Image;
    img.dir    = PortDirection::In;
    img.desc   = QStringLiteral("待识别的输入图像（可由上游算子提供）");
    ports << img;
    return ports;
}

QJsonObject OcrTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["modelPath"] = m_modelPath;
    obj["charsetPath"] = m_charsetPath;
    obj["imgHeight"] = m_imgHeight;
    obj["confThreshold"] = m_confThreshold;
    return obj;
}

bool OcrTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("modelPath")) m_modelPath = data["modelPath"].toString();
    if (data.contains("charsetPath")) m_charsetPath = data["charsetPath"].toString();
    if (data.contains("imgHeight")) {
        const int v = data["imgHeight"].toInt();
        m_imgHeight = std::clamp(v, 8, 256);
    }
    if (data.contains("confThreshold")) m_confThreshold = data["confThreshold"].toDouble();
    return true;
}
