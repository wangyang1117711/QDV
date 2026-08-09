#include "PositionCorrectTool.h"
#include "Core/Logger.h"
#include <QJsonArray>
#include <QVariantList>
#include <QVariantMap>
#include <opencv2/imgproc.hpp>
#include <cmath>

using namespace QDV;

PositionCorrectTool::PositionCorrectTool() {
    m_name = "位置修正";
}

bool PositionCorrectTool::configure(const QJsonObject& params) {
    if (params.contains("poseX"))       m_poseX       = params["poseX"].toDouble();
    if (params.contains("poseY"))       m_poseY       = params["poseY"].toDouble();
    if (params.contains("poseAngle"))   m_poseAngle   = params["poseAngle"].toDouble();
    if (params.contains("poseScaleX"))  m_poseScaleX  = params["poseScaleX"].toDouble();
    if (params.contains("poseScaleY"))  m_poseScaleY  = params["poseScaleY"].toDouble();
    if (params.contains("refX"))        m_refX        = params["refX"].toDouble();
    if (params.contains("refY"))        m_refY        = params["refY"].toDouble();

    // 钳制缩放比例到有效范围，避免退化变换（0 缩放会丢失维度信息）
    if (m_poseScaleX <= 0.0) {
        Logger::warn(QString("PositionCorrectTool: poseScaleX %1 非法，已回退到 1.0").arg(m_poseScaleX));
        m_poseScaleX = 1.0;
    }
    if (m_poseScaleY <= 0.0) {
        Logger::warn(QString("PositionCorrectTool: poseScaleY %1 非法，已回退到 1.0").arg(m_poseScaleY));
        m_poseScaleY = 1.0;
    }

    m_params = params;
    return true;
}

bool PositionCorrectTool::execute(const cv::Mat& input, ToolResult& result) {
    try {
        // 旋转角度转弧度
        const double theta = m_poseAngle * CV_PI / 180.0;
        const double cosT = std::cos(theta);
        const double sinT = std::sin(theta);

        // 构建 2x2 旋转+缩放部分（X/Y 独立缩放）
        // [a b]   [sx*cos  -sy*sin]
        // [c d] = [sx*sin   sy*cos]
        const double a = m_poseScaleX * cosT;
        const double b = -m_poseScaleY * sinT;
        const double c = m_poseScaleX * sinT;
        const double d = m_poseScaleY * cosT;

        // 平移部分：使标准参考点 (refX,refY) 经矩阵映射到实际位姿点 (poseX,poseY)
        // 即 a*refX + b*refY + tx = poseX
        //     c*refX + d*refY + ty = poseY
        const double tx = m_poseX - (a * m_refX + b * m_refY);
        const double ty = m_poseY - (c * m_refX + d * m_refY);

        // 组装 2x3 仿射矩阵，按行存储为 6 个 double
        // 行优先顺序：[a, b, tx, c, d, ty]
        QVariantList matrixList;
        matrixList << a << b << tx << c << d << ty;

        // 位姿输出（QVariantMap）
        QVariantMap poseMap;
        poseMap.insert("x", m_poseX);
        poseMap.insert("y", m_poseY);
        poseMap.insert("angle", m_poseAngle);
        poseMap.insert("scaleX", m_poseScaleX);
        poseMap.insert("scaleY", m_poseScaleY);

        result.ports["affineMatrix"] = matrixList;
        result.ports["pose"] = poseMap;

        // JSON 输出同步
        QJsonArray matJson;
        matJson.append(a); matJson.append(b); matJson.append(tx);
        matJson.append(c); matJson.append(d); matJson.append(ty);
        result.data["matrix"] = matJson;

        QJsonObject poseJson;
        poseJson["x"] = m_poseX;
        poseJson["y"] = m_poseY;
        poseJson["angle"] = m_poseAngle;
        poseJson["scaleX"] = m_poseScaleX;
        poseJson["scaleY"] = m_poseScaleY;
        result.data["pose"] = poseJson;
        result.data["refX"] = m_refX;
        result.data["refY"] = m_refY;

        result.score = 1.0;
        result.ok = true;

        // overlayImage 透传输入（保证 BGR 格式，便于下游统一绘制）
        if (input.channels() == 1) {
            cv::cvtColor(input, result.overlayImage, cv::COLOR_GRAY2BGR);
        } else if (input.channels() == 4) {
            cv::cvtColor(input, result.overlayImage, cv::COLOR_BGRA2BGR);
        } else {
            result.overlayImage = input.clone();
        }

        // 在 overlay 上标注：标准参考点（红）与实际位姿点（绿），并用黄色连线表示偏移
        const cv::Point refPt(static_cast<int>(std::round(m_refX)),
                              static_cast<int>(std::round(m_refY)));
        const cv::Point posePt(static_cast<int>(std::round(m_poseX)),
                               static_cast<int>(std::round(m_poseY)));
        if (refPt.x >= 0 && refPt.x < result.overlayImage.cols &&
            refPt.y >= 0 && refPt.y < result.overlayImage.rows) {
            cv::drawMarker(result.overlayImage, refPt, cv::Scalar(0, 0, 255),
                           cv::MARKER_CROSS, 16, 2, cv::LINE_AA);
        }
        if (posePt.x >= 0 && posePt.x < result.overlayImage.cols &&
            posePt.y >= 0 && posePt.y < result.overlayImage.rows) {
            cv::drawMarker(result.overlayImage, posePt, cv::Scalar(0, 255, 0),
                           cv::MARKER_CROSS, 16, 2, cv::LINE_AA);
            cv::line(result.overlayImage, refPt, posePt, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
        }

        return true;
    } catch (const cv::Exception& e) {
        Logger::error(QString("PositionCorrectTool: OpenCV 异常: %1").arg(e.what()));
        result.ok = false;
        result.data["error"] = QString("OpenCV exception: %1").arg(e.what());
        return false;
    } catch (const std::exception& e) {
        Logger::error(QString("PositionCorrectTool: 异常: %1").arg(e.what()));
        result.ok = false;
        result.data["error"] = QString("Exception: %1").arg(e.what());
        return false;
    }
}

QList<PortDescriptor> PositionCorrectTool::outputPorts() const {
    return {
        {"affineMatrix", QStringLiteral("仿射矩阵"), PortType::Points, PortDirection::Out,
         QStringLiteral("2x3 仿射矩阵（6 个 double，行优先）")},
        {"pose", QStringLiteral("位姿"), PortType::Pose, PortDirection::Out,
         QStringLiteral("位姿 {x,y,angle,scaleX,scaleY}")},
    };
}

QList<PortDescriptor> PositionCorrectTool::inputPorts() const {
    return {
        {"image", QStringLiteral("图像"), PortType::Image, PortDirection::In,
         QStringLiteral("输入图像（用于 overlay 标注）")},
    };
}

QJsonObject PositionCorrectTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["poseX"] = m_poseX;
    obj["poseY"] = m_poseY;
    obj["poseAngle"] = m_poseAngle;
    obj["poseScaleX"] = m_poseScaleX;
    obj["poseScaleY"] = m_poseScaleY;
    obj["refX"] = m_refX;
    obj["refY"] = m_refY;
    return obj;
}

bool PositionCorrectTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    // 仅更新 data 中实际存在的字段，避免缺失字段被覆盖为默认值
    if (data.contains("poseX"))      m_poseX      = data["poseX"].toDouble();
    if (data.contains("poseY"))      m_poseY      = data["poseY"].toDouble();
    if (data.contains("poseAngle"))  m_poseAngle  = data["poseAngle"].toDouble();
    if (data.contains("poseScaleX")) m_poseScaleX = data["poseScaleX"].toDouble();
    if (data.contains("poseScaleY")) m_poseScaleY = data["poseScaleY"].toDouble();
    if (data.contains("refX"))       m_refX       = data["refX"].toDouble();
    if (data.contains("refY"))       m_refY       = data["refY"].toDouble();

    // 反序列化后同样钳制缩放，保证状态一致
    if (m_poseScaleX <= 0.0) m_poseScaleX = 1.0;
    if (m_poseScaleY <= 0.0) m_poseScaleY = 1.0;
    return true;
}
