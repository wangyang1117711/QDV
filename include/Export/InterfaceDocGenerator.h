#ifndef INTERFACE_DOC_GENERATOR_H
#define INTERFACE_DOC_GENERATOR_H

#include <QString>
#include <QStringList>
#include <QVariantList>

struct ExportConfig;

/**
 * @brief 生成 Markdown 接口文档
 *
 * 内容含：概述、部署说明、DLL/EXE/Python 调用说明、注意事项。
 */
class InterfaceDocGenerator {
public:
    /// 生成接口文档
    /// @param config 导出配置
    /// @param nodes 当前方案节点（QVariantList of QVariantMap）
    /// @param containsAi 是否含 AI 算子
    /// @return Markdown 文本
    static QString generate(const ExportConfig& config,
                           const QVariantList& nodes,
                           bool containsAi);

    /// 生成使用说明（快速上手）
    static QString generateQuickStart(const ExportConfig& config);
};

#endif // INTERFACE_DOC_GENERATOR_H
