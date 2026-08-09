#include "FindShapeModelTool.h"
#include "Core/Logger.h"
#include <QJsonArray>
#include <opencv2/imgproc.hpp>
#include <cmath>

using namespace QDV;

FindShapeModelTool::FindShapeModelTool() {
    m_name = "形状匹配";
}

// 加载模板图: 使用 QFile 读取字节流后用 cv::imdecode 解码,
// 绕过 cv::imread 对中文路径在 Windows GBK 系统下的失败问题
bool FindShapeModelTool::loadTemplate() {
    m_template.release();

    if (m_templatePath.isEmpty()) {
        return false;
    }

    QFileInfo info(m_templatePath);
    if (!info.exists() || !info.isFile()) {
        Logger::error("FindShapeModelTool: template file not found: " + m_templatePath);
        return false;
    }

    QFile f(m_templatePath);
    if (!f.open(QIODevice::ReadOnly)) {
        Logger::error("FindShapeModelTool: cannot open template: " + m_templatePath);
        return false;
    }
    QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty()) {
        Logger::error("FindShapeModelTool: template file is empty: " + m_templatePath);
        return false;
    }

    cv::Mat raw = cv::imdecode(
        cv::Mat(1, bytes.size(), CV_8UC1, const_cast<char*>(bytes.constData())),
        cv::IMREAD_GRAYSCALE);
    if (raw.empty()) {
        Logger::error("FindShapeModelTool: failed to decode template: " + m_templatePath);
        return false;
    }

    m_template = raw;
    Logger::info(QString("FindShapeModelTool: template loaded %1 (%2x%3)")
        .arg(info.fileName()).arg(m_template.cols).arg(m_template.rows));
    return true;
}

// 绕模板中心旋转, 使用外扩画布保证旋转后内容不被裁剪
cv::Mat FindShapeModelTool::rotateTemplate(const cv::Mat& tpl, double angleDeg) const {
    if (angleDeg == 0.0) return tpl.clone();

    // 外接正方形边长 = 对角线长度, 确保任意角度旋转都不裁剪
    int len = static_cast<int>(std::sqrt(double(tpl.cols * tpl.cols + tpl.rows * tpl.rows)) + 0.5);
    len = std::max(len, std::max(tpl.cols, tpl.rows));

    // 将原模板居中贴到正方形画布上, 背景填充中性灰(128)
    // 用中性灰可避免旋转后边缘产生强梯度影响 NCC
    cv::Mat canvas = cv::Mat::zeros(len, len, tpl.type());
    canvas.setTo(cv::Scalar(128));
    int dx = (len - tpl.cols) / 2;
    int dy = (len - tpl.rows) / 2;
    cv::Rect roi(dx, dy, tpl.cols, tpl.rows);
    tpl.copyTo(canvas(roi));

    cv::Point2f center(static_cast<float>(len) / 2.0f, static_cast<float>(len) / 2.0f);
    cv::Mat rot = cv::getRotationMatrix2D(center, angleDeg, 1.0);
    cv::Mat dst;
    cv::warpAffine(canvas, dst, rot, cv::Size(len, len),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(128));
    return dst;
}

// P0-6a：各向异性缩放模板，ScaleX/ScaleY 独立
// 用 cv::resize 双三次插值；尺度为 1.0 时返回克隆避免无谓拷贝开销
cv::Mat FindShapeModelTool::scaleTemplate(const cv::Mat& tpl, double scaleX, double scaleY) const {
    if (std::abs(scaleX - 1.0) < 1e-6 && std::abs(scaleY - 1.0) < 1e-6) {
        return tpl.clone();
    }
    // 尺度钳制到合理范围，避免过大/过小导致内存爆炸或退化
    scaleX = std::max(0.1, std::min(scaleX, 10.0));
    scaleY = std::max(0.1, std::min(scaleY, 10.0));
    int newW = std::max(1, static_cast<int>(tpl.cols * scaleX + 0.5));
    int newH = std::max(1, static_cast<int>(tpl.rows * scaleY + 0.5));
    cv::Mat dst;
    cv::resize(tpl, dst, cv::Size(newW, newH), 0, 0, cv::INTER_CUBIC);
    return dst;
}

bool FindShapeModelTool::configure(const QJsonObject& params) {
    if (params.contains("templatePath")) {
        QString newPath = params["templatePath"].toString();
        if (newPath != m_templatePath) {
            m_templatePath = newPath;
            loadTemplate();
        }
    }

    if (params.contains("threshold")) {
        double t = params["threshold"].toDouble();
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        m_threshold = t;
    }

    if (params.contains("angleStart")) {
        m_angleStart = params["angleStart"].toDouble();
    }

    if (params.contains("angleExtent")) {
        // 钳制到 [0, 360]
        double ext = params["angleExtent"].toDouble();
        if (ext < 0.0) ext = 0.0;
        if (ext > 360.0) ext = 360.0;
        m_angleExtent = ext;
    }

    if (params.contains("maxMatches")) {
        int m = params["maxMatches"].toInt();
        if (m < 1) m = 1;
        if (m > 1000) m = 1000;
        m_maxMatches = m;
    }

    // P0-6a 各向异性尺度搜索参数解析
    if (params.contains("scaleMin"))  m_scaleMin = std::max(0.1, params["scaleMin"].toDouble());
    if (params.contains("scaleMax"))  m_scaleMax = std::max(m_scaleMin, params["scaleMax"].toDouble());
    if (params.contains("scaleStep")) m_scaleStep = std::max(0.01, params["scaleStep"].toDouble());
    if (params.contains("anisotropyEnabled")) m_anisotropyEnabled = params["anisotropyEnabled"].toBool();
    if (params.contains("scaleXRatio")) m_scaleXRatio = params["scaleXRatio"].toDouble();
    if (params.contains("scaleYRatio")) m_scaleYRatio = params["scaleYRatio"].toDouble();
    if (params.contains("ratioMin"))  m_ratioMin  = std::max(0.1, params["ratioMin"].toDouble());
    if (params.contains("ratioMax"))  m_ratioMax  = std::max(m_ratioMin, params["ratioMax"].toDouble());
    if (params.contains("ratioStep")) m_ratioStep = std::max(0.01, params["ratioStep"].toDouble());

    m_params = params;
    return true;
}

bool FindShapeModelTool::execute(const cv::Mat& input, ToolResult& result) {
    // 输入检查
    if (input.empty()) {
        Logger::error("FindShapeModelTool: input image is empty");
        result.ok = false;
        result.data["error"] = "Input image is empty";
        return false;
    }

    if (m_template.empty()) {
        Logger::error("FindShapeModelTool: template not loaded");
        result.ok = false;
        result.data["error"] = "Template not loaded";
        return false;
    }

    // 输入转灰度
    cv::Mat grayInput;
    if (input.channels() == 1) {
        grayInput = input.clone();
    } else if (input.channels() == 3) {
        cv::cvtColor(input, grayInput, cv::COLOR_BGR2GRAY);
    } else if (input.channels() == 4) {
        cv::cvtColor(input, grayInput, cv::COLOR_BGRA2GRAY);
    } else {
        grayInput = input.clone();
    }

    // P0-6a：构建尺度搜索列表
    // 当 scaleMin==scaleMax==1.0 时仅一次尺度（退化为原行为）
    std::vector<double> scales;
    for (double s = m_scaleMin; s <= m_scaleMax + 1e-6; s += m_scaleStep) {
        scales.push_back(s);
    }
    if (scales.empty()) scales.push_back(1.0);

    // 各向异性比率搜索列表（仅 anisotropyEnabled 且 ratioMin<ratioMax 时枚举）
    // ratio = ScaleX/ScaleY；ScaleX = scale*ratio, ScaleY = scale/ratio
    std::vector<double> ratios;
    if (m_anisotropyEnabled && m_ratioMax > m_ratioMin + 1e-6) {
        for (double r = m_ratioMin; r <= m_ratioMax + 1e-6; r += m_ratioStep) {
            ratios.push_back(r);
        }
    } else {
        ratios.push_back(1.0); // 各向同性
    }
    if (ratios.empty()) ratios.push_back(1.0);

    // 角度步长策略: 范围≤90°用 5° 步长, 否则用 10° 步长(平衡精度与耗时)
    double step = (m_angleExtent <= 90.0) ? 5.0 : 10.0;
    if (m_angleExtent <= 0.0) step = 360.0; // 退化为 0° 单次匹配

    double a = m_angleStart;
    double aEnd = m_angleStart + m_angleExtent;

    // 候选结构（含各向异性尺度信息）
    struct Cand {
        cv::Point pt;
        double score;
        double angle;
        double scaleX;
        double scaleY;
        int boxLen; // 该候选的框尺寸（随尺度变化）
    };

    // 遍历所有 (尺度, 比率, 角度) 组合，逐位置记录最优
    // bestScoreMap 在每个尺度组合内独立计算，再与全局最优合并
    std::vector<Cand> allCands;

    for (double scale : scales) {
        for (double ratio : ratios) {
            // 各向异性：ScaleX = scale*ratio, ScaleY = scale/ratio
            // 各向同性（ratio=1.0）时 ScaleX=ScaleY=scale
            double sx = scale * ratio;
            double sy = scale / ratio;

            cv::Mat scaledTpl = scaleTemplate(m_template, sx, sy);
            if (scaledTpl.empty()) continue;

            // 当前尺度下的外接框尺寸（旋转基准）
            int curBoxLen = static_cast<int>(std::sqrt(
                double(scaledTpl.cols * scaledTpl.cols +
                       scaledTpl.rows * scaledTpl.rows)) + 0.5);
            curBoxLen = std::max(curBoxLen, std::max(scaledTpl.cols, scaledTpl.rows));

            cv::Mat bestScoreMap;
            cv::Mat bestAngleMap;
            bool firstAng = true;

            for (double ang = a; ang < aEnd || (firstAng && m_angleExtent == 0.0); ang += step) {
                firstAng = false;

                cv::Mat rotated = rotateTemplate(scaledTpl, ang);
                if (rotated.cols > grayInput.cols || rotated.rows > grayInput.rows) {
                    continue;
                }

                cv::Mat score;
                cv::matchTemplate(grayInput, rotated, score, cv::TM_CCOEFF_NORMED);

                if (bestScoreMap.empty()) {
                    bestScoreMap = score.clone();
                    bestAngleMap = cv::Mat(score.size(), CV_32FC1, cv::Scalar(static_cast<float>(ang)));
                } else {
                    for (int y = 0; y < score.rows; ++y) {
                        const float* sRow = score.ptr<float>(y);
                        float* bRow = bestScoreMap.ptr<float>(y);
                        float* angRow = bestAngleMap.ptr<float>(y);
                        for (int x = 0; x < score.cols; ++x) {
                            if (sRow[x] > bRow[x]) {
                                bRow[x] = sRow[x];
                                angRow[x] = static_cast<float>(ang);
                            }
                        }
                    }
                }
                if (m_angleExtent == 0.0) break;
            }

            if (bestScoreMap.empty()) continue;

            // 收集该尺度组合下超过阈值的候选
            for (int y = 0; y < bestScoreMap.rows; ++y) {
                const float* sRow = bestScoreMap.ptr<float>(y);
                const float* aRow = bestAngleMap.ptr<float>(y);
                for (int x = 0; x < bestScoreMap.cols; ++x) {
                    if (sRow[x] >= static_cast<float>(m_threshold)) {
                        allCands.push_back({cv::Point(x, y),
                                            static_cast<double>(sRow[x]),
                                            static_cast<double>(aRow[x]),
                                            sx, sy, curBoxLen});
                    }
                }
            }
        }
    }

    if (allCands.empty()) {
        Logger::warn("FindShapeModelTool: no valid match across scale/angle");
        result.ok = false;
        result.data["error"] = "No valid match across scale/angle";
        return false;
    }

    // 按得分降序
    std::sort(allCands.begin(), allCands.end(),
        [](const Cand& a, const Cand& b) { return a.score > b.score; });

    // NMS 去重：使用各候选自身的 boxLen（各向异性时框尺寸不同）
    const double iouThr = 0.3;
    std::vector<Cand> kept;
    for (const auto& c : allCands) {
        if (static_cast<int>(kept.size()) >= m_maxMatches) break;
        cv::Rect box(c.pt.x, c.pt.y, c.boxLen, c.boxLen);
        bool keep = true;
        for (const auto& k : kept) {
            cv::Rect kb(k.pt.x, k.pt.y, k.boxLen, k.boxLen);
            int x1 = std::max(box.x, kb.x);
            int y1 = std::max(box.y, kb.y);
            int x2 = std::min(box.x + box.width, kb.x + kb.width);
            int y2 = std::min(box.y + box.height, kb.y + kb.height);
            int iw = std::max(0, x2 - x1);
            int ih = std::max(0, y2 - y1);
            double inter = static_cast<double>(iw) * ih;
            double uni = static_cast<double>(box.width) * box.height +
                         static_cast<double>(k.boxLen) * k.boxLen - inter;
            if (uni > 0.0 && (inter / uni) > iouThr) {
                keep = false;
                break;
            }
        }
        if (keep) kept.push_back(c);
    }

    // 准备 overlayImage(BGR)
    cv::Mat overlay;
    if (input.channels() == 3) {
        overlay = input.clone();
    } else if (input.channels() == 1) {
        cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
    } else if (input.channels() == 4) {
        cv::cvtColor(input, overlay, cv::COLOR_BGRA2BGR);
    } else {
        overlay = input.clone();
    }

    // 绘制: 绿色矩形 + "得分@角度" 标签
    QJsonArray matchesArr;
    double bestScore = 0.0;
    for (const auto& k : kept) {
        if (k.score > bestScore) bestScore = k.score;

        // P0-6a：使用各候选自身的 boxLen（各向异性时随尺度变化）
        cv::Rect box(k.pt.x, k.pt.y, k.boxLen, k.boxLen);
        // 钳制到图像范围内,避免框线越界
        box = box & cv::Rect(0, 0, overlay.cols, overlay.rows);
        cv::rectangle(overlay, box.tl(), box.br(), cv::Scalar(0, 255, 0), 2);

        // 标签含尺度信息（非 1.0 时显示）
        QString label;
        if (std::abs(k.scaleX - 1.0) < 1e-3 && std::abs(k.scaleY - 1.0) < 1e-3) {
            label = QString("%1@%2°").arg(k.score, 0, 'f', 3).arg(k.angle, 0, 'f', 1);
        } else {
            label = QString("%1@%2°(%3,%4)")
                .arg(k.score, 0, 'f', 3)
                .arg(k.angle, 0, 'f', 1)
                .arg(k.scaleX, 0, 'f', 2)
                .arg(k.scaleY, 0, 'f', 2);
        }
        cv::putText(overlay, label.toStdString(),
                    cv::Point(box.x, std::max(box.y - 5, 12)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(0, 255, 0), 1, cv::LINE_AA);

        // 在框中心画十字标记匹配位置
        cv::Point center(box.x + box.width / 2, box.y + box.height / 2);
        cv::drawMarker(overlay, center, cv::Scalar(0, 255, 0),
                       cv::MARKER_CROSS, 12, 1, cv::LINE_AA);

        QJsonObject m;
        m["x"] = box.x;
        m["y"] = box.y;
        m["width"] = box.width;
        m["height"] = box.height;
        m["cx"] = center.x;
        m["cy"] = center.y;
        m["score"] = k.score;
        m["angle"] = k.angle;
        // P0-6a：输出各向异性尺度
        m["scaleX"] = k.scaleX;
        m["scaleY"] = k.scaleY;
        matchesArr.append(m);
    }

    result.ok = !kept.empty();
    result.score = bestScore;
    result.overlayImage = overlay;

    result.data["count"] = static_cast<int>(kept.size());
    result.data["matches"] = matchesArr;
    result.data["threshold"] = m_threshold;
    result.data["angleStart"] = m_angleStart;
    result.data["angleExtent"] = m_angleExtent;
    result.data["scaleMin"] = m_scaleMin;
    result.data["scaleMax"] = m_scaleMax;
    result.data["anisotropyEnabled"] = m_anisotropyEnabled;
    result.data["templatePath"] = m_templatePath;

    m_results["lastMatchCount"] = static_cast<int>(kept.size());
    m_results["lastBestScore"] = bestScore;

    Logger::info(QString("FindShapeModelTool: matched %1 (best=%2)")
        .arg(kept.size()).arg(bestScore, 0, 'f', 3));

    return true;
}

QJsonObject FindShapeModelTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["templatePath"] = m_templatePath;
    obj["threshold"] = m_threshold;
    obj["angleStart"] = m_angleStart;
    obj["angleExtent"] = m_angleExtent;
    obj["maxMatches"] = m_maxMatches;
    // P0-6a 各向异性尺度搜索参数
    obj["scaleMin"] = m_scaleMin;
    obj["scaleMax"] = m_scaleMax;
    obj["scaleStep"] = m_scaleStep;
    obj["anisotropyEnabled"] = m_anisotropyEnabled;
    obj["scaleXRatio"] = m_scaleXRatio;
    obj["scaleYRatio"] = m_scaleYRatio;
    obj["ratioMin"] = m_ratioMin;
    obj["ratioMax"] = m_ratioMax;
    obj["ratioStep"] = m_ratioStep;
    return obj;
}

bool FindShapeModelTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    // 使用 contains 检查,避免缺失字段被覆盖为默认值
    if (data.contains("templatePath")) {
        m_templatePath = data["templatePath"].toString();
        loadTemplate();
    }
    if (data.contains("threshold")) {
        double t = data["threshold"].toDouble();
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        m_threshold = t;
    }
    if (data.contains("angleStart")) m_angleStart = data["angleStart"].toDouble();
    if (data.contains("angleExtent")) {
        double ext = data["angleExtent"].toDouble();
        if (ext < 0.0) ext = 0.0;
        if (ext > 360.0) ext = 360.0;
        m_angleExtent = ext;
    }
    if (data.contains("maxMatches")) {
        int m = data["maxMatches"].toInt();
        if (m < 1) m = 1;
        if (m > 1000) m = 1000;
        m_maxMatches = m;
    }
    // P0-6a 各向异性尺度搜索参数
    if (data.contains("scaleMin"))  m_scaleMin = std::max(0.1, data["scaleMin"].toDouble());
    if (data.contains("scaleMax"))  m_scaleMax = std::max(m_scaleMin, data["scaleMax"].toDouble());
    if (data.contains("scaleStep")) m_scaleStep = std::max(0.01, data["scaleStep"].toDouble());
    if (data.contains("anisotropyEnabled")) m_anisotropyEnabled = data["anisotropyEnabled"].toBool();
    if (data.contains("scaleXRatio")) m_scaleXRatio = data["scaleXRatio"].toDouble();
    if (data.contains("scaleYRatio")) m_scaleYRatio = data["scaleYRatio"].toDouble();
    if (data.contains("ratioMin"))  m_ratioMin  = std::max(0.1, data["ratioMin"].toDouble());
    if (data.contains("ratioMax"))  m_ratioMax  = std::max(m_ratioMin, data["ratioMax"].toDouble());
    if (data.contains("ratioStep")) m_ratioStep = std::max(0.01, data["ratioStep"].toDouble());
    return true;
}
