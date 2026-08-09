#ifndef CLIPBOARD_MANAGER_H
#define CLIPBOARD_MANAGER_H

// ============================================================================
// ClipboardManager —— 算子参数剪贴板管理器（从 EditViewBridge 拆分，Task 6）
// ----------------------------------------------------------------------------
// 职责：
//   1. 持有单条算子参数剪贴板状态（覆盖式：新复制覆盖旧数据）
//   2. 提供从节点列表拷贝参数的 copyFrom
//   3. 提供参数粘贴的"类型转换 + 字段匹配"纯逻辑 buildPaste
//
// 设计要点：
//   - 本类为纯逻辑协作者，不发射 Qt 信号；错误信息通过返回结构体由
//     EditViewBridge 统一转换为 errorRaised 信号（保持 phase 字符串契约）。
//   - 参数校验依赖通过 std::function 回调注入，避免与 EditViewBridge::validateParam
//     形成循环依赖，同时复用既有校验逻辑（不重复实现）。
// ============================================================================

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class ClipboardManager {
public:
    ClipboardManager() = default;
    ~ClipboardManager() = default;

    // === 状态查询 ===
    bool hasData() const { return m_clipHasData; }
    QString type() const { return m_clipType; }
    QVariantMap params() const { return m_clipParams; }

    // === 写入 / 清空 ===
    /// 从节点列表中查找源节点，拷贝其 type + params 到剪贴板
    /// @return true=成功；false=节点不存在（剪贴板状态不变）
    bool copyFrom(const QVariantList& nodes, const QString& nodeId);

    /// 直接设置剪贴板内容（用于测试或外部注入）
    void setClip(const QString& type, const QVariantMap& params);

    /// 清空剪贴板
    void clear();

    // === 粘贴构建 ===
    /// 构建粘贴到目标节点的合并参数（不直接写入，由调用方走 UndoCommand 框架）
    /// @param targetNode 目标节点数据（含 type / params）
    /// @param validator 参数校验回调：(targetType, paramName, value) -> 错误列表（空表示通过）
    /// @return PasteResult：applied=false 表示无可粘贴参数；mergedParams 为合并后待写入参数
    struct PasteResult {
        bool applied = false;          ///< 是否有可粘贴参数
        int  appliedCount = 0;         ///< 成功粘贴的参数个数
        QVariantMap mergedParams;      ///< 合并后参数（用于 updateOperatorParams）
        QStringList skippedNames;      ///< 跳过的参数名
        QString clipType;              ///< 剪贴板来源算子类型（用于错误提示）
    };
    PasteResult buildPaste(const QVariantMap& targetNode,
                           const std::function<QStringList(const QString&,
                                                           const QString&,
                                                           const QVariant&)>& validator) const;

private:
    bool            m_clipHasData = false;  ///< 剪贴板是否有数据
    QString         m_clipType;             ///< 剪贴板参数来源的算子类型
    QVariantMap     m_clipParams;           ///< 剪贴板参数内容
};

#endif // CLIPBOARD_MANAGER_H
