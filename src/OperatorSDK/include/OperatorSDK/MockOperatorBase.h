#ifndef QDV_MOCK_OPERATOR_BASE_H
#define QDV_MOCK_OPERATOR_BASE_H

#include "OperatorSDK/IOperator.h"
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QJsonObject>
#include <QVariantMap>
#include <QString>

namespace QDV {

/// mock 算子共享基类
/// 设计目标：封装通用 mock 行为（参数校验、占位图生成、ToolResult 构造），
///           32 个 mock 补全算子与 5 个 Agent 角色 mock 算子继承它，
///           子类只需实现 mockTag() 和极简的 clone()。
///
/// 行为规范：
///   - 接收输入图像 → 返回合法 ToolResult（含输出图像路径 + data 字段）
///   - 不做真实计算，输出图像 = 输入图像副本或固定生成的占位图
///   - ToolResult.data["reason"] 标注 "[MOCK]" 前缀
class MockOperatorBase : public IOperator {
public:
    /// 通用 execute：参数校验 → 生成占位输出 → 构造 ToolResult
    bool execute(const cv::Mat& input, ToolResult& result) override {
        // 1. 参数校验（空实现，子类可覆盖 validateParams）
        validateParams(m_params);

        // 2. 生成占位输出图（输入副本或固定占位图）
        cv::Mat placeholder = generatePlaceholder(input);

        // 3. 保存占位图到临时路径
        QString outPath = savePlaceholder(placeholder);

        // 4. 构造 mock ToolResult
        result.ok = true;
        result.data = buildMockResult(outPath, input);
        result.score = 1.0;  // mock 默认满分
        result.elapsedMs = 0;
        result.overlayImage = placeholder;  // 叠加图 = 占位图

        return true;
    }

    QString version() const override { return "1.0.0"; }

    /// 配置参数（保存到 m_params 供 execute 使用）
    bool configure(const QJsonObject& params) override {
        m_params = params;
        return validateParams(params);
    }

    /// 序列化
    QJsonObject serialize() const override {
        QJsonObject obj = IOperator::serialize();
        obj["isMock"] = true;
        obj["mockTag"] = mockTag();
        return obj;
    }

    /// 反序列化
    bool deserialize(const QJsonObject& data) override {
        if (!IOperator::deserialize(data)) return false;
        m_params = data.contains("params") ? data["params"].toObject() : QJsonObject();
        return true;
    }

protected:
    /// 子类提供 mock 标签（如 "GaussFilter" / "MockClassify"）
    virtual QString mockTag() const = 0;

    /// 生成占位图：默认返回输入图副本，输入为空时生成 64x64 灰度图
    virtual cv::Mat generatePlaceholder(const cv::Mat& input) const {
        if (!input.empty()) {
            return input.clone();
        }
        return cv::Mat(64, 64, CV_8UC1, cv::Scalar(128));
    }

    /// 参数校验：默认实现仅检查必填字段存在性，子类可扩展
    virtual bool validateParams(const QJsonObject& params) const {
        Q_UNUSED(params)
        return true;
    }

    /// 保存占位图到临时文件，返回绝对路径
    virtual QString savePlaceholder(const cv::Mat& image) const {
        QString tempDir = QDir::tempPath() + "/qdv_mock";
        QDir().mkpath(tempDir);
        QString filename = QString("%1_%2_%3.png")
            .arg(mockTag())
            .arg(QDateTime::currentMSecsSinceEpoch())
            .arg(qHash(reinterpret_cast<quintptr>(this)));
        QString outPath = tempDir + "/" + filename;
        cv::imwrite(outPath.toStdString(), image);
        return outPath;
    }

    /// 构造 mock ToolResult.data
    virtual QJsonObject buildMockResult(const QString& outPath, const cv::Mat& input) const {
        QJsonObject data;
        data["reason"] = QString("[MOCK] %1 placeholder execution").arg(mockTag());
        data["outputImagePath"] = outPath;
        data["mockTag"] = mockTag();
        data["inputWidth"] = input.empty() ? 0 : input.cols;
        data["inputHeight"] = input.empty() ? 0 : input.rows;
        data["inputChannels"] = input.empty() ? 0 : input.channels();
        data["isMock"] = true;
        return data;
    }

protected:
    QJsonObject m_params;  ///< 配置参数（由 configure 保存）
};

} // namespace QDV

#endif // QDV_MOCK_OPERATOR_BASE_H
