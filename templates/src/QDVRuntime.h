#ifndef QDV_RUNTIME_H
#define QDV_RUNTIME_H

/**
 * @file QDVRuntime.h
 * @brief 导出运行时核心：方案加载 + 算子重建 + 流程执行
 *
 * 被 QDVPipeline.dll（wrapper）与 QDVPipelineRun.exe（命令行）共享。
 * 不依赖 QML / UI 模块（精简运行时）。
 */

#include <QString>
#include <QList>
#include <QMap>
#include <QJsonObject>
#include <opencv2/core/mat.hpp>

namespace QDV { class VisionTool; }
class ToolChainExecutor;
struct ToolResult;

class QDVRuntime {
public:
    QDVRuntime();
    ~QDVRuntime();

    /// 加载 .json 方案文件（SchemeSerializer 保存的格式）
    bool loadScheme(const QString& schemePath);

    /// 设置输入图像（从文件路径加载）
    bool setInputImage(const QString& imagePath);
    /// 设置输入图像（直接传入 cv::Mat）
    bool setInputImage(const cv::Mat& image);

    /// 执行算子流程
    bool run();

    /// 获取所有算子结果的 JSON（含每个算子的 ok/elapsedMs/data）
    QString getResultJson() const;

    /// 获取指定算子的某个结果字段（JSON 字符串）
    QString getResult(const QString& toolId) const;

    /// 保存指定算子的输出图像到文件
    bool saveOutputImage(const QString& toolId, const QString& savePath) const;

    /// 最近错误信息
    QString lastError() const { return m_lastError; }

    /// 版本号
    static QString version();

    /// 方案包含的算子类型列表（供文档生成用）
    QStringList operatorTypes() const { return m_operatorTypes; }

    /// 方案是否含 AI 算子
    bool containsAi() const { return m_containsAi; }

private:
    QList<QDV::VisionTool*> m_tools;
    ToolChainExecutor* m_executor;
    cv::Mat m_inputImage;
    QString m_lastError;
    QStringList m_operatorTypes;
    bool m_containsAi = false;

    void clearTools();
    bool parseSchemeJson(const QJsonObject& root);
    QString toolChainOrder(const QJsonObject& root) const;
};

#endif // QDV_RUNTIME_H
