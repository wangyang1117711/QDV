#pragma once

#include <QString>
#include <QVariant>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QVariantMap>
#include <QList>
#include <opencv2/core/mat.hpp>

// P1-3 typed ports：显式类型化数据端口
// 设计要点：
// - 图像数据仍走 ToolResult.overlayImage（cv::Mat 不便包装进 QVariant）
// - 非图像 typed 数据（Pose/Number/String/Region/Contour）走 ToolResult.ports
// - 算子通过 outputPorts()/inputPorts() 声明端口元数据，供连线期类型校验
// - 现有算子无需修改（默认返回空端口列表，走 overlayImage 兼容路径）
namespace QDV {

/// 端口数据类型（连线类型校验用）
enum class PortType {
    Any,      ///< 通配类型，兼容一切（旧算子未声明端口时默认）
    Image,    ///< 图像（对应 overlayImage 通道）
    Region,   ///< 区域（点集/多边形）
    Pose,     ///< 位姿 {x,y,angle,scale}
    Contour,  ///< 轮廓（点集序列）
    String,   ///< 字符串
    Number,   ///< 数值（int/float 统一）
    Bool,     ///< 布尔
    Points,   ///< 点集（如检测结果列表）
};

/// 端口方向
enum class PortDirection {
    In,   ///< 输入端口
    Out,  ///< 输出端口
};

/// 端口描述符（算子声明自身端口元数据）
struct PortDescriptor {
    QString      name;        ///< 内部名（"pose", "distance"）
    QString      cnName;      ///< 中文显示名（"位姿", "距离"）
    PortType     type = PortType::Any;
    PortDirection dir = PortDirection::Out;
    QString      desc;        ///< 端口说明（可选）

    /// 端口类型名称（用于 QML 显示与日志）
    static QString typeName(PortType t);
    /// 两个类型是否兼容（连线校验：from 输出 → to 输入）
    /// Any 兼容一切；其余需精确匹配（Number/Int/Float 互通；Image/Mat 互通）
    static bool compatible(PortType outType, PortType inType);
    /// 序列化为 QVariantMap（QML 端 JS 友好）
    QVariantMap toMap() const;
};

} // namespace QDV

struct ToolResult {
    bool ok = false;
    QJsonObject data;
    double score = 0.0;
    qint64 elapsedMs = 0;
    cv::Mat overlayImage;
    // P1-3 typed ports：显式类型化数据端口
    // key=端口名，value=QVariant（Pose→QVariantMap, Number→double/int, String→QString,
    // Region/Contour/Points→QVariantList of points, Bool→bool）
    // 图像数据仍走 overlayImage，此处仅存非图像 typed 输出
    QVariantMap ports;
};

namespace QDV {

class VisionTool
{
public:
    enum ToolType {
        TemplateMatch,
        EdgeDetect,
        BlobDetect,
        ColorDetect,
        Threshold,
        ImagePreprocess,
        ContourAnalyze,
        GeometryMeasure,
        LineCircleDetect,
        ImageArithmetic,
        ImageTransform,
        ImageMerge,
        BranchControl,
        AiClassify,
        ReadImage
    };

    VisionTool() : m_id(QUuid::createUuid().toString()) {}
    virtual ~VisionTool() = default;

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    // P0 修复：公开 setId，供 buildToolChainFromNodes 设置节点 ID，
    // 替代之前调用 deserialize({id,name,type}) 的做法——后者会覆盖 configure 已设置的参数。
    void setId(const QString& id) { m_id = id; }
    void setName(const QString& name) { m_name = name; }
    virtual QString type() const = 0;

    virtual bool configure(const QJsonObject& params) {
        Q_UNUSED(params)
        return true;
    }

    virtual bool execute(const cv::Mat& input, ToolResult& result) {
        Q_UNUSED(input)
        Q_UNUSED(result)
        return true;
    }

    // P1-3 typed ports：算子声明自身的输入/输出端口元数据
    // 默认返回空列表（向后兼容：旧算子未声明端口，走 overlayImage 通道，连线时不校验类型）
    // 新算子应重写这两个方法，声明端口以便连线期类型校验与 QML 端口可视化
    virtual QList<PortDescriptor> outputPorts() const { return {}; }
    virtual QList<PortDescriptor> inputPorts() const { return {}; }

    QMap<QString, QVariant> results() const { return m_results; }

    // ===== 算子级通用 ROI（spec 阶段一 Task 1）=====
    // ROI 数据结构（QVariantMap）支持两种形态：
    //   矩形：{"type":"rect","x":int,"y":int,"w":int,"h":int}
    //   多边形：{"type":"polygon","points":[{x,y},{x,y},...]}
    // 默认空 ROI = 全图（向后兼容：旧方案无该字段时视为空）
    // 设计要点：
    //   - ToolChainExecutor 在 execute 前按 ROI 自动裁剪/掩蔽输入图像
    //   - 执行后自动将结果坐标反变换回原图坐标系
    //   - 所有算子零改动获得 ROI 能力（除 Caliper/ColorMatch 需迁移私有 ROI）
    void setRoi(const QVariantMap& roi) { m_roi = roi; }
    QVariantMap roi() const { return m_roi; }
    bool hasRoi() const {
        // 空 ROI 或 type 字段缺失视为无 ROI（全图，向后兼容）
        return !m_roi.isEmpty() && m_roi.contains("type");
    }

    virtual QJsonObject serialize() const {
        QJsonObject obj;
        obj["type"] = type();
        obj["name"] = m_name;
        obj["id"] = m_id;
        // 序列化 ROI 字段（空 ROI 也写入，便于 UI 端识别）
        obj["roi"] = roiToJson(m_roi);
        return obj;
    }

    virtual bool deserialize(const QJsonObject& data) {
        if (data.contains("id")) m_id = data["id"].toString();
        if (data.contains("name")) m_name = data["name"].toString();
        // 反序列化 ROI（旧方案无该字段时视为空 ROI，向后兼容）
        if (data.contains("roi")) {
            m_roi = roiFromJson(data["roi"].toObject());
        }
        return true;
    }

protected:
    QString m_id;
    QString m_name;
    QJsonObject m_params;
    QMap<QString, QVariant> m_results;
    // 通用 ROI（默认空 = 全图）
    QVariantMap m_roi;

    // ROI 序列化辅助：QVariantMap -> QJsonObject
    static QJsonObject roiToJson(const QVariantMap& roi) {
        QJsonObject obj;
        const QString type = roi.value("type").toString();
        obj["type"] = type;
        if (type == "rect") {
            obj["x"] = roi.value("x").toInt();
            obj["y"] = roi.value("y").toInt();
            obj["w"] = roi.value("w").toInt();
            obj["h"] = roi.value("h").toInt();
        } else if (type == "polygon") {
            QJsonArray ptsArr;
            const QVariantList pts = roi.value("points").toList();
            for (const QVariant& v : pts) {
                const QVariantMap pm = v.toMap();
                QJsonObject pt;
                pt["x"] = pm.value("x").toInt();
                pt["y"] = pm.value("y").toInt();
                ptsArr.append(pt);
            }
            obj["points"] = ptsArr;
        }
        return obj;
    }

    // ROI 反序列化辅助：QJsonObject -> QVariantMap
    static QVariantMap roiFromJson(const QJsonObject& obj) {
        QVariantMap roi;
        const QString type = obj.value("type").toString();
        roi["type"] = type;
        if (type == "rect") {
            roi["x"] = obj.value("x").toInt();
            roi["y"] = obj.value("y").toInt();
            roi["w"] = obj.value("w").toInt();
            roi["h"] = obj.value("h").toInt();
        } else if (type == "polygon") {
            QVariantList pts;
            const QJsonArray ptsArr = obj.value("points").toArray();
            for (const QJsonValue& v : ptsArr) {
                const QJsonObject pt = v.toObject();
                QVariantMap pm;
                pm["x"] = pt.value("x").toInt();
                pm["y"] = pt.value("y").toInt();
                pts.append(pm);
            }
            roi["points"] = pts;
        }
        return roi;
    }
};

} // namespace QDV

Q_DECLARE_METATYPE(QDV::PortDescriptor)
