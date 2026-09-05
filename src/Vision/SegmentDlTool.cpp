#include "Vision/SegmentDlTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QByteArray>
#include <vector>
#include <algorithm>

using namespace QDV;

// ----------------------------------------------------------------------------
// Mat → QJsonObject 编码（PNG 字节流 + 尺寸/通道/类型元数据）
// 与 ZeroShotDetectTool 保持同构实现：掩膜等 Mat 数据无法直接放入
// ToolResult.ports（QVariantMap），统一编码为 JSON 数据字段，
// 供变量管理/属性面板展示与后续算子复用。
// ----------------------------------------------------------------------------
namespace {

QJsonObject encodeMatToJson(const cv::Mat& img) {
    QJsonObject obj;
    if (img.empty()) {
        obj["valid"] = false;
        return obj;
    }
    std::vector<uchar> buf;
    // 先转 8UC1/8UC3 便于 imencode（兼容 16U 等非常规类型）
    cv::Mat enc = img;
    if (enc.depth() != CV_8U) {
        double mn, mx;
        cv::minMaxLoc(enc, &mn, &mx);
        if (enc.channels() == 1) {
            cv::Mat tmp;
            enc.convertTo(tmp, CV_8U, 255.0 / std::max(1.0, mx - mn),
                          -mn * 255.0 / std::max(1.0, mx - mn));
            enc = tmp;
        } else {
            cv::Mat tmp;
            enc.convertTo(tmp, CV_8UC3, 255.0 / std::max(1.0, mx - mn),
                          -mn * 255.0 / std::max(1.0, mx - mn));
            enc = tmp;
        }
    }
    if (enc.channels() != 1 && enc.channels() != 3 && enc.channels() != 4) {
        obj["valid"] = false;
        return obj;
    }
    cv::imencode(".png", enc, buf);
    if (buf.empty()) {
        obj["valid"] = false;
        return obj;
    }
    obj["valid"] = true;
    obj["rows"] = img.rows;
    obj["cols"] = img.cols;
    obj["channels"] = img.channels();
    obj["type"] = img.type();
    obj["encoding"] = QStringLiteral("png");
    // 二进制字节流 base64 编码为字符串存储（QJsonValue 不支持 QByteArray）
    QByteArray raw(reinterpret_cast<const char*>(buf.data()), static_cast<int>(buf.size()));
    obj["data"] = QString::fromLatin1(raw.toBase64());
    return obj;
}

}  // namespace

SegmentDlTool::SegmentDlTool() {
    m_name = "DL语义分割";
}

SegmentDlTool::~SegmentDlTool() {
    // m_engine 所有权归外部（注入方），不 delete
}

bool SegmentDlTool::configure(const QJsonObject& params) {
    if (params.contains("modelPath")) {
        setModelPath(params["modelPath"].toString());
    }
    if (params.contains("confidenceThreshold")) {
        setConfidenceThreshold(params["confidenceThreshold"].toDouble(0.5));
    }
    if (params.contains("inputWidth") && params.contains("inputHeight")) {
        setInputSize(params["inputWidth"].toInt(512), params["inputHeight"].toInt(512));
    }
    if (params.contains("categoryLabels")) {
        QJsonArray arr = params["categoryLabels"].toArray();
        QStringList labels;
        for (const auto& v : arr) {
            labels.append(v.toString());
        }
        setCategoryLabels(labels);
    }
    m_params = params;
    return true;
}

bool SegmentDlTool::loadModel() {
    if (m_modelLoaded) return true;
    if (m_modelPath.isEmpty()) return false;

    if (!QFileInfo::exists(m_modelPath)) {
        Logger::error("SegmentDlTool: model file not found: " + m_modelPath);
        return false;
    }

    try {
        m_net = cv::dnn::readNetFromONNX(m_modelPath.toStdString());
        if (m_net.empty()) {
            Logger::error("SegmentDlTool: failed to read ONNX model: " + m_modelPath);
            return false;
        }
        m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        m_modelLoaded = true;
        Logger::info("SegmentDlTool: model loaded: " + m_modelPath);
    } catch (const cv::Exception& e) {
        Logger::error(QString("SegmentDlTool: OpenCV exception loading model: %1").arg(e.what()));
        return false;
    } catch (const std::exception& e) {
        Logger::error(QString("SegmentDlTool: exception loading model: %1").arg(e.what()));
        return false;
    }
    return true;
}

bool SegmentDlTool::execute(const cv::Mat& input, ToolResult& result) {
    // 线程安全：防止 m_modelLoaded 检查-设置竞态与 m_net 并发推理冲突
    QMutexLocker locker(&m_execMutex);

    if (input.empty()) {
        Logger::warn("SegmentDlTool: empty input");
        result.ok = false;
        return false;
    }

    if (m_modelPath.isEmpty()) {
        Logger::warn("SegmentDlTool: no model configured");
        result.ok = false;
        result.data["error"] = "No model configured";
        result.data["modelLoaded"] = false;
        return false;
    }

    // 优先通过 IInferenceEngine::segment 接口推理
    if (m_engine) {
        // 懒加载：共享引擎可能未加载模型或加载了其他模型，首次执行必须用本算子
        // 模型路径加载（与 AiClassifyTool/YoloDetectTool 的 m_warmedUp 模式保持一致）
        if (!m_warmedUp) {
            Logger::info("SegmentDlTool: loading model before first inference");
            bool loaded = m_engine->loadModel(m_modelPath,
                QSize(m_inputWidth, m_inputHeight),
                cv::Scalar(0, 0, 0),
                1.0 / 255.0,
                true);
            if (loaded) {
                m_engine->warmUp(3);
                m_warmedUp = true;
            } else {
                result.ok = false;
                result.data["error"] = "Failed to load model";
                result.data["modelLoaded"] = false;
                return false;
            }
        }
        result.data["modelLoaded"] = true;

        // segment 接口直接返回 mask + overlay（彩色叠加图）+ resultJson（含 classStats）
        cv::Mat mask, overlay;
        QJsonObject segResult;
        bool ok = m_engine->segment(input, mask, overlay, segResult);
        if (!ok) {
            Logger::error("SegmentDlTool: segmentation failed via engine");
            result.ok = false;
            // segResult 中可能已包含引擎返回的具体错误信息（如 OpenCV DNN forward error）
            if (segResult.contains("error") && !segResult["error"].toString().isEmpty()) {
                result.data["error"] = segResult["error"].toString();
                result.data["errorDetail"] = segResult["error"].toString();
            } else {
                result.data["error"] = "Segmentation failed";
            }
            return false;
        }

        // overlay 直接作为 result.overlayImage
        result.overlayImage = overlay;

        // 指标
        InferenceMetricsLite metrics = m_engine->lastMetrics();
        result.elapsedMs = metrics.totalMs;

        // 解析 classStats（若引擎已提供则直接使用，否则基于 mask 统计）
        QJsonArray classStats = segResult["classStats"].toArray();
        int numClasses = segResult["numClasses"].toInt();
        if (classStats.isEmpty() && !mask.empty()) {
            // 基于 mask 统计各类别像素占比
            numClasses = 0;
            for (int y = 0; y < mask.rows; ++y) {
                const uchar* row = mask.ptr<uchar>(y);
                for (int x = 0; x < mask.cols; ++x) {
                    int label = row[x];
                    if (label >= numClasses) numClasses = label + 1;
                }
            }
            std::vector<int> pixelCounts(numClasses, 0);
            for (int y = 0; y < mask.rows; ++y) {
                const uchar* row = mask.ptr<uchar>(y);
                for (int x = 0; x < mask.cols; ++x) {
                    int label = row[x];
                    if (label >= 0 && label < numClasses) {
                        pixelCounts[label]++;
                    }
                }
            }
            int totalPixels = mask.rows * mask.cols;
            for (int c = 0; c < numClasses; ++c) {
                QJsonObject stat;
                stat["classId"] = c;
                if (c < m_categoryLabels.size()) {
                    stat["className"] = m_categoryLabels[c];
                }
                stat["pixelCount"] = pixelCounts[c];
                stat["ratio"] = totalPixels > 0 ? (double)pixelCounts[c] / (double)totalPixels : 0.0;
                classStats.append(stat);
            }
        }

        result.data["classStats"] = classStats;
        result.data["numClasses"] = numClasses;
        result.data["inputWidth"] = m_inputWidth;
        result.data["inputHeight"] = m_inputHeight;
        result.data["elapsedMs"] = result.elapsedMs;
        result.data["confidenceThreshold"] = m_confidenceThreshold;
        result.data["preprocess_ms"] = metrics.preprocessMs;
        result.data["inference_ms"] = metrics.inferenceMs;
        result.data["postprocess_ms"] = metrics.postprocessMs;
        result.data["backend"] = metrics.backend;
        // typed ports 填充（与 operators.json outputs: mask 保持一致）
        result.ports["mask"] = encodeMatToJson(mask);
        result.data["mask"]  = encodeMatToJson(mask);
        result.score = 1.0;  // 分割成功即视为通过
        result.ok = true;

        m_results["lastNumClasses"] = numClasses;

        return true;
    }

    // Fallback: m_engine 未注入时，使用 cv::dnn::Net 直接推理
    Logger::warn("SegmentDlTool: no inference engine injected, fallback to cv::dnn::Net");

    // 懒加载模型（首次执行时加载）
    if (!m_modelLoaded && !loadModel()) {
        result.ok = false;
        result.data["error"] = "Failed to load model";
        result.data["modelLoaded"] = false;
        return false;
    }
    result.data["modelLoaded"] = true;

    QElapsedTimer timer;
    timer.start();

    // 预处理：resize + normalize + swapRB
    cv::Mat blob = cv::dnn::blobFromImage(input, 1.0 / 255.0,
        cv::Size(m_inputWidth, m_inputHeight),
        cv::Scalar(0, 0, 0), true, false, CV_32F);

    // 推理
    m_net.setInput(blob);
    cv::Mat output = m_net.forward();

    // 后处理：argmax 生成类别标签图 + 彩色掩膜
    cv::Mat colorMask, labelMap;
    postprocess(output, input.size(), colorMask, labelMap);

    result.elapsedMs = timer.elapsed();

    // 叠加分割结果到 overlayImage（原图 60% + 掩膜 40%）
    cv::Mat overlay;
    if (input.channels() == 1) {
        cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
    } else {
        overlay = input.clone();
    }
    cv::addWeighted(overlay, 0.6, colorMask, 0.4, 0, result.overlayImage);

    // 统计各类别像素占比
    int numClasses = (output.dims >= 2) ? output.size[1] : 0;
    std::vector<int> pixelCounts(numClasses, 0);
    for (int y = 0; y < labelMap.rows; ++y) {
        const uchar* row = labelMap.ptr<uchar>(y);
        for (int x = 0; x < labelMap.cols; ++x) {
            int label = row[x];
            if (label >= 0 && label < numClasses) {
                pixelCounts[label]++;
            }
        }
    }

    int totalPixels = labelMap.rows * labelMap.cols;
    QJsonArray classStats;
    for (int c = 0; c < numClasses; ++c) {
        QJsonObject stat;
        stat["classId"] = c;
        if (c < m_categoryLabels.size()) {
            stat["className"] = m_categoryLabels[c];
        }
        stat["pixelCount"] = pixelCounts[c];
        stat["ratio"] = totalPixels > 0 ? (double)pixelCounts[c] / (double)totalPixels : 0.0;
        classStats.append(stat);
    }

    result.data["classStats"] = classStats;
    result.data["numClasses"] = numClasses;
    result.data["inputWidth"] = m_inputWidth;
    result.data["inputHeight"] = m_inputHeight;
    result.data["elapsedMs"] = result.elapsedMs;
    result.data["confidenceThreshold"] = m_confidenceThreshold;
    // typed ports 填充（与 operators.json outputs: mask 保持一致）
    result.ports["mask"] = encodeMatToJson(labelMap);
    result.data["mask"]  = encodeMatToJson(labelMap);
    result.score = 1.0;  // 分割成功即视为通过
    result.ok = true;

    m_results["lastNumClasses"] = numClasses;

    return true;
}

void SegmentDlTool::postprocess(const cv::Mat& output, const cv::Size& origSize,
                                 cv::Mat& colorMask, cv::Mat& labelMap) {
    // 输出格式: [1, num_classes, H, W]
    // 对每个像素沿通道维度 argmax 得到类别标签
    if (output.dims < 4) {
        colorMask = cv::Mat::zeros(origSize, CV_8UC3);
        labelMap = cv::Mat::zeros(origSize, CV_8UC1);
        return;
    }

    int numClasses = output.size[1];
    int h = output.size[2];
    int w = output.size[3];

    labelMap.create(h, w, CV_8UC1);
    colorMask.create(h, w, CV_8UC3);

    const float* data = (const float*)output.data;

    // 逐像素 argmax
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int maxClass = 0;
            float maxVal = -1.0f;
            for (int c = 0; c < numClasses; ++c) {
                // data[0][c][y][x] = data[c * h * w + y * w + x]
                float val = data[c * h * w + y * w + x];
                if (val > maxVal) {
                    maxVal = val;
                    maxClass = c;
                }
            }
            labelMap.at<uchar>(y, x) = (uchar)maxClass;
            cv::Scalar color = classColor(maxClass);
            colorMask.at<cv::Vec3b>(y, x) = cv::Vec3b((uchar)color[0], (uchar)color[1], (uchar)color[2]);
        }
    }

    // 缩放到原图尺寸
    cv::resize(colorMask, colorMask, origSize, 0, 0, cv::INTER_LINEAR);
    cv::resize(labelMap, labelMap, origSize, 0, 0, cv::INTER_NEAREST);
}

cv::Scalar SegmentDlTool::classColor(int classId) {
    // 预定义颜色表（20 种），循环使用保证一致性
    static const cv::Scalar colors[] = {
        cv::Scalar(128, 0, 0),     // 暗红
        cv::Scalar(0, 128, 0),     // 暗绿
        cv::Scalar(0, 0, 128),     // 暗蓝
        cv::Scalar(128, 128, 0),   // 橄榄
        cv::Scalar(128, 0, 128),   // 紫
        cv::Scalar(0, 128, 128),   // 青绿
        cv::Scalar(192, 0, 0),     // 红
        cv::Scalar(0, 192, 0),     // 绿
        cv::Scalar(0, 0, 192),     // 蓝
        cv::Scalar(192, 192, 0),   // 黄
        cv::Scalar(192, 0, 192),   // 品红
        cv::Scalar(0, 192, 192),   // 青
        cv::Scalar(255, 0, 0),     // 亮红
        cv::Scalar(0, 255, 0),     // 亮绿
        cv::Scalar(0, 0, 255),     // 亮蓝
        cv::Scalar(255, 255, 0),   // 亮黄
        cv::Scalar(255, 0, 255),   // 亮品红
        cv::Scalar(0, 255, 255),   // 亮青
        cv::Scalar(128, 128, 128), // 灰
        cv::Scalar(255, 255, 255)  // 白
    };
    constexpr int numColors = sizeof(colors) / sizeof(colors[0]);
    return colors[classId % numColors];
}

QJsonObject SegmentDlTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["modelPath"] = m_modelPath;
    obj["confidenceThreshold"] = m_confidenceThreshold;
    obj["inputWidth"] = m_inputWidth;
    obj["inputHeight"] = m_inputHeight;
    QJsonArray labels;
    for (const auto& label : m_categoryLabels) {
        labels.append(label);
    }
    obj["categoryLabels"] = labels;
    return obj;
}

bool SegmentDlTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("modelPath")) m_modelPath = data["modelPath"].toString();
    if (data.contains("confidenceThreshold")) m_confidenceThreshold = data["confidenceThreshold"].toDouble(0.5);
    if (data.contains("inputWidth")) m_inputWidth = data["inputWidth"].toInt(512);
    if (data.contains("inputHeight")) m_inputHeight = data["inputHeight"].toInt(512);
    if (data.contains("categoryLabels")) {
        QJsonArray arr = data["categoryLabels"].toArray();
        m_categoryLabels.clear();
        for (const auto& v : arr) {
            m_categoryLabels.append(v.toString());
        }
    }

    return configure(data);
}

// ----------------------------------------------------------------------------
// 端口声明（与 config/operators.json 的 outputs/inputs 保持一致）
// ----------------------------------------------------------------------------
QList<PortDescriptor> SegmentDlTool::outputPorts() const {
    QList<PortDescriptor> ports;
    PortDescriptor mask;
    mask.name   = "mask";
    mask.cnName = QStringLiteral("分割掩膜");
    mask.type   = PortType::Image;
    mask.dir    = PortDirection::Out;
    mask.desc   = QStringLiteral("语义分割结果掩膜，数据见 result.data.mask / result.ports.mask");
    ports << mask;
    return ports;
}

QList<PortDescriptor> SegmentDlTool::inputPorts() const {
    QList<PortDescriptor> ports;
    PortDescriptor img;
    img.name   = "image";
    img.cnName = QStringLiteral("输入图像");
    img.type   = PortType::Image;
    img.dir    = PortDirection::In;
    img.desc   = QStringLiteral("待分割的输入图像");
    ports << img;
    return ports;
}
