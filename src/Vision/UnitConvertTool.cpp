#include "UnitConvertTool.h"
#include "Core/Logger.h"
#include <opencv2/core.hpp>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

using namespace QDV;

// =====================================================
// 构造
// =====================================================
UnitConvertTool::UnitConvertTool() {
    m_name = "单位换算";
}

// =====================================================
// 参数配置
// =====================================================
bool UnitConvertTool::configure(const QJsonObject& params) {
    if (params.contains("calibFile"))  m_calibFile  = params["calibFile"].toString();
    if (params.contains("inputValue")) m_inputValue = params["inputValue"].toDouble();
    if (params.contains("inputType")) {
        const QString v = params["inputType"].toString();
        if (v == "distance" || v == "area") {
            m_inputType = v;
        } else {
            Logger::warn(QString("UnitConvertTool: inputType '%1' 非法，回退为 'distance'").arg(v));
            m_inputType = "distance";
        }
    }
    if (params.contains("outputUnit")) {
        const QString v = params["outputUnit"].toString();
        if (v == "mm" || v == "px") {
            m_outputUnit = v;
        } else {
            Logger::warn(QString("UnitConvertTool: outputUnit '%1' 非法，回退为 'mm'").arg(v));
            m_outputUnit = "mm";
        }
    }
    if (params.contains("pixelScale")) {
        const double s = params["pixelScale"].toDouble();
        // 钳制：必须 > 0，否则用默认值
        m_pixelScale = (s > 0.0) ? s : 1.0;
    }
    m_params = params;
    return true;
}

// =====================================================
// 加载标定文件
// 支持 .json（QFile + QJsonDocument）和 .yml/.yaml（cv::FileStorage）
// 成功时返回推算的 pixelScale（mm/px）= workDistance / mean(fx, fy)
// 失败时返回 false（调用方回退到手动 m_pixelScale）
// =====================================================
bool UnitConvertTool::loadCalibFile(double& fx, double& fy, double& cx, double& cy,
                                     double& workDistance, double& scale) const {
    fx = fy = cx = cy = workDistance = scale = 0.0;

    if (m_calibFile.isEmpty()) return false;

    const QFileInfo fi(m_calibFile);
    const QString suffix = fi.suffix().toLower();

    if (suffix == "json") {
        // JSON 格式：直接读取标量字段 fx/fy/cx/cy/workDistance
        QFile f(m_calibFile);
        if (!f.open(QIODevice::ReadOnly)) {
            Logger::warn(QString("UnitConvertTool: 无法打开标定文件 %1").arg(m_calibFile));
            return false;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) {
            Logger::warn("UnitConvertTool: 标定文件 JSON 格式无效");
            return false;
        }
        const QJsonObject obj = doc.object();
        fx = obj.value("fx").toDouble();
        fy = obj.value("fy").toDouble();
        cx = obj.value("cx").toDouble();
        cy = obj.value("cy").toDouble();
        workDistance = obj.value("workDistance").toDouble();
    } else {
        // yml / yaml：用 cv::FileStorage 读取相机矩阵
        try {
            cv::FileStorage fs(m_calibFile.toStdString(), cv::FileStorage::READ);
            if (!fs.isOpened()) {
                Logger::warn(QString("UnitConvertTool: FileStorage 无法打开 %1").arg(m_calibFile));
                return false;
            }
            cv::Mat K;
            fs["cameraMatrix"] >> K;

            // workDistance 为可选字段，不存在时保持 0
            cv::FileNode wdNode = fs["workDistance"];
            if (!wdNode.empty()) workDistance = (double)wdNode;

            if (!K.empty() && K.rows >= 3 && K.cols >= 3) {
                // cameraMatrix 为 3x3 double（标准张正友标定输出）
                fx = K.at<double>(0, 0);
                fy = K.at<double>(1, 1);
                cx = K.at<double>(0, 2);
                cy = K.at<double>(1, 2);
            } else {
                // 兼容直接写标量字段的情况
                cv::FileNode nFx = fs["fx"];
                cv::FileNode nFy = fs["fy"];
                cv::FileNode nCx = fs["cx"];
                cv::FileNode nCy = fs["cy"];
                if (!nFx.empty()) fx = (double)nFx;
                if (!nFy.empty()) fy = (double)nFy;
                if (!nCx.empty()) cx = (double)nCx;
                if (!nCy.empty()) cy = (double)nCy;
            }
        } catch (const cv::Exception& e) {
            Logger::error(QString("UnitConvertTool: 标定文件解析异常: %1")
                              .arg(QString::fromStdString(e.what())));
            return false;
        }
    }

    // 推算 pixelScale（mm/px）= workDistance / mean(fx, fy)
    // 前提：有内参 + 工作距离
    if (fx > 0.0 && fy > 0.0 && workDistance > 0.0) {
        scale = workDistance / ((fx + fy) * 0.5);
        return true;
    }
    // 有内参但无工作距离：无法推算物理尺度，回退手动
    if (fx > 0.0 || fy > 0.0) {
        Logger::info("UnitConvertTool: 标定文件有内参但无 workDistance，回退手动 pixelScale");
        return false;
    }
    Logger::warn("UnitConvertTool: 标定文件缺少 fx/fy，回退手动 pixelScale");
    return false;
}

// =====================================================
// execute：执行单位换算
// 换算规则：
//   outputUnit="mm"（输入 px → 输出 mm）：
//     distance: out = in * pixelScale
//     area    : out = in * pixelScale^2
//   outputUnit="px"（输入 mm → 输出 px）：
//     distance: out = in / pixelScale
//     area    : out = in / pixelScale^2
// =====================================================
bool UnitConvertTool::execute(const cv::Mat& input, ToolResult& result) {
    try {
        // 1. 尝试从标定文件推算 pixelScale；失败则用手动 m_pixelScale 降级
        double fx = 0.0, fy = 0.0, cx = 0.0, cy = 0.0, workDistance = 0.0, scale = 0.0;
        const bool fromCalib = loadCalibFile(fx, fy, cx, cy, workDistance, scale);

        double pixelScale = fromCalib ? scale : m_pixelScale;
        // 兜底保护：pixelScale 必须 > 0
        if (!(pixelScale > 0.0)) pixelScale = 1.0;

        // 2. 执行换算
        double outputValue = 0.0;
        if (m_outputUnit == "mm") {
            if (m_inputType == "area") {
                outputValue = m_inputValue * pixelScale * pixelScale;
            } else {
                outputValue = m_inputValue * pixelScale;
            }
        } else { // "px"
            if (m_inputType == "area") {
                outputValue = m_inputValue / (pixelScale * pixelScale);
            } else {
                outputValue = m_inputValue / pixelScale;
            }
        }

        // 3. 写出结果（ports + data 双通道）
        result.ok = true;
        result.ports["outputValue"] = QVariant(outputValue);
        result.ports["unit"]        = QVariant(m_outputUnit);
        result.ports["pixelScale"]  = QVariant(pixelScale);

        result.data["outputValue"] = outputValue;
        result.data["unit"]        = m_outputUnit;
        result.data["pixelScale"]  = pixelScale;
        result.data["inputValue"]  = m_inputValue;
        result.data["inputType"]   = m_inputType;

        // 标定来源信息（便于排查）
        result.data["calibFrom"] = fromCalib ? "calibFile" : "manual";
        if (fromCalib) {
            result.data["fx"] = fx;
            result.data["fy"] = fy;
            result.data["cx"] = cx;
            result.data["cy"] = cy;
            result.data["workDistance"] = workDistance;
        } else if (!m_calibFile.isEmpty()) {
            // 配置了标定文件但加载失败：降级提示（仍返回 true）
            result.data["warning"] = QString("标定文件加载失败，回退手动 pixelScale=%1").arg(m_pixelScale);
            Logger::warn(QString("UnitConvertTool: 标定文件 %1 加载失败，降级为手动 pixelScale=%2")
                             .arg(m_calibFile).arg(m_pixelScale));
        }

        // 4. overlay 透传输入（若有）
        if (!input.empty()) {
            result.overlayImage = input.clone();
        }

        Logger::info(QString("UnitConvertTool: in=%1 %2 → out=%3 %4 (scale=%5, src=%6)")
                         .arg(m_inputValue, 0, 'f', 4)
                         .arg(m_inputType)
                         .arg(outputValue, 0, 'f', 4)
                         .arg(m_outputUnit)
                         .arg(pixelScale, 0, 'f', 6)
                         .arg(fromCalib ? "calib" : "manual"));
        return true;

    } catch (const cv::Exception& e) {
        Logger::error(QString("UnitConvertTool: OpenCV 异常: %1").arg(QString::fromStdString(e.what())));
        result.ok = false;
        result.data["error"] = QString::fromStdString(e.what());
        return false;
    } catch (const std::exception& e) {
        Logger::error(QString("UnitConvertTool: 标准异常: %1").arg(QString::fromStdString(e.what())));
        result.ok = false;
        result.data["error"] = QString::fromStdString(e.what());
        return false;
    }
}

// =====================================================
// 端口声明（P1-3 typed ports）
// =====================================================
QList<PortDescriptor> UnitConvertTool::outputPorts() const {
    return {
        PortDescriptor{ "outputValue", "输出值", PortType::Number, PortDirection::Out, "换算后的数值" },
        PortDescriptor{ "unit",        "单位",   PortType::String, PortDirection::Out, "输出值的单位（mm/px）" },
    };
}

QList<PortDescriptor> UnitConvertTool::inputPorts() const {
    return {
        PortDescriptor{ "image",      "图像",   PortType::Image,  PortDirection::In, "输入图像（透传到 overlay）" },
        PortDescriptor{ "inputValue", "输入值", PortType::Number, PortDirection::In, "待换算的数值" },
    };
}

// =====================================================
// 序列化 / 反序列化
// =====================================================
QJsonObject UnitConvertTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["calibFile"]  = m_calibFile;
    obj["inputValue"] = m_inputValue;
    obj["inputType"]  = m_inputType;
    obj["outputUnit"] = m_outputUnit;
    obj["pixelScale"] = m_pixelScale;
    return obj;
}

bool UnitConvertTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("calibFile"))  m_calibFile  = data["calibFile"].toString();
    if (data.contains("inputValue")) m_inputValue = data["inputValue"].toDouble();
    if (data.contains("inputType")) {
        const QString v = data["inputType"].toString();
        if (v == "distance" || v == "area") m_inputType = v;
    }
    if (data.contains("outputUnit")) {
        const QString v = data["outputUnit"].toString();
        if (v == "mm" || v == "px") m_outputUnit = v;
    }
    if (data.contains("pixelScale")) {
        const double s = data["pixelScale"].toDouble();
        m_pixelScale = (s > 0.0) ? s : 1.0;
    }
    return true;
}
