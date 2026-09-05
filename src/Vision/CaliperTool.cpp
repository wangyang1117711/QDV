#include "CaliperTool.h"
#include "Core/Logger.h"
#include <QJsonArray>
#include <QVariantMap>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace QDV;

CaliperTool::CaliperTool() {
    m_name = "卡尺测量";
}

bool CaliperTool::configure(const QJsonObject& params) {
    if (params.contains("roiX"))           m_roiX           = params["roiX"].toInt();
    if (params.contains("roiY"))           m_roiY           = params["roiY"].toInt();
    if (params.contains("roiW"))           m_roiW           = params["roiW"].toInt();
    if (params.contains("roiH"))           m_roiH           = params["roiH"].toInt();
    if (params.contains("searchWidth"))    m_searchWidth    = params["searchWidth"].toInt();
    if (params.contains("searchLength"))   m_searchLength   = params["searchLength"].toInt();
    if (params.contains("edgeThreshold"))  m_edgeThreshold  = params["edgeThreshold"].toDouble();
    if (params.contains("polarity")) {
        const QString p = params["polarity"].toString();
        if (p == "any" || p == "dark_to_bright" || p == "bright_to_dark") {
            m_polarity = p;
        } else {
            Logger::warn(QString("CaliperTool: polarity '%1' 非法，已回退到 'any'").arg(p));
            m_polarity = "any";
        }
    }
    if (params.contains("smoothSigma")) {
        const double s = params["smoothSigma"].toDouble();
        // sigma 负值无意义，0 表示不平滑
        m_smoothSigma = (s < 0.0) ? 0.0 : s;
    }
    // P 优化：Sobel/中心差分切换（默认 Sobel，更适合工业软边）
    if (params.contains("useSobel")) {
        m_useSobel = params["useSobel"].toBool(true);
    }
    // P 优化：自适应阈值降级开关
    if (params.contains("autoThreshold")) {
        m_autoThreshold = params["autoThreshold"].toBool(true);
    }

    // 钳制尺寸下限，避免退化
    if (m_roiW < 1) m_roiW = 1;
    if (m_roiH < 1) m_roiH = 1;
    if (m_searchWidth < 1) m_searchWidth = 1;
    if (m_searchLength < 1) m_searchLength = 1;
    if (m_edgeThreshold < 0.0) m_edgeThreshold = 0.0;

    // spec Task 3：私有 ROI 迁移到基类（向下兼容旧方案）
    // 如果 params 含 roiX/roiY/roiW/roiH 且基类 ROI 未被外部设置，自动转为基类矩形 ROI
    // 迁移后 ToolChainExecutor 会自动裁剪图像，execute 内部用全图（hasRoi 判断）
    if (!hasRoi() &&
        (params.contains("roiX") || params.contains("roiY") ||
         params.contains("roiW") || params.contains("roiH"))) {
        QVariantMap roiMap;
        roiMap["type"] = "rect";
        roiMap["x"] = m_roiX;
        roiMap["y"] = m_roiY;
        roiMap["w"] = m_roiW;
        roiMap["h"] = m_roiH;
        setRoi(roiMap);
    }

    m_params = params;
    return true;
}

bool CaliperTool::execute(const cv::Mat& input, ToolResult& result) {
    try {
        if (input.empty()) {
            result.ok = false;
            result.data["error"] = "Input image is empty";
            return false;
        }

        // 转灰度
        cv::Mat gray;
        if (input.channels() == 3) {
            cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
        } else if (input.channels() == 4) {
            cv::cvtColor(input, gray, cv::COLOR_BGRA2GRAY);
        } else {
            gray = input.clone();
        }
        if (gray.type() != CV_8UC1) {
            gray.convertTo(gray, CV_8U);
        }

        // 钳制 ROI 到图像范围
        // spec Task 3：基类有 ROI 时 ToolChainExecutor 已裁剪图像，内部 ROI 用全图
        // 基类无 ROI 时用旧成员（向下兼容，理论上迁移后不会走到此分支）
        cv::Rect roi;
        if (hasRoi()) {
            roi = cv::Rect(0, 0, gray.cols, gray.rows);
        } else {
            roi = cv::Rect(m_roiX, m_roiY, m_roiW, m_roiH);
            roi = roi & cv::Rect(0, 0, gray.cols, gray.rows);
        }
        if (roi.width < 3 || roi.height < 3) {
            result.ok = false;
            result.data["error"] = "ROI too small or out of image bounds";
            return false;
        }

        // 测量方向自动判定：ROI 宽≥高 → 水平方向，否则垂直方向
        const bool horizontal = (roi.width >= roi.height);

        // 在 ROI 内定义搜索带（以 ROI 中心为中心）
        // searchLength 沿测量方向，searchWidth 垂直测量方向
        int len = horizontal ? std::min(m_searchLength, roi.width)
                             : std::min(m_searchLength, roi.height);
        int wid = horizontal ? std::min(m_searchWidth, roi.height)
                             : std::min(m_searchWidth, roi.width);
        if (len < 3) len = horizontal ? roi.width : roi.height;
        if (wid < 1) wid = 1;

        cv::Rect band;
        const int cx = roi.x + roi.width / 2;
        const int cy = roi.y + roi.height / 2;
        if (horizontal) {
            band.x = cx - len / 2;
            band.y = cy - wid / 2;
            band.width = len;
            band.height = wid;
        } else {
            band.x = cx - wid / 2;
            band.y = cy - len / 2;
            band.width = wid;
            band.height = len;
        }
        band = band & cv::Rect(0, 0, gray.cols, gray.rows);
        if (band.width < 3 || band.height < 1) {
            result.ok = false;
            result.data["error"] = "Search band too small";
            return false;
        }

        // 高斯平滑搜索带（降低噪声对梯度的影响）
        cv::Mat smoothed;
        if (m_smoothSigma > 0.0) {
            cv::GaussianBlur(gray(band), smoothed, cv::Size(0, 0), m_smoothSigma);
        } else {
            smoothed = gray(band).clone();
        }

        // 投影：沿垂直测量方向求平均，得到 1D profile
        // 水平测量 → 对每列求 Y 方向平均；垂直测量 → 对每行求 X 方向平均
        const int profileLen = horizontal ? band.width : band.height;
        std::vector<double> profile(profileLen, 0.0);
        if (horizontal) {
            for (int x = 0; x < band.width; ++x) {
                double sum = 0.0;
                for (int y = 0; y < band.height; ++y) {
                    sum += smoothed.at<uchar>(y, x);
                }
                profile[x] = sum / std::max(1, band.height);
            }
        } else {
            for (int y = 0; y < band.height; ++y) {
                double sum = 0.0;
                for (int x = 0; x < band.width; ++x) {
                    sum += smoothed.at<uchar>(y, x);
                }
                profile[y] = sum / std::max(1, band.width);
            }
        }

        // P 优化：用 Sobel 算子替代中心差分，更适合工业软边（金属件/纹理件）
        // 软边的圆周通常是高斯型渐变带，单像素中心差分会把梯度分散到很多像素，
        // 单点梯度 max 很小（典型 1~5），导致传统中心差分+固定阈值失效。
        // Sobel 算子（3x3）通过二维高斯加权差分，能在更大尺度上捕捉软边的整体跃迁。
        // 等价实现：profile 上的 Sobel 一维展开 = [1,2,1]/4 - [-1,0,1]/2
        //        = [-1, -2, 0, 2, 1] / 4
        // 这里直接按 Sobel 模板逐位置卷积。
        std::vector<double> grad(profileLen, 0.0);
        if (m_useSobel) {
            // Sobel 一维核（沿 profile 方向，对应测量方向）
            // 水平测量 → 沿 X 求导 → Sobel X = [-1,0,1; -2,0,2; -1,0,1]
            //   profile 是 band 沿 Y 的平均，已是 1D 信号，等价于对 band 做了 Y 均值后再 X 求导
            //   由于 Y 均值会平滑边缘梯度，这里额外使用 cv::Sobel 在 2D 上算（更准）
            cv::Mat g32;
            // 对 2D band 直接 Sobel 取 X 或 Y 方向
            int dx = horizontal ? 1 : 0;
            int dy = horizontal ? 0 : 1;
            cv::Sobel(smoothed, g32, CV_32F, dx, dy, 3, 1.0/8.0);  // scale 1/8 = Sobel 归一化
            // 再投影到 1D
            if (horizontal) {
                for (int x = 0; x < band.width; ++x) {
                    double sum = 0.0;
                    for (int y = 0; y < band.height; ++y) {
                        sum += g32.at<float>(y, x);
                    }
                    grad[x] = sum / std::max(1, band.height);
                }
            } else {
                for (int y = 0; y < band.height; ++y) {
                    double sum = 0.0;
                    for (int x = 0; x < band.width; ++x) {
                        sum += g32.at<float>(y, x);
                    }
                    grad[y] = sum / std::max(1, band.width);
                }
            }
        } else {
            // 原中心差分实现（保留兼容，硬边场景仍可用）
            for (int i = 1; i + 1 < profileLen; ++i) {
                grad[i] = (profile[i + 1] - profile[i - 1]) / 2.0;
            }
        }

        // P 优化：自适应阈值降级（仅当用户给的高阈值找不到边缘时启用）
        // 软边场景（如金属件渐变圆周）单点梯度可能仅 1~5，用户预设阈值 30 会失败。
        // 此机制自动降阈值到 mean(|grad|)*1.5（至少 2.0）保证能识别软边。
        // 触发条件：用户初始阈值 ≥ 5 且边缘数不足 2。
        struct EdgePt { double pos; double gradVal; int pol; };
        double usedThr = m_edgeThreshold;
        double initialThr = m_edgeThreshold;
        std::vector<EdgePt> edges;
        // 提取"在给定阈值下找边缘"为 lambda，便于自适应降阈值
        auto findEdges = [&](double thr) -> std::vector<EdgePt> {
            std::vector<EdgePt> es;
            for (int i = 1; i + 1 < profileLen; ++i) {
                const double g = grad[i];
                int pol = 0;
                bool match = false;
                if (m_polarity == "dark_to_bright") {
                    if (g > thr) { match = true; pol = 1; }
                } else if (m_polarity == "bright_to_dark") {
                    if (g < -thr) { match = true; pol = -1; }
                } else { // any
                    if (std::fabs(g) > thr) {
                        match = true;
                        pol = (g > 0.0) ? 1 : -1;
                    }
                }
                if (!match) continue;

                // 局部极值检测：正极性取局部最大，负极性取局部最小
                // 避免一个边缘平台产生多个点
                bool isPeak = true;
                if (pol > 0) {
                    if (grad[i - 1] > g) isPeak = false;
                    if (grad[i + 1] > g) isPeak = false;
                } else {
                    if (grad[i - 1] < g) isPeak = false;
                    if (grad[i + 1] < g) isPeak = false;
                }
                if (!isPeak) continue;

                // 抛物线亚像素插值：d = 0.5*(gPrev - gNext) / (gPrev - 2*gCurr + gNext)
                double subOffset = 0.0;
                const double gPrev = grad[i - 1];
                const double gNext = grad[i + 1];
                const double denom = gPrev - 2.0 * g + gNext;
                if (std::fabs(denom) > 1e-9) {
                    subOffset = 0.5 * (gPrev - gNext) / denom;
                    subOffset = std::clamp(subOffset, -1.0, 1.0);
                }
                es.push_back({static_cast<double>(i) + subOffset, g, pol});
            }
            return es;
        };

        edges = findEdges(usedThr);
        // 自适应降阈值（仅当启用时）
        if (m_autoThreshold && edges.size() < 2 && initialThr >= 5.0) {
            double meanAG = 0.0;
            for (int i = 0; i < profileLen; ++i) meanAG += std::fabs(grad[i]);
            meanAG /= std::max(1, profileLen);
            double dynamicThr = std::max(2.0, meanAG * 1.5);
            if (dynamicThr < initialThr) {
                usedThr = dynamicThr;
                edges = findEdges(usedThr);
                Logger::info(QString("CaliperTool: 自适应阈值降级 %1 → %2 (找到 %3 个边缘)")
                                  .arg(initialThr, 0, 'f', 1).arg(usedThr, 0, 'f', 1).arg(edges.size()));
            }
        }

        if (edges.size() < 2) {
            result.ok = false;
            result.data["error"] = QString("Not enough edges found (%1)").arg(edges.size());
            result.overlayImage = input.clone();
            return false;
        }

        // 取前两个边缘点计算间距
        const EdgePt& e1 = edges[0];
        const EdgePt& e2 = edges[1];

        // 转换为图像坐标
        double x1, y1, x2, y2, distance;
        if (horizontal) {
            x1 = band.x + e1.pos;
            y1 = band.y + band.height / 2.0;
            x2 = band.x + e2.pos;
            y2 = band.y + band.height / 2.0;
            distance = std::fabs(x2 - x1);
        } else {
            x1 = band.x + band.width / 2.0;
            y1 = band.y + e1.pos;
            x2 = band.x + band.width / 2.0;
            y2 = band.y + e2.pos;
            distance = std::fabs(y2 - y1);
        }

        // 输出 typed ports
        QVariantMap edgePos1, edgePos2;
        edgePos1.insert("x", x1);
        edgePos1.insert("y", y1);
        edgePos2.insert("x", x2);
        edgePos2.insert("y", y2);
        result.ports["edgePos1"] = edgePos1;
        result.ports["edgePos2"] = edgePos2;
        result.ports["distance"] = distance;

        // JSON 输出同步
        result.data["distance"] = distance;
        QJsonObject j1; j1["x"] = x1; j1["y"] = y1;
        QJsonObject j2; j2["x"] = x2; j2["y"] = y2;
        result.data["edge1"] = j1;
        result.data["edge2"] = j2;
        result.data["polarity"] = m_polarity;
        result.data["edgeCount"] = static_cast<int>(edges.size());

        result.score = distance;
        result.ok = true;

        // overlayImage：绘制卡尺框 + 边缘点 + 连线
        if (input.channels() == 1) {
            cv::cvtColor(input, result.overlayImage, cv::COLOR_GRAY2BGR);
        } else if (input.channels() == 4) {
            cv::cvtColor(input, result.overlayImage, cv::COLOR_BGRA2BGR);
        } else {
            result.overlayImage = input.clone();
        }

        // 卡尺搜索带（黄色框）
        cv::rectangle(result.overlayImage, band.tl(), band.br(),
                      cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
        // ROI 外框（青色，便于对照）
        cv::rectangle(result.overlayImage, roi.tl(), roi.br(),
                      cv::Scalar(255, 255, 0), 1, cv::LINE_AA);

        // 边缘点（绿色圆）+ 连线（绿色）
        const cv::Point p1(static_cast<int>(std::round(x1)),
                           static_cast<int>(std::round(y1)));
        const cv::Point p2(static_cast<int>(std::round(x2)),
                           static_cast<int>(std::round(y2)));
        cv::circle(result.overlayImage, p1, 5, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
        cv::circle(result.overlayImage, p2, 5, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
        cv::line(result.overlayImage, p1, p2, cv::Scalar(0, 255, 0), 1, cv::LINE_AA);

        // 间距标签
        const QString label = QString("%1 px").arg(distance, 0, 'f', 2);
        cv::putText(result.overlayImage, label.toStdString(),
                    cv::Point(std::min(p1.x, p2.x),
                              std::min(p1.y, p2.y) - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(0, 255, 0), 1, cv::LINE_AA);

        return true;
    } catch (const cv::Exception& e) {
        Logger::error(QString("CaliperTool: OpenCV 异常: %1").arg(e.what()));
        result.ok = false;
        result.data["error"] = QString("OpenCV exception: %1").arg(e.what());
        return false;
    } catch (const std::exception& e) {
        Logger::error(QString("CaliperTool: 异常: %1").arg(e.what()));
        result.ok = false;
        result.data["error"] = QString("Exception: %1").arg(e.what());
        return false;
    }
}

QList<PortDescriptor> CaliperTool::outputPorts() const {
    return {
        {"distance", QStringLiteral("间距"), PortType::Number, PortDirection::Out,
         QStringLiteral("两边缘点亚像素间距（像素）")},
        {"edgePos1", QStringLiteral("边缘1"), PortType::Points, PortDirection::Out,
         QStringLiteral("第一个边缘点 {x,y}")},
        {"edgePos2", QStringLiteral("边缘2"), PortType::Points, PortDirection::Out,
         QStringLiteral("第二个边缘点 {x,y}")},
    };
}

QList<PortDescriptor> CaliperTool::inputPorts() const {
    return {
        {"image", QStringLiteral("图像"), PortType::Image, PortDirection::In,
         QStringLiteral("输入图像")},
    };
}

QJsonObject CaliperTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["roiX"] = m_roiX;
    obj["roiY"] = m_roiY;
    obj["roiW"] = m_roiW;
    obj["roiH"] = m_roiH;
    obj["searchWidth"] = m_searchWidth;
    obj["searchLength"] = m_searchLength;
    obj["edgeThreshold"] = m_edgeThreshold;
    obj["polarity"] = m_polarity;
    obj["smoothSigma"] = m_smoothSigma;
    // P 优化：序列化新参数
    obj["useSobel"] = m_useSobel;
    obj["autoThreshold"] = m_autoThreshold;
    return obj;
}

bool CaliperTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("roiX"))          m_roiX          = data["roiX"].toInt();
    if (data.contains("roiY"))          m_roiY          = data["roiY"].toInt();
    if (data.contains("roiW"))          m_roiW          = data["roiW"].toInt();
    if (data.contains("roiH"))          m_roiH          = data["roiH"].toInt();
    if (data.contains("searchWidth"))   m_searchWidth   = data["searchWidth"].toInt();
    if (data.contains("searchLength"))  m_searchLength  = data["searchLength"].toInt();
    if (data.contains("edgeThreshold")) m_edgeThreshold = data["edgeThreshold"].toDouble();
    if (data.contains("polarity")) {
        const QString p = data["polarity"].toString();
        if (p == "any" || p == "dark_to_bright" || p == "bright_to_dark") {
            m_polarity = p;
        }
    }
    if (data.contains("smoothSigma")) {
        const double s = data["smoothSigma"].toDouble();
        m_smoothSigma = (s < 0.0) ? 0.0 : s;
    }
    // P 优化：反序列化新参数（缺省走默认值，向后兼容）
    if (data.contains("useSobel"))       m_useSobel       = data["useSobel"].toBool(true);
    if (data.contains("autoThreshold"))  m_autoThreshold  = data["autoThreshold"].toBool(true);

    // 反序列化后同样钳制尺寸下限
    if (m_roiW < 1) m_roiW = 1;
    if (m_roiH < 1) m_roiH = 1;
    if (m_searchWidth < 1) m_searchWidth = 1;
    if (m_searchLength < 1) m_searchLength = 1;
    if (m_edgeThreshold < 0.0) m_edgeThreshold = 0.0;

    // spec Task 3：旧方案自动迁移
    // 旧方案 data 含 roiX/roiY/roiW/roiH 但不含 "roi" 字段（VisionTool::deserialize 已处理 "roi"）
    // 如果基类 ROI 为空且旧成员有效，迁移到基类 ROI，行为不变
    if (!hasRoi() &&
        (data.contains("roiX") || data.contains("roiY") ||
         data.contains("roiW") || data.contains("roiH"))) {
        QVariantMap roiMap;
        roiMap["type"] = "rect";
        roiMap["x"] = m_roiX;
        roiMap["y"] = m_roiY;
        roiMap["w"] = m_roiW;
        roiMap["h"] = m_roiH;
        setRoi(roiMap);
    }
    return true;
}
