#ifndef QDV_OPERATOR_HELP_PROVIDER_H
#define QDV_OPERATOR_HELP_PROVIDER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QMap>

/**
 * @brief 算子帮助内容提供器（v2.8.0）
 *
 * 双源加载帮助内容：
 * - config/operators.json：算子元数据（含 coreMeaning/scenario/caveats 字段）
 * - docs/algorithms/operators/*.md：完整算子手册（8 章节说明）
 *
 * 提供三个核心接口：
 * - getShortDesc(type): 返回一句话核心说明（coreMeaning 优先，回退 description/cnName）
 * - getFullDoc(type): 返回完整文档 QVariantMap（含 coreMeaning/scenario/params/caveats/relatedOperators）
 * - getParamHelp(type, paramName): 返回参数的 help 字段（回退到 "cnName (类型名)"）
 *
 * 设计要点：
 * - 单例模式（避免重复加载 200KB JSON + 14 个 Markdown 文件）
 * - 懒加载（首次调用时加载）
 * - 错误容忍（文件缺失或格式错误时返回空值，不崩溃）
 * - 线程安全：仅 UI 线程调用，无需加锁
 */
class OperatorHelpProvider : public QObject {
    Q_OBJECT
public:
    /// 获取单例实例（首次调用触发懒加载）
    static OperatorHelpProvider& instance();

    /// 获取算子短描述（coreMeaning 优先，回退到 description，再回退到 cnName）
    /// 返回值不超过 100 字符（超出自动截断并加省略号）
    QString getShortDesc(const QString& type) const;

    /// 获取算子完整文档
    /// 返回 QVariantMap，包含字段：
    /// - coreMeaning (QString): 核心含义（Markdown 源优先，回退 JSON）
    /// - scenario (QString): 适用场景（Markdown 源优先，回退 JSON）
    /// - description (QString): 功能描述（来自 operators.json）
    /// - cnName (QString): 中文名
    /// - category (QString): 分类
    /// - params (QVariantList): 参数列表，每项为 QVariantMap（含 name/cnName/type/help 等）
    /// - caveats (QStringList): 注意事项（Markdown 源优先，回退 JSON）
    /// - relatedOperators (QStringList): 关联算子（来自 Markdown）
    /// 算子不存在时返回空 QVariantMap
    QVariantMap getFullDoc(const QString& type) const;

    /// 获取参数帮助
    /// 返回参数的 help 字段；若 help 为空，返回 "cnName (类型名)" 格式
    /// 参数或算子不存在时返回空字符串
    QString getParamHelp(const QString& type, const QString& paramName) const;

    /// 测试辅助：重新加载数据（普通使用无需调用，单例首次访问自动加载）
    /// 用于单元测试中注入自定义路径
    void reload(const QString& operatorsJsonPath = QString(),
                const QString& markdownDir = QString());

    /// 测试辅助：清空缓存（单元测试间隔离用）
    void clearCache();

private:
    OperatorHelpProvider();
    Q_DISABLE_COPY(OperatorHelpProvider)

    /// 确保数据已加载（const 方法借助 mutable 缓冲懒加载）
    void ensureLoaded() const;

    /// 从 operators.json 加载算子元数据到 m_operatorMeta
    void loadOperatorsJson();

    /// 从 Markdown 手册加载完整文档到 m_operatorDocs
    void loadMarkdownDocs();

    /// 解析单个算子的 Markdown 文档段落
    /// @param type 算子 type（用于匹配 `## <Type>（...）` 标题）
    /// @param markdown 该算子段落（从 `## <Type>` 到下一个 `## ` 或文档末尾）
    /// @return QVariantMap 含 coreMeaning/scenario/caveats/relatedOperators 等字段
    QVariantMap parseOperatorMarkdown(const QString& type, const QString& markdown) const;

    /// ParamType int → 类型中文名映射（0=Int,1=Double,2=Enum,3=Bool,4=String,5=Vector,6=Mat）
    static QString paramTypeName(int typeInt);

    // 注：缓存成员声明为 mutable，使 const 查询方法能触发懒加载
    mutable bool m_loaded = false;
    QString m_operatorsJsonPath;   ///< operators.json 路径（空=使用默认路径）
    QString m_markdownDir;          ///< Markdown 手册目录（空=使用默认路径）

    /// 算子元数据缓存：type → 元数据 QVariantMap（含 coreMeaning/scenario/caveats/params 等）
    mutable QMap<QString, QVariantMap> m_operatorMeta;

    /// 算子 Markdown 文档缓存：type → 完整文档 QVariantMap
    mutable QMap<QString, QVariantMap> m_operatorDocs;
};

#endif // QDV_OPERATOR_HELP_PROVIDER_H
