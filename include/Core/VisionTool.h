#pragma once

#include <QString>
#include <QVariant>
#include <QMap>
#include <QJsonObject>
#include <QUuid>
#include <opencv2/core/mat.hpp>

struct ToolResult {
    bool ok = false;
    QJsonObject data;
    double score = 0.0;
    qint64 elapsedMs = 0;
    cv::Mat overlayImage;
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

    QMap<QString, QVariant> results() const { return m_results; }

    virtual QJsonObject serialize() const {
        QJsonObject obj;
        obj["type"] = type();
        obj["name"] = m_name;
        obj["id"] = m_id;
        return obj;
    }

    virtual bool deserialize(const QJsonObject& data) {
        if (data.contains("id")) m_id = data["id"].toString();
        if (data.contains("name")) m_name = data["name"].toString();
        return true;
    }

protected:
    QString m_id;
    QString m_name;
    QJsonObject m_params;
    QMap<QString, QVariant> m_results;
};

} // namespace QDV