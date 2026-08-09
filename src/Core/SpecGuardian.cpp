// SpecGuardian.cpp - SpecGuardian 最高审计者实现 + Verdict 文本序列化实现
//
// 本文件包含两部分实现：
//   1. Verdict 的 toString()/fromString() 文本序列化（VERDICT 裁决协议文本格式）
//   2. SpecGuardian 单例的所有方法（审计 / ACL / 确定性终门 / 审计历史）
//
// 对应：AGENTS.md v2.0「III. 强制行为规范」「IV. 治理与执行」

#include "SpecGuardian.h"

#include <QMutexLocker>
#include <QTextStream>
#include <QStringList>
#include <QJsonArray>
#include <QJsonValue>
#include <QMetaType>
#include <QChar>

namespace QDV {

// 文件内辅助函数（匿名命名空间，仅本翻译单元可见）
namespace {
/// 去除字符串首尾成对的双引号；无引号则原样返回
QString stripQuotes(const QString& s) {
    QString t = s.trimmed();
    if (t.size() >= 2 && t.startsWith('"') && t.endsWith('"')) {
        return t.mid(1, t.size() - 2);
    }
    return t;
}
} // anonymous namespace

// ============================================================
// Verdict 文本序列化实现
// ============================================================

QString Verdict::toString() const {
    // 格式遵循 AGENTS.md v2.0「VERDICT 裁决协议」规范：
    //   VERDICT: PASS
    //   confidence: 0.95
    //   checks:
    //     - name: "契约合规性"
    //       status: pass
    //     - name: "业务逻辑对齐"
    //       status: fail
    //       reason: "退款金额错误"
    //       evidence_link: "trace://session_12345"
    QString out;
    QTextStream ts(&out);

    // 状态行
    ts << "VERDICT: ";
    switch (status) {
        case PASS:               ts << "PASS"; break;
        case FAIL:               ts << "FAIL"; break;
        case NEEDS_HUMAN_REVIEW: ts << "NEEDS_HUMAN_REVIEW"; break;
    }
    ts << "\n";

    // 置信度行
    ts << "confidence: " << QString::number(confidence, 'f', 2) << "\n";

    // 检查项段
    if (!checks.isEmpty()) {
        ts << "checks:\n";
        for (const auto& c : checks) {
            ts << "  - name: \"" << c.name << "\"\n";
            ts << "    status: " << (c.passed ? "pass" : "fail") << "\n";
            if (!c.passed && !c.reason.isEmpty()) {
                ts << "    reason: \"" << c.reason << "\"\n";
            }
            if (!c.evidenceLink.isEmpty()) {
                ts << "    evidence_link: \"" << c.evidenceLink << "\"\n";
            }
        }
    }

    ts.flush();
    return out;
}

Verdict Verdict::fromString(const QString& str) {
    // 解析 toString() 输出格式。按行扫描，容错跳过无法识别的行。
    Verdict v;
    const QStringList lines = str.split('\n', Qt::SkipEmptyParts);

    // 前缀常量（编译期生成，避免重复构造）
    const QString kVerdictPrefix    = QStringLiteral("VERDICT:");
    const QString kConfidencePrefix = QStringLiteral("confidence:");
    const QString kChecksPrefix     = QStringLiteral("checks:");
    const QString kNamePrefix       = QStringLiteral("  - name:");
    const QString kStatusPrefix     = QStringLiteral("    status:");
    const QString kReasonPrefix     = QStringLiteral("    reason:");
    const QString kEvidencePrefix   = QStringLiteral("    evidence_link:");

    int idx = 0;

    // 1. 解析 VERDICT: 行
    for (; idx < lines.size(); ++idx) {
        if (lines[idx].startsWith(kVerdictPrefix)) {
            const QString s = lines[idx].mid(kVerdictPrefix.length()).trimmed();
            if (s == QStringLiteral("PASS")) v.status = PASS;
            else if (s == QStringLiteral("FAIL")) v.status = FAIL;
            else if (s == QStringLiteral("NEEDS_HUMAN_REVIEW")) v.status = NEEDS_HUMAN_REVIEW;
            ++idx;
            break;
        }
    }

    // 2. 解析 confidence: 行
    for (; idx < lines.size(); ++idx) {
        if (lines[idx].startsWith(kConfidencePrefix)) {
            const QString s = lines[idx].mid(kConfidencePrefix.length()).trimmed();
            v.confidence = s.toDouble();
            ++idx;
            break;
        }
    }

    // 3. 跳到 checks: 段
    for (; idx < lines.size(); ++idx) {
        if (lines[idx].startsWith(kChecksPrefix)) {
            ++idx;
            break;
        }
    }

    // 4. 逐项解析 check 块
    Verdict::Check current;
    bool inCheck = false;
    for (; idx < lines.size(); ++idx) {
        const QString& line = lines[idx];
        if (line.startsWith(kNamePrefix)) {
            // 遇到新 check 项，先把上一个提交
            if (inCheck) {
                v.checks.append(current);
            }
            current = Verdict::Check();
            current.name = stripQuotes(line.mid(kNamePrefix.length()).trimmed());
            inCheck = true;
        } else if (line.startsWith(kStatusPrefix)) {
            const QString s = line.mid(kStatusPrefix.length()).trimmed();
            current.passed = (s == QStringLiteral("pass"));
        } else if (line.startsWith(kReasonPrefix)) {
            current.reason = stripQuotes(line.mid(kReasonPrefix.length()).trimmed());
        } else if (line.startsWith(kEvidencePrefix)) {
            current.evidenceLink = stripQuotes(line.mid(kEvidencePrefix.length()).trimmed());
        }
    }
    if (inCheck) {
        v.checks.append(current);
    }

    return v;
}

// ============================================================
// SpecGuardian 实现
// ============================================================

SpecGuardian* SpecGuardian::instance() {
    // Meyers 单例，C++11 起局部静态变量初始化线程安全
    static SpecGuardian inst;
    return &inst;
}

SpecGuardian::SpecGuardian(QObject* parent) : QObject(parent) {
    // 注册元类型，使 Verdict 可用于 queued connection 与 QVariant
    // Q_DECLARE_METATYPE 已在 Verdict.h 中声明，这里完成运行时注册
    qRegisterMetaType<Verdict>("QDV::Verdict");
}

QString SpecGuardian::aclKey(const QString& agentId, const QString& stateKey) {
    // 用 Unit Separator (0x1F) 拼接，避免与正常字符串内容冲突
    return agentId + QChar(0x1F) + stateKey;
}

bool SpecGuardian::matchesType(const QVariant& value, const QString& expectedType) {
    // 根据 schema 中的 type 字符串校验 QVariant 实际类型
    // 未指定类型时不做校验（向前兼容）
    if (expectedType.isEmpty()) return true;

    const int id = value.metaType().id();

    if (expectedType == "string") {
        return id == QMetaType::QString;
    }
    if (expectedType == "number") {
        // 整数与浮点均视为 number
        return id == QMetaType::Int      || id == QMetaType::Double
            || id == QMetaType::LongLong  || id == QMetaType::ULongLong
            || id == QMetaType::UInt      || id == QMetaType::Float;
    }
    if (expectedType == "bool") {
        return id == QMetaType::Bool;
    }
    if (expectedType == "object") {
        return id == QMetaType::QVariantMap || id == QMetaType::QJsonObject;
    }
    if (expectedType == "array") {
        return id == QMetaType::QVariantList || id == QMetaType::QJsonArray;
    }
    // 未知类型名不阻断（向前兼容）
    return true;
}

Verdict SpecGuardian::auditAgentOutput(const QString& agentId, const QString& taskId,
                                       const QVariantMap& output, const QVariantMap& spec) {
    // 审计流程：
    // 1. 持锁运行所有已注册规则（注意：规则函数不应回调 SpecGuardian 方法，否则死锁）
    // 2. 汇总结论：任一失败即 FAIL，否则 PASS；无规则视作 vacuously PASS
    // 3. 写入审计历史
    // 4. 解锁后发射信号（避免槽函数回调导致递归死锁）

    Verdict v;
    QList<Verdict::Check> checks;
    int passed = 0;
    int total = 0;

    {
        QMutexLocker locker(&m_mutex);
        for (auto it = m_rules.constBegin(); it != m_rules.constEnd(); ++it) {
            const QString& ruleName = it.key();
            const RuleFn& rule = it.value();

            Verdict::Check check;
            check.name = ruleName;
            try {
                check = rule(output, spec);
                // 规则可能未设置 name，回填规则名保证可追溯
                if (check.name.isEmpty()) {
                    check.name = ruleName;
                }
            } catch (...) {
                // 规则抛异常视为该项失败，不中断整体审计
                check.passed = false;
                check.reason = QStringLiteral("规则执行抛出异常");
            }

            if (check.passed) ++passed;
            ++total;
            checks.append(check);
        }

        v.checks = checks;
        if (total == 0 || passed == total) {
            // 无规则 = vacuously true；全部通过 = PASS
            v.status = Verdict::PASS;
            v.confidence = 1.0;
        } else {
            v.status = Verdict::FAIL;
            // confidence = 失败占比（失败越多，对 FAIL 裁决越自信）
            v.confidence = static_cast<double>(total - passed) / static_cast<double>(total);
        }

        // 写入审计历史（带 agentId 维度，便于按 Agent 回溯）
        m_auditHistory[agentId].append(v);
    } // 解锁

    // 解锁后发射信号，避免槽函数回调 SpecGuardian 导致递归死锁
    emit verdictIssued(agentId, v);
    if (v.isBlocking()) {
        // 取第一条失败原因作为阻断理由
        QString reason;
        for (const auto& c : v.checks) {
            if (!c.passed) {
                reason = c.reason;
                break;
            }
        }
        emit blockingVerdict(agentId, reason);
    }

    Q_UNUSED(taskId);
    return v;
}

void SpecGuardian::registerRule(const QString& ruleName, RuleFn rule) {
    QMutexLocker locker(&m_mutex);
    m_rules[ruleName] = std::move(rule);
}

QList<Verdict> SpecGuardian::getAuditHistory(const QString& agentId) const {
    QMutexLocker locker(&m_mutex);
    if (agentId.isEmpty()) {
        // 汇总所有 Agent 的历史（QMap 按 key 排序，结果稳定）
        QList<Verdict> all;
        for (auto it = m_auditHistory.constBegin(); it != m_auditHistory.constEnd(); ++it) {
            all.append(it.value());
        }
        return all;
    }
    return m_auditHistory.value(agentId);
}

bool SpecGuardian::canRead(const QString& agentId, const QString& stateKey) const {
    // 默认拒绝：未显式授权的一律无读权限（最小权限原则）
    QMutexLocker locker(&m_mutex);
    auto it = m_acl.constFind(aclKey(agentId, stateKey));
    if (it == m_acl.constEnd()) return false;
    return it.value().first;
}

bool SpecGuardian::canWrite(const QString& agentId, const QString& stateKey) const {
    // 默认拒绝：未显式授权的一律无写权限
    QMutexLocker locker(&m_mutex);
    auto it = m_acl.constFind(aclKey(agentId, stateKey));
    if (it == m_acl.constEnd()) return false;
    return it.value().second;
}

void SpecGuardian::grantAccess(const QString& agentId, const QString& stateKey, bool read, bool write) {
    QMutexLocker locker(&m_mutex);
    m_acl[aclKey(agentId, stateKey)] = qMakePair(read, write);
}

Verdict SpecGuardian::finalGate(const QString& taskId, const QVariantMap& output,
                                const QJsonObject& schema) {
    // 确定性终门：用代码/规则/Schema 校验输出（AGENTS.md v2.0 规则 2）
    // 两类检查：
    //   1. required —— 必填字段存在性
    //   2. properties —— 字段类型匹配
    // 任一失败即 FAIL（阻断发布）
    QList<Verdict::Check> checks;
    bool allPassed = true;

    // 1. 检查 required 必填字段
    const QJsonArray required = schema.value("required").toArray();
    for (const QJsonValue& reqVal : required) {
        const QString field = reqVal.toString();
        Verdict::Check c;
        c.name = QStringLiteral("必填字段: %1").arg(field);
        if (output.contains(field)) {
            c.passed = true;
        } else {
            c.passed = false;
            c.reason = QStringLiteral("缺少必填字段: %1").arg(field);
            c.evidenceLink = QStringLiteral("trace://task/%1").arg(taskId);
            allPassed = false;
        }
        checks.append(c);
    }

    // 2. 检查 properties 字段类型
    const QJsonObject properties = schema.value("properties").toObject();
    for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
        const QString field = it.key();
        if (!output.contains(field)) {
            continue;  // 缺失字段由 required 检查负责；非必填则跳过类型检查
        }
        const QJsonObject fieldSpec = it.value().toObject();
        const QString expectedType = fieldSpec.value("type").toString();

        Verdict::Check c;
        c.name = QStringLiteral("字段类型: %1").arg(field);
        if (matchesType(output.value(field), expectedType)) {
            c.passed = true;
        } else {
            c.passed = false;
            c.reason = QStringLiteral("字段 %1 类型不匹配，期望 %2")
                           .arg(field, expectedType);
            c.evidenceLink = QStringLiteral("trace://task/%1").arg(taskId);
            allPassed = false;
        }
        checks.append(c);
    }

    Verdict v;
    v.checks = checks;
    if (allPassed) {
        v.status = Verdict::PASS;
        v.confidence = 1.0;
    } else {
        v.status = Verdict::FAIL;
        v.confidence = 1.0;
    }
    return v;
}

} // namespace QDV
