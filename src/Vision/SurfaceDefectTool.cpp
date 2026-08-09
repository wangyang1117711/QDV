#include "Vision/SurfaceDefectTool.h"
#include "Core/Logger.h"
#include <QJsonArray>
#include <QVariantList>
#include <QVariantMap>
#include <opencv2/imgproc.hpp>
#include <algorithm>

using namespace QDV;

SurfaceDefectTool::SurfaceDefectTool() {
    m_name = "表面缺陷检测";
}

// 加载标准模板图：QFile 读取字节流 + cv::imdecode 解码，绕过 cv::imread 中文路径问题
// 解码为彩色图（BGR），与 input 差分时需要通道一致
bool SurfaceDefectTool::loadTemplate() {
    m_template.release();

    if (m_templatePath.isEmpty()) {
        return false;
    }

    QFileInfo info(m_templatePath);
    if (!info.exists() || !info.isFile()) {
        Logger::error("SurfaceDefectTool: template file not found: " + m_templatePath);
        return false;
    }

    QFile f(m_templatePath);
    if (!f.open(QIODevice::ReadOnly)) {
        Logger::error("SurfaceDefectTool: cannot open template: " + m_templatePath);
        return false;
    }
    QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty()) {
        Logger::error("SurfaceDefectTool: template file is empty: " + m_templatePath);
        return false;
    }

    // 解码为彩色图
    cv::Mat raw = cv::imdecode(
        cv::Mat(1, bytes.size(), CV_8UC1, const_cast<char*>(bytes.constData())),
        cv::IMREAD_COLOR);
    if (raw.empty()) {
        Logger::error("SurfaceDefectTool: failed to decode template: " + m_templatePath);
        return false;
    }

    m_template = raw;
    Logger::info(QString("SurfaceDefectTool: template loaded %1 (%2x%3)")
        .arg(info.fileName()).arg(m_template.cols).arg(m_template.rows));
    return true;
}

bool SurfaceDefectTool::configure(const QJsonObject& params) {
    if (params.contains("templatePath")) {
        QString newPath = params["templatePath"].toString();
        if (newPath != m_templatePath) {
            m_templatePath = newPath;
            loadTemplate();
        }
    }

    if (params.contains("sensitivity")) {
        double s = params["sensitivity"].toDouble();
        if (s < 0.1) s = 0.1;       // 防止阈值过低导致全图判定为缺陷
        if (s > 20.0) s = 20.0;
        m_sensitivity = s;
    }

    if (params.contains("minDefectArea")) {
        double a = params["minDefectArea"].toDouble();
        if (a < 0.0) a = 0.0;
        m_minDefectArea = a;
    }

    if (params.contains("maxDefectArea")) {
        double a = params["maxDefectArea"].toDouble();
        if (a < 0.0) a = 0.0;
        m_maxDefectArea = a;
    }

    if (params.contains("morphKernelSize")) {
        int k = params["morphKernelSize"].toInt();
        if (k < 1) k = 1;
        if (k > 31) k = 31;
        m_morphKernelSize = k;
    }

    if (params.contains("blurSize")) {
        int b = params["blurSize"].toInt();
        // 高斯模糊核必须是正奇数
        if (b < 1) b = 1;
        if (b % 2 == 0) b += 1;
        if (b > 31) b = 31;
        m_blurSize = b;
    }

    m_params = params;
    return true;
}

bool SurfaceDefectTool::execute(const cv::Mat& input, ToolResult& result) {
    try {
        // 输入校验
        if (input.empty()) {
            Logger::error("SurfaceDefectTool: input image is empty");
            result.ok = false;
            result.data["error"] = "Input image is empty";
            return false;
        }

        // 标准模板必须已加载
        if (m_template.empty()) {
            Logger::error("SurfaceDefectTool: template not loaded");
            result.ok = false;
            result.data["error"] = "Template not loaded";
            return false;
        }

        // 准备与 input 同尺寸、同通道数的模板副本（若尺寸不一致则 resize）
        cv::Mat tpl = m_template;
        if (tpl.size() != input.size()) {
            cv::resize(tpl, tpl, input.size(), 0, 0, cv::INTER_LINEAR);
        }
        // 通道对齐：若 input 是单通道，模板也转灰度；若 input 是 3 通道而模板是单通道，转 BGR
        cv::Mat inputAligned, tplAligned;
        if (input.channels() == 1) {
            if (tpl.channels() == 3) {
                cv::cvtColor(tpl, tplAligned, cv::COLOR_BGR2GRAY);
            } else {
                tplAligned = tpl.clone();
            }
            inputAligned = input.clone();
        } else if (input.channels() == 3) {
            if (tpl.channels() == 1) {
                cv::cvtColor(tpl, tplAligned, cv::COLOR_GRAY2BGR);
            } else {
                tplAligned = tpl.clone();
            }
            inputAligned = input.clone();
        } else if (input.channels() == 4) {
            cv::cvtColor(input, inputAligned, cv::COLOR_BGRA2BGR);
            if (tpl.channels() == 1) {
                cv::cvtColor(tpl, tplAligned, cv::COLOR_GRAY2BGR);
            } else if (tpl.channels() == 4) {
                cv::cvtColor(tpl, tplAligned, cv::COLOR_BGRA2BGR);
            } else {
                tplAligned = tpl.clone();
            }
        } else {
            inputAligned = input.clone();
            tplAligned = tpl.clone();
        }

        // 1. 两图差分
        cv::Mat diff;
        cv::absdiff(inputAligned, tplAligned, diff);

        // 2. 灰度化 + 高斯模糊降噪
        cv::Mat gray;
        if (diff.channels() == 1) {
            gray = diff.clone();
        } else {
            cv::cvtColor(diff, gray, cv::COLOR_BGR2GRAY);
        }
        int blurSize = m_blurSize;
        if (blurSize % 2 == 0) blurSize += 1;  // 高斯核必须正奇数
        if (blurSize > 0) {
            cv::GaussianBlur(gray, gray, cv::Size(blurSize, blurSize), 0);
        }

        // 3. 自适应阈值：threshold = mean + sensitivity * stddev
        cv::Scalar mean, stddev;
        cv::meanStdDev(gray, mean, stddev);
        double thr = mean[0] + m_sensitivity * stddev[0];
        // 阈值下限保护，避免空白差分图产生随机噪声
        if (thr < 5.0) thr = 5.0;

        cv::Mat mask;
        cv::threshold(gray, mask, thr, 255, cv::THRESH_BINARY);

        // 4. 形态学开运算去噪（先腐蚀后膨胀，去除小颗粒噪声）
        if (m_morphKernelSize > 0) {
            cv::Mat kernel = cv::getStructuringElement(
                cv::MORPH_RECT, cv::Size(m_morphKernelSize, m_morphKernelSize));
            cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
        }

        // 5. findContours 找缺陷区域，按面积过滤
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        struct Defect {
            cv::Rect rect;
            double   area;
        };
        std::vector<Defect> defects;
        double totalDefectArea = 0.0;
        double maxDefectArea = 0.0;

        for (const auto& c : contours) {
            double area = cv::contourArea(c);
            // 面积过滤：必须在 [minDefectArea, maxDefectArea] 范围内
            if (area < m_minDefectArea) continue;
            if (area > m_maxDefectArea) continue;

            defects.push_back({cv::boundingRect(c), area});
            totalDefectArea += area;
            if (area > maxDefectArea) maxDefectArea = area;
        }

        int defectCount = static_cast<int>(defects.size());
        bool hasDefect = (defectCount > 0);

        // 构建 typed ports 输出
        QVariantList defectsList;
        QJsonArray defectsArr;
        for (const auto& d : defects) {
            QVariantMap vm;
            vm["x"] = d.rect.x;
            vm["y"] = d.rect.y;
            vm["w"] = d.rect.width;
            vm["h"] = d.rect.height;
            vm["area"] = d.area;
            defectsList.append(vm);

            QJsonObject m;
            m["x"] = d.rect.x;
            m["y"] = d.rect.y;
            m["w"] = d.rect.width;
            m["h"] = d.rect.height;
            m["area"] = d.area;
            defectsArr.append(m);
        }

        // 写入 typed ports
        result.ports["defectCount"]      = defectCount;
        result.ports["totalDefectArea"]  = totalDefectArea;
        result.ports["maxDefectArea"]    = maxDefectArea;
        result.ports["hasDefect"]        = hasDefect;
        result.ports["defects"]          = defectsList;

        // 同步到 data
        result.data["defectCount"]      = defectCount;
        result.data["totalDefectArea"]  = totalDefectArea;
        result.data["maxDefectArea"]    = maxDefectArea;
        result.data["hasDefect"]        = hasDefect;
        result.data["defects"]          = defectsArr;
        result.data["threshold"]        = thr;
        result.data["mean"]             = mean[0];
        result.data["stddev"]           = stddev[0];
        result.data["sensitivity"]      = m_sensitivity;
        result.data["minDefectArea"]    = m_minDefectArea;
        result.data["maxDefectArea"]    = m_maxDefectArea;
        result.data["templatePath"]     = m_templatePath;

        result.ok = true;
        // 缺陷越多分数越低（用于排序/筛选场景）
        result.score = hasDefect ? (1.0 / (1.0 + defectCount)) : 1.0;

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

        // 绘制缺陷区域：半透明红色轮廓填充 + 红色矩形框
        if (hasDefect) {
            // 重新收集过滤后的轮廓点集用于填充（保证与 defects 一一对应）
            std::vector<std::vector<cv::Point>> filteredContours;
            filteredContours.reserve(defects.size());
            for (const auto& c : contours) {
                double area = cv::contourArea(c);
                if (area < m_minDefectArea) continue;
                if (area > m_maxDefectArea) continue;
                filteredContours.push_back(c);
            }
            // 半透明红色填充：先在独立图层上画红色，再按 0.3 权重叠加
            cv::Mat defectLayer = cv::Mat::zeros(overlay.size(), overlay.type());
            cv::drawContours(defectLayer, filteredContours, -1,
                             cv::Scalar(0, 0, 255), cv::FILLED);
            cv::addWeighted(overlay, 1.0, defectLayer, 0.3, 0, overlay);
            // 红色矩形框
            for (const auto& d : defects) {
                cv::Rect box = d.rect & cv::Rect(0, 0, overlay.cols, overlay.rows);
                cv::rectangle(overlay, box.tl(), box.br(), cv::Scalar(0, 0, 255), 2);
            }
        }

        // 顶部绘制统计信息
        QString label = QString("defects=%1 area=%2 %3")
            .arg(defectCount)
            .arg(totalDefectArea, 0, 'f', 1)
            .arg(hasDefect ? "[DEFECT]" : "[OK]");
        cv::Scalar labelColor = hasDefect ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0);
        cv::putText(overlay, label.toStdString(),
                    cv::Point(10, 25),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7,
                    labelColor, 2, cv::LINE_AA);

        result.overlayImage = overlay;

        m_results["lastDefectCount"]     = defectCount;
        m_results["lastTotalDefectArea"] = totalDefectArea;
        m_results["lastHasDefect"]       = hasDefect;

        Logger::info(QString("SurfaceDefectTool: defects=%1 totalArea=%2 maxArea=%3 thr=%4")
            .arg(defectCount)
            .arg(totalDefectArea, 0, 'f', 1)
            .arg(maxDefectArea, 0, 'f', 1)
            .arg(thr, 0, 'f', 2));

        return true;
    } catch (const cv::Exception& e) {
        Logger::error(QString("SurfaceDefectTool: OpenCV exception: %1").arg(e.what()));
        result.ok = false;
        result.data["error"] = QString("OpenCV exception: %1").arg(e.what());
        return false;
    } catch (const std::exception& e) {
        Logger::error(QString("SurfaceDefectTool: std exception: %1").arg(e.what()));
        result.ok = false;
        result.data["error"] = QString("std exception: %1").arg(e.what());
        return false;
    }
}

QJsonObject SurfaceDefectTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["templatePath"]   = m_templatePath;
    obj["sensitivity"]    = m_sensitivity;
    obj["minDefectArea"]  = m_minDefectArea;
    obj["maxDefectArea"]  = m_maxDefectArea;
    obj["morphKernelSize"]= m_morphKernelSize;
    obj["blurSize"]       = m_blurSize;
    return obj;
}

bool SurfaceDefectTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("templatePath")) {
        m_templatePath = data["templatePath"].toString();
        loadTemplate();
    }
    if (data.contains("sensitivity")) {
        double s = data["sensitivity"].toDouble();
        if (s < 0.1) s = 0.1;
        if (s > 20.0) s = 20.0;
        m_sensitivity = s;
    }
    if (data.contains("minDefectArea")) {
        double a = data["minDefectArea"].toDouble();
        if (a < 0.0) a = 0.0;
        m_minDefectArea = a;
    }
    if (data.contains("maxDefectArea")) {
        double a = data["maxDefectArea"].toDouble();
        if (a < 0.0) a = 0.0;
        m_maxDefectArea = a;
    }
    if (data.contains("morphKernelSize")) {
        int k = data["morphKernelSize"].toInt();
        if (k < 1) k = 1;
        if (k > 31) k = 31;
        m_morphKernelSize = k;
    }
    if (data.contains("blurSize")) {
        int b = data["blurSize"].toInt();
        if (b < 1) b = 1;
        if (b % 2 == 0) b += 1;
        if (b > 31) b = 31;
        m_blurSize = b;
    }
    return true;
}

// 声明输出端口：缺陷数（数值）、是否有缺陷（布尔）、缺陷列表（点集）
QList<PortDescriptor> SurfaceDefectTool::outputPorts() const {
    return {
        {QStringLiteral("defectCount"), QStringLiteral("缺陷数"),     PortType::Number, PortDirection::Out, QStringLiteral("检测到的缺陷数量")},
        {QStringLiteral("hasDefect"),   QStringLiteral("是否有缺陷"), PortType::Bool,   PortDirection::Out, QStringLiteral("是否存在缺陷")},
        {QStringLiteral("defects"),     QStringLiteral("缺陷列表"),   PortType::Points, PortDirection::Out, QStringLiteral("缺陷区域列表 {x,y,w,h,area}")},
    };
}

// 声明输入端口：图像
QList<PortDescriptor> SurfaceDefectTool::inputPorts() const {
    return {
        {QStringLiteral("image"), QStringLiteral("图像"), PortType::Image, PortDirection::In, QStringLiteral("待检测的输入图像")},
    };
}
