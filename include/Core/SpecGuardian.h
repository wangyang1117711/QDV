#ifndef QDV_SPEC_GUARDIAN_H
#define QDV_SPEC_GUARDIAN_H

// SpecGuardian —— AGENTS.md v2.0 宪法规定的最高审计者
// 拥有最高管辖权，审计所有其他 Agent，可覆盖任何决策
//
// 对应章节：AGENTS.md v2.0
//   - I. 规则 2「确定性终门」
//   - I. 规则 3「状态主权」
//   - III. 强制行为规范「VERDICT 裁决协议」
//   - IV. 治理与执行「SpecGuardian 拥有最高管辖权」

#include <QObject>
#include <QMap>
#include <QList>
#include <QMutex>
#include <QVariantMap>
#include <QJsonObject>
#include <functional>

#include "Verdict.h"

namespace QDV {

/// SpecGuardian：Agent 协作框架的最高审计者与终门守卫
///
/// 职责（AGENTS.md v2.0）：
/// 1. 审计其他 Agent 的输出 —— 基于已注册的审计规则集
/// 2. 状态主权（State Sovereignty）—— 管理共享状态的 ACL（读/写权限）
/// 3. 确定性终门（Deterministic Final Gate）—— 用 JSON Schema 校验输出
/// 4. 审计历史记录 —— 供复盘与回溯
///
/// 单例：全局唯一实例，通过 instance() 获取（Meyers 单例，线程安全）
class SpecGuardian : public QObject {
    Q_OBJECT
public:
    /// 审计规则函数：接收 (agent输出, 契约规格)，返回单项检查结果
    using RuleFn = std::function<Verdict::Check(const QVariantMap&, const QVariantMap&)>;

    /// 获取单例实例（线程安全）
    static SpecGuardian* instance();

    // ===== 审计接口 =====

    /// 审计 Agent 输出
    /// 对 (output, spec) 依次运行所有已注册规则，汇总为 VERDICT
    /// - 任一规则失败 → Verdict::FAIL（阻断）
    /// - 全部通过 → Verdict::PASS
    /// - 无规则 → Verdict::PASS（vacuously true）
    /// 结果写入审计历史，并发射 verdictIssued / blockingVerdict 信号
    Verdict auditAgentOutput(const QString& agentId, const QString& taskId,
                             const QVariantMap& output, const QVariantMap& spec);

    /// 注册审计规则（同名规则会被覆盖）
    void registerRule(const QString& ruleName, RuleFn rule);

    /// 查询审计历史
    /// agentId 为空时返回所有 Agent 的历史；否则返回指定 Agent 的历史
    QList<Verdict> getAuditHistory(const QString& agentId = {}) const;

    // ===== 状态主权：ACL 管理 =====

    /// 查询 Agent 是否对 stateKey 有读权限（默认拒绝，符合最小权限原则）
    bool canRead(const QString& agentId, const QString& stateKey) const;

    /// 查询 Agent 是否对 stateKey 有写权限（默认拒绝）
    bool canWrite(const QString& agentId, const QString& stateKey) const;

    /// 授予 Agent 对 stateKey 的读/写权限
    void grantAccess(const QString& agentId, const QString& stateKey, bool read, bool write);

    // ===== 确定性终门 =====

    /// 使用 JSON Schema 校验 output
    /// schema 格式（简化版 JSON Schema）：
    /// {
    ///   "required": ["field1", "field2"],     // 必填字段名列表
    ///   "properties": {                        // 字段类型约束
    ///     "field1": {"type": "string"},
    ///     "field2": {"type": "number"}
    ///   }
    /// }
    /// 支持的 type：string / number / bool / object / array
    Verdict finalGate(const QString& taskId, const QVariantMap& output,
                      const QJsonObject& schema);

signals:
    /// 每次审计产生裁决时发射
    void verdictIssued(const QString& agentId, const QDV::Verdict& verdict);

    /// 产生阻断性裁决（FAIL）时发射 —— 必须阻断流程并通知人工
    void blockingVerdict(const QString& agentId, const QString& reason);

private:
    SpecGuardian(QObject* parent = nullptr);

    // 单例禁止拷贝/移动
    SpecGuardian(const SpecGuardian&) = delete;
    SpecGuardian& operator=(const SpecGuardian&) = delete;

    /// 生成 ACL 内部 key：agentId + 分隔符 + stateKey
    static QString aclKey(const QString& agentId, const QString& stateKey);

    /// 根据「类型字符串」校验 QVariant 值类型是否匹配
    static bool matchesType(const QVariant& value, const QString& expectedType);

    mutable QMutex m_mutex;                                 ///< 保护并发访问
    QMap<QString, QList<Verdict>> m_auditHistory;           ///< agentId -> verdicts
    QMap<QString, QPair<bool, bool>> m_acl;                 ///< "agentId\x1fkey" -> (read, write)
    QMap<QString, RuleFn> m_rules;                          ///< ruleName -> rule
};

} // namespace QDV

#endif // QDV_SPEC_GUARDIAN_H
