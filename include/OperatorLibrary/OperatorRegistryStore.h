#ifndef QDV_OPERATORLIBRARY_REGISTRYSTORE_H
#define QDV_OPERATORLIBRARY_REGISTRYSTORE_H

// =====================================================================
// OperatorRegistryStore — 已导入算子注册表的持久化层
//
// 设计契约（spec §3.3 + tasks.md Task 1.1）：
//   - 一算子一文件：config/operators_imported/<type>.qdvop
//   - 原子写入：先写 <type>.qdvop.tmp → QFile::rename 为 <type>.qdvop
//   - 备份策略：保存前若文件已存在，先复制为 <type>.qdvop.bak（仅保留最近一份）
//   - 扫描规则：按文件名字典序加载 *.qdvop，跳过 *.tmp / *.bak；损坏文件记日志+跳过
//   - 降级目录：QStandardPaths::AppDataLocation/operators_imported/
//   - 编码：UTF-8（不带 BOM，与 config/operators.json 实际一致）
//
// 不在本切片（O1a）：
//   - 版本历史（O1e）：loadVersions/saveVersions/history/appendVersion
//   - 依赖反查（O2）：dependentsOf/setDependentsCache
//   - 单文件 operators.json 模式（被一算子一文件模式替代）
// =====================================================================

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include "OperatorLibrary/OperatorDefinition.h"

namespace QDV {
namespace OperatorLibrary {

class OperatorRegistryStore : public QObject {
    Q_OBJECT
public:
    /// 单例入口（与 OperatorDescriptors::instance 风格一致）
    static OperatorRegistryStore* instance();

    /// 初始化持久化目录
    /// - 自动创建目录（含父目录）
    /// - 创建失败时降级到 QStandardPaths::AppDataLocation/operators_imported/
    /// - 成功后立即 scanAll() 加载已有算子
    /// @return true=目录就绪并已扫描；false=目录创建彻底失败
    bool init(const QString& dirPath, QString* err = nullptr);

    /// 扫描目录下所有 .qdvop 文件并加载到内存缓存
    /// - 按文件名字典序加载（保证多次启动行为一致）
    /// - 跳过 *.tmp / *.bak 后缀
    /// - 损坏文件：记日志 + 跳过（不阻断启动）
    /// @return 成功加载的算子数量
    int scanAll(QString* err = nullptr);

    /// 保存单个算子（原子写入 + 备份）
    /// - 路径：<dir>/<type>.qdvop
    /// - 流程：写 <type>.qdvop.tmp → 备份原文件为 <type>.qdvop.bak → rename tmp 为正式文件
    /// - 成功后更新内存缓存 m_loaded[type] = def
    /// @return true=保存成功；false=失败（err 填充原因）
    bool save(const OperatorDef& def, QString* err = nullptr);

    /// 删除单个算子文件
    /// - 同时清理 .bak 文件（如有）
    /// - 成功后从内存缓存移除
    /// @return true=删除成功或文件本就不存在；false=删除失败（err 填充原因）
    bool remove(const QString& type, QString* err = nullptr);

    /// 查询：是否已存在（仅查内存缓存，不查文件系统）
    bool contains(const QString& type) const;

    /// 查询：按 type 获取算子定义（不存在返回空 OperatorDef，*ok=false）
    OperatorDef get(const QString& type, bool* ok = nullptr) const;

    /// 查询：列出全部已加载算子（按 type 字典序）
    QList<OperatorDef> all() const;

    /// 查询：列出全部已加载算子的 type 集合
    QStringList types() const;

    /// 查询：分类下的全部算子
    QList<OperatorDef> byCategory(const QString& category) const;

    /// 查询：所有已出现的分类集合
    QStringList categories() const;

    /// 查询：按分类 + 关键字过滤（keyword 在 type/cnName/tags 中匹配）
    QList<OperatorDef> query(const QString& category, const QString& keyword) const;

    /// 当前持久化目录路径（供 Controller/测试用）
    QString directory() const { return m_dirPath; }

    /// 推算某 type 对应的文件路径（供 Controller 二次校验防 TOCTOU）
    QString filePathFor(const QString& type) const;

signals:
    /// 单算子保存成功
    void operatorSaved(const QString& type);
    /// 单算子删除成功
    void operatorRemoved(const QString& type);
    /// 通用变更信号（save/remove 均会发射）
    void changed();

private:
    explicit OperatorRegistryStore(QObject* parent = nullptr);

    /// 尝试创建目录，失败时降级到 QStandardPaths
    bool ensureDirectory(const QString& preferredPath, QString* err);

    /// 原子写入单个文件（tmp → rename）
    bool atomicWriteFile(const QString& targetPath, const QByteArray& data, QString* err);

    /// 备份已存在的文件为 .bak（覆盖旧 .bak）
    bool backupIfExists(const QString& targetPath);

    QString                 m_dirPath;            // 实际生效的持久化目录
    QMap<QString, OperatorDef> m_loaded;          // type -> OperatorDef 内存缓存
    static OperatorRegistryStore* s_instance;
};

} // namespace OperatorLibrary
} // namespace QDV

#endif // QDV_OPERATORLIBRARY_REGISTRYSTORE_H
