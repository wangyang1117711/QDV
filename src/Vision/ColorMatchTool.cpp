#include "Vision/ColorMatchTool.h"
#include "Core/Logger.h"
#include <QVariantMap>
#include <opencv2/imgproc.hpp>
#include <algorithm>

using namespace QDV;

ColorMatchTool::ColorMatchTool() {
    m_name = "颜色比对";
}

// 加载标准样片图：QFile 读取字节流 + cv::imdecode 解码，绕过 cv::imread 中文路径问题
// 解码为彩色图（BGR），直方图比对需要三通道
bool ColorMatchTool::loadTemplate() {
    m_template.release();

    if (m_templatePath.isEmpty()) {
        return false;
    }

    QFileInfo info(m_templatePath);
    if (!info.exists() || !info.isFile()) {
        Logger::error("ColorMatchTool: template file not found: " + m_templatePath);
        return false;
    }

    QFile f(m_templatePath);
    if (!f.open(QIODevice::ReadOnly)) {
        Logger::error("ColorMatchTool: cannot open template: " + m_templatePath);
        return false;
    }
    QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty()) {
        Logger::error("ColorMatchTool: template file is empty: " + m_templatePath);
        return false;
    }

    // 解码为彩色图
    cv::Mat raw = cv::imdecode(
        cv::Mat(1, bytes.size(), CV_8UC1, const_cast<char*>(bytes.constData())),
        cv::IMREAD_COLOR);
    if (raw.empty()) {
        Logger::error("ColorMatchTool: failed to decode template: " + m_templatePath);
        return false;
    }

    m_template = raw;
    Logger::info(QString("ColorMatchTool: template loaded %1 (%2x%3)")
        .arg(info.fileName()).arg(m_template.cols).arg(m_template.rows));
    return true;
}

// 将方法字符串映射为 cv::HISTCMP_* 枚举
int ColorMatchTool::histMethodCode() const {
    if (m_histMethod == "ChiSqr")        return cv::HISTCMP_CHISQR;
    if (m_histMethod == "Intersect")     return cv::HISTCMP_INTERSECT;
    if (m_histMethod == "Bhattacharyya") return cv::HISTCMP_BHATTACHARYYA;
    return cv::HISTCMP_CORREL;  // 默认 Correl
}

// 色彩空间转换：RGB 不转换（OpenCV 默认 BGR，三通道直接用），HSV/Lab 做 cvtColor
cv::Mat ColorMatchTool::convertColorSpace(const cv::Mat& bgr) const {
    if (bgr.channels() != 3) return bgr.clone();
    cv::Mat out;
    if (m_colorSpace == "HSV") {
        cv::cvtColor(bgr, out, cv::COLOR_BGR2HSV);
    } else if (m_colorSpace == "Lab") {
        cv::cvtColor(bgr, out, cv::COLOR_BGR2Lab);
    } else {
        // "RGB" 或其他：保持 BGR 三通道（颜色直方图对通道顺序不敏感，比对双方一致即可）
        out = bgr.clone();
    }
    return out;
}

// 计算多通道直方图并归一化到 [0,1]
// 对 3 通道图像，每通道用相同 bins 数，范围取该色彩空间的全范围
cv::Mat ColorMatchTool::computeHist(const cv::Mat& img) const {
    cv::Mat converted = convertColorSpace(img);
    int channels = converted.channels();
    if (channels < 1) channels = 1;

    // 各色彩空间的通道范围
    // HSV: H∈[0,180], S∈[0,256], V∈[0,256]
    // Lab: L∈[0,256], a∈[0,256], b∈[0,256]
    // RGB/BGR: 三通道均 [0,256]
    std::vector<int> histSize(channels, m_bins);
    std::vector<float> ranges;
    if (m_colorSpace == "HSV" && channels == 3) {
        // H 通道范围 [0,180]，S/V 通道范围 [0,256]
        ranges = {0, 180, 0, 256, 0, 256};
    } else {
        // 其余色彩空间所有通道范围 [0,256]
        ranges.resize(channels * 2);
        for (int i = 0; i < channels; ++i) {
            ranges[i * 2]     = 0.0f;
            ranges[i * 2 + 1] = 256.0f;
        }
    }

    // std::vector 转 const float** 需要 ranges 数组的连续存储
    // cv::calcHist 接受 const float* ranges[] 形式，这里用数组指针
    std::vector<const float*> rangePtrs(channels);
    for (int i = 0; i < channels; ++i) {
        rangePtrs[i] = &ranges[i * 2];
    }
    std::vector<int> chIdx(channels);
    for (int i = 0; i < channels; ++i) chIdx[i] = i;

    cv::Mat hist;
    cv::calcHist(&converted, 1, chIdx.data(), cv::Mat(),
                 hist, channels, histSize.data(), rangePtrs.data(), true, false);
    // 归一化到 [0,1]，便于跨方法比较
    cv::normalize(hist, hist, 0, 1, cv::NORM_MINMAX, -1, cv::Mat());
    return hist;
}

// 比对方法是否为"越大越好"
bool ColorMatchTool::isHigherBetter() const {
    return (m_histMethod == "Correl" || m_histMethod == "Intersect");
}

// 将原始距离转换为 [0,1] 分数
// - 越大越好方法：直接钳制到 [0,1]（Correl∈[-1,1]，Intersect 已归一化）
// - 越小越好方法：用 1/(1+distance) 将无界距离映射到 [0,1]
double ColorMatchTool::toScore(double distance) const {
    if (isHigherBetter()) {
        if (distance < 0.0) return 0.0;
        if (distance > 1.0) return 1.0;
        return distance;
    } else {
        if (distance < 0.0) distance = 0.0;
        return 1.0 / (1.0 + distance);
    }
}

bool ColorMatchTool::configure(const QJsonObject& params) {
    if (params.contains("templatePath")) {
        QString newPath = params["templatePath"].toString();
        if (newPath != m_templatePath) {
            m_templatePath = newPath;
            loadTemplate();
        }
    }

    if (params.contains("colorSpace")) {
        QString cs = params["colorSpace"].toString();
        if (cs == "HSV" || cs == "RGB" || cs == "Lab") {
            m_colorSpace = cs;
        } else {
            Logger::warn("ColorMatchTool: invalid colorSpace, using default HSV");
            m_colorSpace = "HSV";
        }
    }

    if (params.contains("histMethod")) {
        QString hm = params["histMethod"].toString();
        if (hm == "Correl" || hm == "ChiSqr" || hm == "Intersect" || hm == "Bhattacharyya") {
            m_histMethod = hm;
        } else {
            Logger::warn("ColorMatchTool: invalid histMethod, using default Correl");
            m_histMethod = "Correl";
        }
    }

    if (params.contains("threshold")) {
        double t = params["threshold"].toDouble();
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        m_threshold = t;
    }

    if (params.contains("bins")) {
        int b = params["bins"].toInt();
        if (b < 1) b = 1;
        if (b > 256) b = 256;
        m_bins = b;
    }

    if (params.contains("roiX")) m_roiX = params["roiX"].toInt();
    if (params.contains("roiY")) m_roiY = params["roiY"].toInt();
    if (params.contains("roiW")) m_roiW = params["roiW"].toInt();
    if (params.contains("roiH")) m_roiH = params["roiH"].toInt();

    // spec Task 3：私有 ROI 迁移到基类（向下兼容旧方案）
    // ColorMatchTool 旧约定：roiW=0/roiH=0 表示全图，仅当 roiW>0 && roiH>0 时迁移
    // 迁移后 ToolChainExecutor 自动裁剪图像，execute 内部用全图（hasRoi 判断）
    if (!hasRoi() && m_roiW > 0 && m_roiH > 0) {
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

bool ColorMatchTool::execute(const cv::Mat& input, ToolResult& result) {
    try {
        // 输入校验
        if (input.empty()) {
            Logger::error("ColorMatchTool: input image is empty");
            result.ok = false;
            result.data["error"] = "Input image is empty";
            return false;
        }

        // 标准样片必须已加载
        if (m_template.empty()) {
            Logger::error("ColorMatchTool: template not loaded");
            result.ok = false;
            result.data["error"] = "Template not loaded";
            return false;
        }

        // 提取 ROI（roiW/roiH 为 0 表示全图）
        // spec Task 3：基类有 ROI 时 ToolChainExecutor 已裁剪图像，内部 ROI 用全图
        // 基类无 ROI 时用旧成员（向下兼容）
        cv::Rect roi(0, 0, input.cols, input.rows);
        if (hasRoi()) {
            // 基类有 ROI：图像已被 ToolChainExecutor 裁剪，内部用全图
            roi = cv::Rect(0, 0, input.cols, input.rows);
        } else if (m_roiW > 0 && m_roiH > 0) {
            // 基类无 ROI：用旧成员（向下兼容）
            cv::Rect r(m_roiX, m_roiY, m_roiW, m_roiH);
            roi = r & cv::Rect(0, 0, input.cols, input.rows);  // 钳制到图像范围内
        }
        cv::Mat inputROI = input(roi);

        // 计算两图直方图
        cv::Mat histInput = computeHist(inputROI);
        cv::Mat histTpl   = computeHist(m_template);
        if (histInput.empty() || histTpl.empty()) {
            Logger::error("ColorMatchTool: failed to compute histogram");
            result.ok = false;
            result.data["error"] = "Failed to compute histogram";
            return false;
        }

        // 直方图比对
        double distance = cv::compareHist(histInput, histTpl, histMethodCode());
        double score = toScore(distance);
        bool isMatch = (score >= m_threshold);

        // 写入 typed ports
        result.ports["score"]    = score;
        result.ports["isMatch"]  = isMatch;
        result.ports["distance"] = distance;

        // 同步到 data
        result.data["score"]    = score;
        result.data["isMatch"]  = isMatch;
        result.data["distance"] = distance;
        result.data["threshold"] = m_threshold;
        result.data["colorSpace"] = m_colorSpace;
        result.data["histMethod"] = m_histMethod;
        result.data["bins"]      = m_bins;
        result.data["roiX"]      = roi.x;
        result.data["roiY"]      = roi.y;
        result.data["roiW"]      = roi.width;
        result.data["roiH"]      = roi.height;
        result.data["templatePath"] = m_templatePath;

        result.ok = true;
        result.score = score;

        // 准备 overlayImage（BGR）
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

        // 绘制 ROI 框（绿色，若不匹配则红色）
        cv::Scalar boxColor = isMatch ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
        cv::rectangle(overlay, roi.tl(), roi.br(), boxColor, 2);

        // 绘制分数与匹配状态文字
        QString label = QString("score=%1 %2")
            .arg(score, 0, 'f', 3)
            .arg(isMatch ? "[MATCH]" : "[NOMATCH]");
        cv::putText(overlay, label.toStdString(),
                    cv::Point(roi.x, std::max(roi.y - 5, 12)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6,
                    boxColor, 1, cv::LINE_AA);

        result.overlayImage = overlay;

        m_results["lastScore"]   = score;
        m_results["lastIsMatch"] = isMatch;

        Logger::info(QString("ColorMatchTool: score=%1 dist=%2 match=%3")
            .arg(score, 0, 'f', 3).arg(distance, 0, 'f', 3).arg(isMatch ? "yes" : "no"));

        return true;
    } catch (const cv::Exception& e) {
        Logger::error(QString("ColorMatchTool: OpenCV exception: %1").arg(e.what()));
        result.ok = false;
        result.data["error"] = QString("OpenCV exception: %1").arg(e.what());
        return false;
    } catch (const std::exception& e) {
        Logger::error(QString("ColorMatchTool: std exception: %1").arg(e.what()));
        result.ok = false;
        result.data["error"] = QString("std exception: %1").arg(e.what());
        return false;
    }
}

QJsonObject ColorMatchTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["templatePath"] = m_templatePath;
    obj["colorSpace"]   = m_colorSpace;
    obj["histMethod"]   = m_histMethod;
    obj["threshold"]    = m_threshold;
    obj["bins"]         = m_bins;
    obj["roiX"]         = m_roiX;
    obj["roiY"]         = m_roiY;
    obj["roiW"]         = m_roiW;
    obj["roiH"]         = m_roiH;
    return obj;
}

bool ColorMatchTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("templatePath")) {
        m_templatePath = data["templatePath"].toString();
        loadTemplate();
    }
    if (data.contains("colorSpace")) {
        QString cs = data["colorSpace"].toString();
        if (cs == "HSV" || cs == "RGB" || cs == "Lab") m_colorSpace = cs;
    }
    if (data.contains("histMethod")) {
        QString hm = data["histMethod"].toString();
        if (hm == "Correl" || hm == "ChiSqr" || hm == "Intersect" || hm == "Bhattacharyya") {
            m_histMethod = hm;
        }
    }
    if (data.contains("threshold")) {
        double t = data["threshold"].toDouble();
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        m_threshold = t;
    }
    if (data.contains("bins")) {
        int b = data["bins"].toInt();
        if (b < 1) b = 1;
        if (b > 256) b = 256;
        m_bins = b;
    }
    if (data.contains("roiX")) m_roiX = data["roiX"].toInt();
    if (data.contains("roiY")) m_roiY = data["roiY"].toInt();
    if (data.contains("roiW")) m_roiW = data["roiW"].toInt();
    if (data.contains("roiH")) m_roiH = data["roiH"].toInt();

    // spec Task 3：旧方案自动迁移
    // 旧方案 data 含 roiX/roiY/roiW/roiH 但不含 "roi" 字段
    // ColorMatchTool 旧约定：roiW=0/roiH=0 表示全图，仅当 roiW>0 && roiH>0 时迁移
    if (!hasRoi() && m_roiW > 0 && m_roiH > 0) {
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

// 声明输出端口：分数（数值）、是否匹配（布尔）、距离（数值）
QList<PortDescriptor> ColorMatchTool::outputPorts() const {
    return {
        {QStringLiteral("score"),   QStringLiteral("分数"),     PortType::Number, PortDirection::Out, QStringLiteral("颜色比对相似分数 [0,1]")},
        {QStringLiteral("isMatch"), QStringLiteral("是否匹配"), PortType::Bool,   PortDirection::Out, QStringLiteral("分数是否达到阈值")},
        {QStringLiteral("distance"),QStringLiteral("距离"),     PortType::Number, PortDirection::Out, QStringLiteral("原始直方图比对距离")},
    };
}

// 声明输入端口：图像
QList<PortDescriptor> ColorMatchTool::inputPorts() const {
    return {
        {QStringLiteral("image"), QStringLiteral("图像"), PortType::Image, PortDirection::In, QStringLiteral("待比对的输入图像")},
    };
}
