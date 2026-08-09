// ============================================================================
// OperatorRecommender —— 智能算子推荐引擎实现（spec：editor-output-connection-optimization，Task 9）
// ============================================================================

#include "UI/OperatorRecommender.h"
#include "UI/EditViewBridge.h"
#include "Core/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QList>
#include <algorithm>

OperatorRecommender::OperatorRecommender(EditViewBridge* bridge, QObject* parent)
    : QObject(parent), m_bridge(bridge) {
    // config/recommender.json 位于可执行目录下 config/ 子目录
    m_configPath = QCoreApplication::applicationDirPath() + QStringLiteral("/config/recommender.json");
    loadWeights();
}

// ---------------------------------------------------------------------------
// 端口类型兼容性（字符串版，镜像 QDV::PortDescriptor::compatible）
// ---------------------------------------------------------------------------
bool OperatorRecommender::portTypesCompatible(const QString& outType, const QString& inType) {
    // Any 兼容一切（旧算子未声明端口时默认 Any）
    if (outType == QStringLiteral("Any") || inType == QStringLiteral("Any")) {
        return true;
    }
    if (outType == inType) {
        return true;
    }
    // Region/Contour/Points 都是点集类，互相兼容
    static const QSet<QString> kPointSet = { QStringLiteral("Region"),
                                             QStringLiteral("Contour"),
                                             QStringLiteral("Points") };
    if (kPointSet.contains(outType) && kPointSet.contains(inType)) {
        return true;
    }
    return false;
}

bool OperatorRecommender::isTypeCompat(const QString& curType, const QString& candType,
                                       QStringList& matchedOut) const {
    if (!m_bridge) return false;
    const QVariantMap curPorts  = m_bridge->getOperatorPorts(curType);
    const QVariantMap candPorts = m_bridge->getOperatorPorts(candType);

    const QVariantList curOuts   = curPorts.value("outputs").toList();
    const QVariantList candIns   = candPorts.value("inputs").toList();

    // 任一端未声明端口（旧算子兼容模式）→ 视为可能兼容（放行）
    // 注意：两端都为空时也放行，避免旧算子之间因端口缺失而完全失去推荐。
    if (curOuts.isEmpty() || candIns.isEmpty()) {
        return true;
    }

    bool anyCompat = false;
    for (const QVariant& outV : curOuts) {
        const QVariantMap out = outV.toMap();
        const QString outType = out.value("type").toString();
        for (const QVariant& inV : candIns) {
            const QVariantMap in = inV.toMap();
            const QString inType = in.value("type").toString();
            if (portTypesCompatible(outType, inType)) {
                matchedOut.append(out.value("name").toString());
                anyCompat = true;
                break;
            }
        }
    }
    return anyCompat;
}

// ---------------------------------------------------------------------------
// 推荐算法
// ---------------------------------------------------------------------------
QVariantList OperatorRecommender::recommend(const QString& nodeId) {
    QVariantList result;
    if (!m_bridge) return result;

    const QString curType = m_bridge->nodeTypeById(nodeId);
    if (curType.isEmpty()) {
        QDV::Logger::warn(QStringLiteral("[OperatorRecommender] recommend: 节点 %1 未找到").arg(nodeId));
        return result;
    }

    const QStringList allTypes = m_bridge->operatorTypes();

    // ---- 结构：统计当前算子类型在某类下游的出现频次 ----
    // 遍历所有连线，fromType==curType 的 to 节点类型即"下游消费者"
    QMap<QString,int> downstreamCount;
    const QVariantList conns = m_bridge->connections();
    int totalDownstream = 0;
    for (const QVariant& v : conns) {
        const QVariantMap c = v.toMap();
        const QString fromType = m_bridge->nodeTypeById(c.value("fromId").toString());
        if (fromType == curType) {
            const QString toType = m_bridge->nodeTypeById(c.value("toId").toString());
            if (!toType.isEmpty()) {
                downstreamCount[toType]++;
                totalDownstream++;
            }
        }
    }

    // ---- 使用频率：maxUse 用于归一化，recents 位置作加成 ----
    int maxUse = 0;
    for (auto it = m_bridge->m_useCounts.constBegin(); it != m_bridge->m_useCounts.constEnd(); ++it) {
        maxUse = qMax(maxUse, it.value());
    }
    const QStringList recents = m_bridge->m_recents;

    // ---- 遍历候选算子打分 ----
    QList<Candidate> cands;
    // 诊断：统计各候选的兼容情况，用于排查"推荐为空"
    int candCompatYes = 0, candCompatNo = 0, candPortMiss = 0;
    for (const QString& type : allTypes) {
        if (type == curType) continue;   // 排除当前算子自身

        Candidate cd;
        cd.type = type;

        // 1) 端口类型兼容分
        QStringList matchedOut;
        const bool compat = isTypeCompat(curType, type, matchedOut);
        if (compat) candCompatYes++; else candCompatNo++;
        double typeScore = 0.0;
        if (compat) {
            // 有兼容输出端口 → 1.0；未声明端口（旧算子）→ 0.5 未知分
            const QVariantList curOuts = m_bridge->getOperatorPorts(curType).value("outputs").toList();
            typeScore = curOuts.isEmpty() ? 0.5 : 1.0;
            if (!matchedOut.isEmpty()) {
                cd.reasons.append(QStringLiteral("端口类型兼容（输出 %1 可被消费）")
                                      .arg(matchedOut.join(QStringLiteral("/"))));
            }
        }

        // 2) 结构分：候选类型作为当前类型下游的归一化频次
        double structScore = 0.0;
        const int dc = downstreamCount.value(type, 0);
        if (totalDownstream > 0 && dc > 0) {
            structScore = static_cast<double>(dc) / static_cast<double>(totalDownstream);
            cd.reasons.append(QStringLiteral("常作为当前算子下游（出现 %1 次）").arg(dc));
        }

        // 3) 频率分：使用频次归一化 + 最近位置加成
        double freqScore = 0.0;
        if (maxUse > 0) {
            freqScore += 0.7 * static_cast<double>(m_bridge->m_useCounts.value(type, 0))
                        / static_cast<double>(maxUse);
        }
        const int rIdx = recents.indexOf(type);
        if (rIdx >= 0) {
            freqScore += 0.3 * (1.0 - static_cast<double>(rIdx) / 10.0);
        }
        if (freqScore > 0.0) {
            cd.reasons.append(QStringLiteral("最近/常用算子"));
        }

        // 线性加权
        cd.score = m_wTypeCompat * typeScore
                 + m_wStructure  * structScore
                 + m_wFrequency  * freqScore;

        // 仅保留有依据的候选（score>0）
        if (cd.score > 0.0) {
            cands.append(cd);
        }
    }

    // ---- 兜底：无端口匹配候选时，保证推荐区非空 ----
    // 场景：新方案无连线、无使用记录，且当前算子输出端口与所有候选输入端口都不匹配。
    // 此时按 频率>最近>库顺序 兜底推荐 Top3，reason 标注"通用推荐"。
    if (cands.isEmpty()) {
        QSet<QString> picked;
        auto addFallback = [&](const QString& type, const QString& reason, double score) {
            if (type.isEmpty() || type == curType || picked.contains(type)) return;
            if (cands.size() >= 3) return;
            picked.insert(type);
            Candidate cd;
            cd.type = type;
            cd.score = score;
            cd.reasons.append(reason);
            cands.append(cd);
        };
        // 1) 按使用频次降序
        QList<QPair<QString,int>> byUse;
        for (auto it = m_bridge->m_useCounts.constBegin(); it != m_bridge->m_useCounts.constEnd(); ++it)
            byUse.append({it.key(), it.value()});
        std::sort(byUse.begin(), byUse.end(),
                  [](const QPair<QString,int>& a, const QPair<QString,int>& b) { return a.second > b.second; });
        for (const auto& p : byUse) addFallback(p.first, QStringLiteral("常用算子"), 0.9);
        // 2) 最近使用
        for (const QString& t : m_bridge->m_recents) addFallback(t, QStringLiteral("最近使用"), 0.8);
        // 3) 库顺序（新方案无任何历史）取前几个，排除当前类型
        for (const QString& t : allTypes) addFallback(t, QStringLiteral("通用推荐"), 0.5);
    }

    // 按分数降序，取 Top3
    std::sort(cands.begin(), cands.end(),
              [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    const int topN = qMin(3, static_cast<int>(cands.size()));
    const double maxScore = topN > 0 ? cands.first().score : 0.0;
    for (int i = 0; i < topN; ++i) {
        const Candidate& cd = cands[i];
        QVariantMap item;
        item["type"]       = cd.type;
        item["confidence"] = (maxScore > 0.0) ? (cd.score / maxScore) : 0.0;
        QString reason;
        if (!cd.reasons.isEmpty()) {
            reason = cd.reasons.join(QStringLiteral("；"));
        } else {
            reason = m_bridge->getShortDesc(cd.type);   // 兜底：类型描述
        }
        item["reason"] = reason;
        result.append(item);
    }
    // 诊断：推荐为空时输出候选兼容情况，便于定位
    QDV::Logger::info(QStringLiteral("[OperatorRecommender] recommend curType=%1 candidates=%2 result=%3 compatYes=%4 compatNo=%5")
                      .arg(curType).arg(allTypes.size() - 1).arg(result.size())
                      .arg(candCompatYes).arg(candCompatNo));
    return result;
}

QVariantList OperatorRecommender::recommendRecent(const QString& nodeType) {
    QVariantList result;
    if (!m_bridge) return result;
    const QStringList recents = m_bridge->m_recents;
    const int total = recents.size();
    for (int i = 0; i < total; ++i) {
        const QString type = recents[i];
        if (type == nodeType) continue;   // 排除当前算子自身
        QVariantMap item;
        item["type"]       = type;
        // 越靠前越自信（线性衰减）
        item["confidence"] = (total > 0) ? (1.0 - static_cast<double>(i) / total) : 0.0;
        item["reason"]     = QStringLiteral("最近使用");
        result.append(item);
        if (result.size() >= 3) break;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 权重配置与持久化
// ---------------------------------------------------------------------------
void OperatorRecommender::setWeight(const QString& key, double v) {
    if (key == QStringLiteral("typeCompat")) {
        m_wTypeCompat = v;
    } else if (key == QStringLiteral("structure")) {
        m_wStructure = v;
    } else if (key == QStringLiteral("frequency")) {
        m_wFrequency = v;
    } else {
        return;   // 未知 key，不持久化
    }
    saveWeights();
}

double OperatorRecommender::getWeight(const QString& key) const {
    if (key == QStringLiteral("typeCompat")) return m_wTypeCompat;
    if (key == QStringLiteral("structure"))  return m_wStructure;
    if (key == QStringLiteral("frequency"))  return m_wFrequency;
    return 0.0;
}

void OperatorRecommender::loadWeights() {
    QFile file(m_configPath);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject obj = doc.object();
    if (obj.contains(QStringLiteral("typeCompat"))) m_wTypeCompat = obj.value(QStringLiteral("typeCompat")).toDouble();
    if (obj.contains(QStringLiteral("structure")))  m_wStructure  = obj.value(QStringLiteral("structure")).toDouble();
    if (obj.contains(QStringLiteral("frequency")))  m_wFrequency  = obj.value(QStringLiteral("frequency")).toDouble();
}

void OperatorRecommender::saveWeights() const {
    QDir().mkpath(QFileInfo(m_configPath).absolutePath());
    QJsonObject obj;
    obj[QStringLiteral("typeCompat")] = m_wTypeCompat;
    obj[QStringLiteral("structure")]  = m_wStructure;
    obj[QStringLiteral("frequency")]  = m_wFrequency;
    QFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();
}