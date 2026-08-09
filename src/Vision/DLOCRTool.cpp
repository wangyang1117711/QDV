#include "Vision/DLOCRTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/features2d.hpp>  // cv::MSER
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <vector>

using namespace QDV;

DLOCRTool::DLOCRTool() {
    m_name = "端到端文本检测识别";
}

// ---------------------------------------------------------------
// 参数配置：钳制到合理范围，重置模型加载状态（路径变更需重新加载）
// ---------------------------------------------------------------
bool DLOCRTool::configure(const QJsonObject& params) {
    if (params.contains("detectionModelPath")) {
        m_detectionModelPath = params["detectionModelPath"].toString();
    }
    if (params.contains("recognitionModelPath")) {
        m_recognitionModelPath = params["recognitionModelPath"].toString();
    }
    if (params.contains("confThreshold")) {
        m_confThreshold = params["confThreshold"].toDouble(0.5);
    }
    if (params.contains("nmsThreshold")) {
        m_nmsThreshold = params["nmsThreshold"].toDouble(0.4);
    }
    if (params.contains("inputWidth")) {
        m_inputWidth = std::clamp(params["inputWidth"].toInt(320), 32, 4096);
    }
    if (params.contains("inputHeight")) {
        m_inputHeight = std::clamp(params["inputHeight"].toInt(320), 32, 4096);
    }
    if (params.contains("language")) {
        m_language = params["language"].toString("chi_sim");
    }
    if (params.contains("maxTextRegions")) {
        m_maxTextRegions = std::clamp(params["maxTextRegions"].toInt(20), 1, 1000);
    }
    m_params = params;

    // 路径变更后需重新懒加载
    m_detModelLoaded = false;
    m_recModelLoaded = false;
    return true;
}

// ---------------------------------------------------------------
// 懒加载检测模型：用 QFile 读字节再 readNetFromONNX/Tensorflow，兼容中文路径
// ---------------------------------------------------------------
bool DLOCRTool::loadDetectionModel() {
    if (m_detModelLoaded) return true;
    if (m_detectionModelPath.isEmpty() || !QFileInfo::exists(m_detectionModelPath)) {
        return false;
    }
    QFile f(m_detectionModelPath);
    if (!f.open(QIODevice::ReadOnly)) {
        Logger::warn("DLOCRTool: 无法打开检测模型文件: " + m_detectionModelPath);
        return false;
    }
    QByteArray bytes = f.readAll();
    f.close();
    // QByteArray -> vector<uchar>（兼容中文路径，避免 toStdString 在 Windows 的编码问题）
    std::vector<uchar> data(reinterpret_cast<const uchar*>(bytes.constData()),
                            reinterpret_cast<const uchar*>(bytes.constData()) + bytes.size());
    try {
        if (m_detectionModelPath.endsWith(".pb", Qt::CaseInsensitive)) {
            m_detNet = cv::dnn::readNetFromTensorflow(data);
        } else {
            // 默认按 ONNX 解析（.onnx 或其他扩展名）
            m_detNet = cv::dnn::readNetFromONNX(data);
        }
        m_detNet.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        m_detNet.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        m_detModelLoaded = true;
        Logger::info("DLOCRTool: 检测模型加载成功: " + m_detectionModelPath);
        return true;
    } catch (const cv::Exception& e) {
        Logger::warn(QString("DLOCRTool: 加载检测模型失败: %1").arg(QString::fromStdString(e.what())));
        m_detModelLoaded = false;
        return false;
    }
}

// ---------------------------------------------------------------
// 懒加载识别模型
// ---------------------------------------------------------------
bool DLOCRTool::loadRecognitionModel() {
    if (m_recModelLoaded) return true;
    if (m_recognitionModelPath.isEmpty() || !QFileInfo::exists(m_recognitionModelPath)) {
        return false;
    }
    QFile f(m_recognitionModelPath);
    if (!f.open(QIODevice::ReadOnly)) {
        Logger::warn("DLOCRTool: 无法打开识别模型文件: " + m_recognitionModelPath);
        return false;
    }
    QByteArray bytes = f.readAll();
    f.close();
    std::vector<uchar> data(reinterpret_cast<const uchar*>(bytes.constData()),
                            reinterpret_cast<const uchar*>(bytes.constData()) + bytes.size());
    try {
        m_recNet = cv::dnn::readNetFromONNX(data);
        m_recNet.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        m_recNet.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        m_recModelLoaded = true;
        Logger::info("DLOCRTool: 识别模型加载成功: " + m_recognitionModelPath);
        return true;
    } catch (const cv::Exception& e) {
        Logger::warn(QString("DLOCRTool: 加载识别模型失败: %1").arg(QString::fromStdString(e.what())));
        m_recModelLoaded = false;
        return false;
    }
}

// ---------------------------------------------------------------
// dnn 文本检测：
// - EAST 风格：双输出 scores(1,1,H,W) + geometry(1,5,H,W)，按偏移解析矩形
// - DB 风格：单输出概率图(1,1,H,W)，阈值化 + 连通域得到矩形
// 最后 NMS 去重
// ---------------------------------------------------------------
bool DLOCRTool::detectByDnn(const cv::Mat& input, std::vector<TextRegion>& regions) {
    const double rW = static_cast<double>(input.cols) / std::max(1, m_inputWidth);
    const double rH = static_cast<double>(input.rows) / std::max(1, m_inputHeight);

    // 预处理：resize 到检测输入尺寸，确保 3 通道（EAST 需要 BGR）
    cv::Mat resized;
    cv::resize(input, resized, cv::Size(m_inputWidth, m_inputHeight));
    if (resized.channels() == 1) {
        cv::cvtColor(resized, resized, cv::COLOR_GRAY2BGR);
    }
    // EAST 预处理：scale=1.0, mean=ImageNet 均值, swapRB=true
    cv::Mat blob = cv::dnn::blobFromImage(resized, 1.0, cv::Size(m_inputWidth, m_inputHeight),
                                          cv::Scalar(123.68, 116.78, 103.94), true, false);

    std::vector<cv::Mat> outs;
    m_detNet.setInput(blob);
    m_detNet.forward(outs);  // 取所有输出层

    std::vector<cv::Rect> boxes;
    std::vector<float> confs;

    if (outs.size() >= 2 && outs[1].dims >= 4 && outs[1].size[1] == 5) {
        // EAST 风格：scores (1,1,H,W) + geometry (1,5,H,W)
        // geometry 5 通道 = (top, right, bottom, left) 偏移 + angle（此处忽略角度，用水平框）
        const int H = outs[0].size[2];
        const int W = outs[0].size[3];
        for (int r = 0; r < H; ++r) {
            for (int c = 0; c < W; ++c) {
                float score = outs[0].ptr<float>(0, 0, r)[c];
                if (score < static_cast<float>(m_confThreshold)) continue;
                float top    = outs[1].ptr<float>(0, 0, r)[c];
                float right  = outs[1].ptr<float>(0, 1, r)[c];
                float bottom = outs[1].ptr<float>(0, 2, r)[c];
                float left   = outs[1].ptr<float>(0, 3, r)[c];
                // 偏移量映射回原图坐标
                float offsetX = static_cast<float>(c * rW);
                float offsetY = static_cast<float>(r * rH);
                int startX = static_cast<int>(offsetX - left);
                int startY = static_cast<int>(offsetY - top);
                int endX   = static_cast<int>(offsetX + right);
                int endY   = static_cast<int>(offsetY + bottom);
                // 钳制到图像边界
                startX = std::max(0, std::min(startX, input.cols - 1));
                startY = std::max(0, std::min(startY, input.rows - 1));
                endX   = std::max(0, std::min(endX, input.cols - 1));
                endY   = std::max(0, std::min(endY, input.rows - 1));
                if (endX <= startX || endY <= startY) continue;
                boxes.emplace_back(startX, startY, endX - startX, endY - startY);
                confs.push_back(score);
            }
        }
    } else if (!outs.empty() && outs[0].dims >= 3) {
        // DB 风格：单输出概率图 (1,1,H,W) 或 (1,H,W)，阈值化 + 连通域
        const int H = outs[0].size[outs[0].dims - 2];
        const int W = outs[0].size[outs[0].dims - 1];
        cv::Mat prob(H, W, CV_32F);
        for (int r = 0; r < H; ++r) {
            for (int c = 0; c < W; ++c) {
                if (outs[0].dims == 4) {
                    prob.at<float>(r, c) = outs[0].ptr<float>(0, 0, r)[c];
                } else {
                    prob.at<float>(r, c) = outs[0].ptr<float>(0, r)[c];
                }
            }
        }
        cv::Mat bin;
        cv::threshold(prob, bin, m_confThreshold, 255.0, cv::THRESH_BINARY);
        bin.convertTo(bin, CV_8U);
        // 概率图 resize 回原图尺寸后找连通域
        cv::Mat binFull;
        cv::resize(bin, binFull, input.size());
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binFull, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        for (const auto& cnt : contours) {
            cv::Rect b = cv::boundingRect(cnt);
            if (b.width < 8 || b.height < 8) continue;  // 过滤过小区域
            boxes.push_back(b);
            confs.push_back(0.9f);  // DB 无明确置信度，给默认值
        }
    }

    // NMS 去重
    std::vector<int> idx;
    if (!boxes.empty()) {
        cv::dnn::NMSBoxes(boxes, confs, static_cast<float>(m_confThreshold),
                          static_cast<float>(m_nmsThreshold), idx);
    }
    for (int i : idx) {
        TextRegion rg;
        rg.rect = boxes[i];
        rg.confidence = confs[i];
        regions.push_back(rg);
    }
    return true;
}

// ---------------------------------------------------------------
// MSER 降级检测：用 cv::MSER 找文本候选区域，过滤过小区域后 NMS 去重
// ---------------------------------------------------------------
bool DLOCRTool::detectByMSER(const cv::Mat& input, std::vector<TextRegion>& regions) {
    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }

    cv::Ptr<cv::MSER> ms = cv::MSER::create();
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Rect> msBoxes;
    ms->detectRegions(gray, contours, msBoxes);

    std::vector<cv::Rect> boxes;
    std::vector<float> confs;
    for (const auto& b : msBoxes) {
        // 过滤过小/过大区域
        if (b.width < 8 || b.height < 8) continue;
        if (b.width > gray.cols || b.height > gray.rows) continue;
        boxes.push_back(b);
        confs.push_back(0.5f);  // MSER 无置信度，给默认值
    }

    // NMS 去重
    std::vector<int> idx;
    if (!boxes.empty()) {
        cv::dnn::NMSBoxes(boxes, confs, 0.1f, static_cast<float>(m_nmsThreshold), idx);
    }
    for (int i : idx) {
        TextRegion rg;
        rg.rect = boxes[i];
        rg.confidence = confs[i];
        regions.push_back(rg);
    }
    return true;
}

// ---------------------------------------------------------------
// dnn 识别单个文本区域：
// 注意：本算子无 charset 参数，无法将 CTC 输出解码为文字。
// 此处仅执行推理并计算平均置信度，返回占位文本（完整文字识别需补充 charset，参考 OcrTool）。
// ---------------------------------------------------------------
QString DLOCRTool::recognizeByDnn(const cv::Mat& region, double& conf) {
    conf = 0.0;
    if (region.empty()) return QString("空区域");
    try {
        cv::Mat gray;
        if (region.channels() == 3) {
            cv::cvtColor(region, gray, cv::COLOR_BGR2GRAY);
        } else {
            gray = region.clone();
        }
        // 固定高度 32，宽度按比例缩放（CRNN 风格输入）
        const int targetH = 32;
        const int targetW = std::max(1, static_cast<int>(std::round(
            static_cast<double>(gray.cols) * targetH / std::max(1, gray.rows))));
        cv::resize(gray, gray, cv::Size(targetW, targetH));
        cv::Mat blob = cv::dnn::blobFromImage(gray, 1.0 / 255.0, cv::Size(targetW, targetH),
                                              cv::Scalar(0), false, false, CV_32F);
        m_recNet.setInput(blob);
        cv::Mat out = m_recNet.forward();
        // CTC 解码需 charset，此处仅计算每帧最大概率的均值作为置信度
        // 输出布局 (T,1,C) 或 (1,T,C)
        const bool batchFirst = (out.dims >= 3 && out.size[0] == 1);
        const int T = batchFirst ? out.size[1] : out.size[0];
        const int C = out.size[out.dims - 1];
        double sumMax = 0.0;
        int count = 0;
        for (int t = 0; t < T; ++t) {
            const float* ptr = batchFirst ? out.ptr<float>(0, t) : out.ptr<float>(t, 0);
            if (!ptr) continue;
            float maxVal = ptr[0];
            for (int c = 1; c < C; ++c) {
                if (ptr[c] > maxVal) maxVal = ptr[c];
            }
            sumMax += maxVal;
            ++count;
        }
        conf = count > 0 ? sumMax / count : 0.0;
        // 无 charset，返回占位（标注需 charset 解码）
        return QString("已识别(需charset)");
    } catch (const cv::Exception& e) {
        Logger::warn(QString("DLOCRTool: 识别推理失败: %1").arg(QString::fromStdString(e.what())));
        conf = 0.0;
        return QString("识别失败");
    }
}

// ---------------------------------------------------------------
// execute：检测 → 识别 → 绘制 → 填充结果
// ---------------------------------------------------------------
bool DLOCRTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // overlay 准备（统一 BGR）
    cv::Mat overlay;
    if (input.channels() == 1) {
        cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
    } else {
        overlay = input.clone();
    }

    try {
        // 1. 文本检测：优先 dnn，失败/无模型降级 MSER
        std::vector<TextRegion> regions;
        bool detModelOk = false;
        if (!m_detectionModelPath.isEmpty()) {
            detModelOk = loadDetectionModel();
            if (detModelOk) {
                detectByDnn(input, regions);
                result.data["modelLoaded"] = true;
            } else {
                // 模型加载失败，降级 MSER
                result.data["modelLoaded"] = false;
                result.data["warning"] = "检测模型加载失败，降级到 MSER";
                detectByMSER(input, regions);
            }
        } else {
            // 未配置检测模型，降级 MSER
            result.data["modelLoaded"] = false;
            result.data["warning"] = "未配置检测模型，使用 MSER 降级";
            detectByMSER(input, regions);
        }

        // 限制最大文本区域数（按置信度排序后截断）
        if (static_cast<int>(regions.size()) > m_maxTextRegions) {
            std::sort(regions.begin(), regions.end(),
                      [](const TextRegion& a, const TextRegion& b) {
                          return a.confidence > b.confidence;
                      });
            regions.resize(m_maxTextRegions);
        }

        // 2. 文本识别：懒加载识别模型
        bool recAvailable = false;
        if (!m_recognitionModelPath.isEmpty()) {
            recAvailable = loadRecognitionModel();
        }
        result.data["recModelLoaded"] = recAvailable;

        // 3. 对每个区域识别 + 绘制
        QVariantList textsList;
        QStringList allTexts;
        for (auto& rg : regions) {
            // 钳制到图像边界
            cv::Rect safeRect = rg.rect & cv::Rect(0, 0, overlay.cols, overlay.rows);
            if (safeRect.area() <= 0) continue;

            if (recAvailable) {
                cv::Mat regionImg = overlay(safeRect).clone();
                rg.text = recognizeByDnn(regionImg, rg.recConfidence);
            } else {
                rg.text = "待识别";
                rg.recConfidence = 0.0;
            }

            // 绘制文本框（绿色）
            cv::rectangle(overlay, safeRect, cv::Scalar(0, 255, 0), 2);

            // 标注文字：纯 ASCII 直接显示，含中文则显示区域编号（避免 putText 乱码）
            bool isAscii = true;
            for (QChar ch : rg.text) {
                if (ch.unicode() > 127) { isAscii = false; break; }
            }
            std::string label = isAscii ? rg.text.toStdString()
                                        : ("R" + std::to_string(allTexts.size()));
            cv::putText(overlay, label,
                        cv::Point(safeRect.x, std::max(safeRect.y - 5, 12)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 255), 1);
            // 检测置信度标注
            std::string confStr = std::to_string(rg.confidence).substr(0, 4);
            cv::putText(overlay, confStr,
                        cv::Point(safeRect.x, std::min(safeRect.y + safeRect.height + 15,
                                                       overlay.rows - 2)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(200, 200, 0), 1);

            // 加入 texts 列表（每个含 {text, x, y, w, h, confidence}）
            QVariantMap item;
            item["text"] = rg.text;
            item["x"] = safeRect.x;
            item["y"] = safeRect.y;
            item["w"] = safeRect.width;
            item["h"] = safeRect.height;
            item["confidence"] = rg.confidence;
            textsList.append(item);
            allTexts.append(rg.text);
        }

        // 4. 填充结果
        const QString fullText = allTexts.join(" ");
        result.overlayImage = overlay;
        result.ok = true;
        result.data["textCount"] = static_cast<int>(regions.size());
        result.data["fullText"] = fullText;

        // typed ports 输出（key=端口名, value=QVariant）
        result.ports["textCount"] = static_cast<int>(regions.size());
        result.ports["texts"] = textsList;
        result.ports["fullText"] = fullText;

        // score 取最大检测置信度
        double maxConf = 0.0;
        for (const auto& rg : regions) {
            maxConf = std::max(maxConf, static_cast<double>(rg.confidence));
        }
        result.score = maxConf;

        // 记录最近检测结果
        m_results["lastTextCount"] = static_cast<int>(regions.size());
        return true;
    } catch (const cv::Exception& e) {
        // OpenCV 异常：完全失败
        Logger::error(QString("DLOCRTool: OpenCV 异常: %1").arg(QString::fromStdString(e.what())));
        result.ok = false;
        result.data["error"] = QString::fromStdString(e.what());
        result.overlayImage = overlay;
        return false;
    } catch (const std::exception& e) {
        Logger::error(QString("DLOCRTool: 异常: %1").arg(e.what()));
        result.ok = false;
        result.data["error"] = e.what();
        result.overlayImage = overlay;
        return false;
    }
}

// ---------------------------------------------------------------
// typed ports 声明
// ---------------------------------------------------------------
QList<PortDescriptor> DLOCRTool::outputPorts() const {
    return {
        { QStringLiteral("textCount"), QStringLiteral("文本数"),   PortType::Number, PortDirection::Out,
          QStringLiteral("检测到的文本区域数量") },
        { QStringLiteral("texts"),      QStringLiteral("文本列表"), PortType::Points, PortDirection::Out,
          QStringLiteral("每个文本区域 {text,x,y,w,h,confidence}") },
        { QStringLiteral("fullText"),   QStringLiteral("全文"),     PortType::String, PortDirection::Out,
          QStringLiteral("所有文本拼接") },
    };
}

QList<PortDescriptor> DLOCRTool::inputPorts() const {
    return {
        { QStringLiteral("image"), QStringLiteral("图像"), PortType::Image, PortDirection::In,
          QStringLiteral("输入图像") },
    };
}

// ---------------------------------------------------------------
// 序列化/反序列化
// ---------------------------------------------------------------
QJsonObject DLOCRTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["detectionModelPath"]   = m_detectionModelPath;
    obj["recognitionModelPath"] = m_recognitionModelPath;
    obj["confThreshold"]        = m_confThreshold;
    obj["nmsThreshold"]         = m_nmsThreshold;
    obj["inputWidth"]           = m_inputWidth;
    obj["inputHeight"]          = m_inputHeight;
    obj["language"]             = m_language;
    obj["maxTextRegions"]       = m_maxTextRegions;
    return obj;
}

bool DLOCRTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;
    return configure(data);
}
