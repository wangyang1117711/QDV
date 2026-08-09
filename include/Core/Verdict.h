#ifndef QDV_VERDICT_H
#define QDV_VERDICT_H

// VERDICT 裁决协议数据结构（遵循 AGENTS.md v2.0 规范）
// 所有 Agent 评审输出必须遵循此结构，无一例外
//
// 对应章节：AGENTS.md v2.0「III. 强制行为规范 - 1. VERDICT 裁决协议」

#include <QString>
#include <QList>
#include <QMetaType>

namespace QDV {

/// VERDICT 裁决结果
/// 结构遵循 AGENTS.md v2.0「III. 强制行为规范 - VERDICT 裁决协议」
struct Verdict {
    enum Status { PASS, FAIL, NEEDS_HUMAN_REVIEW };

    Status status = PASS;           ///< 裁决状态
    double confidence = 0.0;        ///< 置信度 0.0-1.0，表示对裁决结果的把握程度

    /// 单项检查结果
    struct Check {
        QString name;               ///< 检查项名称
        bool passed = true;         ///< 是否通过
        QString reason;             ///< 失败原因（通过时为空）
        QString evidenceLink;       ///< 证据链接，形如 trace://session_xxx
    };

    QList<Check> checks;            ///< 检查项列表

    // ===== 便捷方法 =====

    /// 是否通过
    bool isPass() const { return status == PASS; }

    /// 是否阻断（FAIL 必须阻断流程；NEEDS_HUMAN_REVIEW 不阻断但需人工介入）
    bool isBlocking() const { return status == FAIL; }

    /// 格式化为 AGENTS.md v2.0 规定的 VERDICT 文本（实现在 SpecGuardian.cpp）
    QString toString() const;

    /// 从 VERDICT 文本解析（实现在 SpecGuardian.cpp）
    static Verdict fromString(const QString& str);

    // ===== 静态工厂方法 =====

    /// 构造「通过」裁决
    static Verdict passResult(double confidence, const QList<Check>& checks = {}) {
        Verdict v;
        v.status = PASS;
        v.confidence = confidence;
        v.checks = checks;
        return v;
    }

    /// 构造「失败」裁决（附带失败原因与证据链接）
    static Verdict failResult(const QString& reason, double confidence = 1.0,
                              const QString& evidence = {}) {
        Verdict v;
        v.status = FAIL;
        v.confidence = confidence;
        Check c;
        c.name = QStringLiteral("失败检查");
        c.passed = false;
        c.reason = reason;
        c.evidenceLink = evidence;
        v.checks.append(c);
        return v;
    }

    /// 构造「需人工审查」裁决
    static Verdict needsHumanReview(const QString& reason, double confidence = 0.5) {
        Verdict v;
        v.status = NEEDS_HUMAN_REVIEW;
        v.confidence = confidence;
        Check c;
        c.name = QStringLiteral("需人工审查");
        c.passed = false;
        c.reason = reason;
        v.checks.append(c);
        return v;
    }
};

} // namespace QDV

// 注册元类型，使 Verdict 可用于信号槽（含 queued connection）与 QVariant
Q_DECLARE_METATYPE(QDV::Verdict)

#endif // QDV_VERDICT_H
