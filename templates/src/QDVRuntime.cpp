// QDVRuntime.cpp - 导出运行时核心实现
// 加载方案 JSON + 重建算子链 + 执行流程

#include "QDVRuntime.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>
#include <QDir>
#include <QMutex>
#include <opencv2/imgcodecs.hpp>

#include "Core/VisionTool.h"
#include "Core/CameraConfig.h"
#include "Vision/ToolFactory.h"
#include "Vision/ToolChainExecutor.h"

// CameraConfig::s_mutex 原本定义在 UI/CameraView.cpp 中，
// 但导出运行时 DLL 不链接 UI 模块（精简运行时），需在此提供定义。
// QDVPipeline.dll 是独立共享库，符号不与主程序冲突。
QMutex CameraConfig::s_mutex;

QDVRuntime::QDVRuntime()
    : m_executor(new ToolChainExecutor())
{
}

QDVRuntime::~QDVRuntime()
{
    clearTools();
    delete m_executor;
}

void QDVRuntime::clearTools()
{
    for (auto* tool : m_tools) {
        delete tool;
    }
    m_tools.clear();
    m_operatorTypes.clear();
    m_containsAi = false;
}

bool QDVRuntime::loadScheme(const QString& schemePath)
{
    QFile file(schemePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QStringLiteral("无法打开方案文件: %1").arg(schemePath);
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        m_lastError = QStringLiteral("方案 JSON 解析失败: %1").arg(parseError.errorString());
        return false;
    }

    if (!doc.isObject()) {
        m_lastError = QStringLiteral("方案 JSON 根不是对象");
        return false;
    }

    clearTools();
    return parseSchemeJson(doc.object());
}

bool QDVRuntime::parseSchemeJson(const QJsonObject& root)
{
    QJsonArray nodes = root.value("nodes").toArray();
    if (nodes.isEmpty()) {
        m_lastError = QStringLiteral("方案无节点");
        return false;
    }

    for (const QJsonValue& nodeVal : nodes) {
        QJsonObject node = nodeVal.toObject();
        QString type = node.value("type").toString();
        QString id = node.value("id").toString();
        QString name = node.value("name").toString();
        QJsonObject params = node.value("params").toObject();

        QDV::VisionTool* tool = ToolFactory::instance()->createTool(type);
        if (!tool) {
            m_lastError = QStringLiteral("未知算子类型: %1").arg(type);
            clearTools();
            return false;
        }

        // 设置 ID（替代 deserialize({id,name,type}) 以避免覆盖 configure 的参数）
        tool->setId(id);
        tool->setName(name.isEmpty() ? type : name);

        // 应用参数
        if (!tool->configure(params)) {
            m_lastError = QStringLiteral("算子 %1 参数配置失败").arg(type);
            delete tool;
            clearTools();
            return false;
        }

        m_tools.append(tool);
        m_operatorTypes.append(type);
        if (type == "AiClassify" || type.contains("Dl") || type.contains("Yolo")) {
            m_containsAi = true;
        }
    }

    // 注入执行器（非 owning，执行器不释放 tool）
    m_executor->setTools(m_tools);
    return true;
}

bool QDVRuntime::setInputImage(const QString& imagePath)
{
    if (imagePath.isEmpty()) {
        m_lastError = QStringLiteral("输入图像路径为空");
        return false;
    }
    cv::Mat img = cv::imread(imagePath.toStdString());
    if (img.empty()) {
        m_lastError = QStringLiteral("无法读取图像: %1").arg(imagePath);
        return false;
    }
    m_inputImage = img;
    return true;
}

bool QDVRuntime::setInputImage(const cv::Mat& image)
{
    if (image.empty()) {
        m_lastError = QStringLiteral("输入图像为空");
        return false;
    }
    m_inputImage = image.clone();
    return true;
}

bool QDVRuntime::run()
{
    if (m_tools.isEmpty()) {
        m_lastError = QStringLiteral("未加载方案");
        return false;
    }
    if (m_inputImage.empty()) {
        m_lastError = QStringLiteral("未设置输入图像");
        return false;
    }

    bool ok = m_executor->execute(m_inputImage);
    if (!ok) {
        m_lastError = QStringLiteral("流程执行失败");
    }
    return ok;
}

QString QDVRuntime::getResultJson() const
{
    if (m_tools.isEmpty()) {
        return QStringLiteral("{}");
    }

    QJsonArray results;
    QMap<QString, ToolResult> allResults = m_executor->getResults();

    for (QDV::VisionTool* tool : m_tools) {
        QJsonObject obj;
        obj["toolId"] = tool->id();
        obj["toolName"] = tool->name().isEmpty() ? tool->type() : tool->name();
        obj["type"] = tool->type();

        ToolResult r = allResults.value(tool->id());
        obj["ok"] = r.ok;
        obj["elapsedMs"] = qint64(r.elapsedMs);
        obj["score"] = r.score;
        obj["data"] = r.data;

        results.append(obj);
    }

    QJsonObject root;
    root["success"] = true;
    root["results"] = results;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QString QDVRuntime::getResult(const QString& toolId) const
{
    ToolResult r = m_executor->getResult(toolId);
    QJsonObject obj;
    obj["toolId"] = toolId;
    obj["ok"] = r.ok;
    obj["elapsedMs"] = qint64(r.elapsedMs);
    obj["score"] = r.score;
    obj["data"] = r.data;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

bool QDVRuntime::saveOutputImage(const QString& toolId, const QString& savePath) const
{
    ToolResult r = m_executor->getResult(toolId);
    if (r.overlayImage.empty()) {
        return false;
    }
    return cv::imwrite(savePath.toStdString(), r.overlayImage);
}

QString QDVRuntime::version()
{
    return QStringLiteral("1.0.0");
}
