// ============================================================================
// OutputConflictDetector —— 输入/输出项冲突检测引擎实现（spec：editor-output-connection-optimization）
// ============================================================================

#include "UI/OutputConflictDetector.h"
#include "UI/EditViewBridge.h"
#include "UI/PortBindingManager.h"

#include <QMap>

OutputConflictDetector::OutputConflictDetector(EditViewBridge* bridge, QObject* parent)
    : QObject(parent), m_bridge(bridge),
      m_bindings(bridge ? qobject_cast<PortBindingManager*>(bridge->portBindingManager())
                        : nullptr) {}

void OutputConflictDetector::appendConflict(QVariantList& out, const Conflict& c) const {
    QVariantMap m;
    m["kind"]       = c.kind;
    m["nodeId"]     = c.nodeId;
    m["portName"]   = c.portName;
    m["detail"]     = c.detail;
    m["candidates"] = c.candidates;
    out.append(m);
}

QString OutputConflictDetector::nodeTypeById(const QString& nodeId) const {
    if (!m_bridge) return QString();
    const QVariantList nodes = m_bridge->currentNodes();
    for (const QVariant& v : nodes) {
        const QVariantMap n = v.toMap();
        if (n.value("id").toString() == nodeId) {
            return n.value("type").toString();
        }
    }
    return QString();
}

QVariantList OutputConflictDetector::outputMetaForNode(const QString& nodeId) const {
    if (!m_bridge) return {};
    const QString type = nodeTypeById(nodeId);
    if (type.isEmpty()) return {};
    return m_bridge->getOperatorMeta(type).value("outputs").toList();
}

QVariantList OutputConflictDetector::inputMetaForNode(const QString& nodeId) const {
    if (!m_bridge) return {};
    const QString type = nodeTypeById(nodeId);
    if (type.isEmpty()) return {};
    return m_bridge->getOperatorMeta(type).value("inputs").toList();
}

QVariantMap OutputConflictDetector::findOutputMeta(const QString& nodeId,
                                                   const QString& portName) const {
    const QVariantList outputs = outputMetaForNode(nodeId);
    for (const QVariant& v : outputs) {
        const QVariantMap om = v.toMap();
        if (om.value("name").toString() == portName) return om;
    }
    return QVariantMap{};
}

QString OutputConflictDetector::outputTypeFor(const QString& nodeId,
                                              const QString& portName) const {
    return findOutputMeta(nodeId, portName).value("typeName").toString();
}

QString OutputConflictDetector::outputAliasFor(const QString& nodeId,
                                               const QString& portName) const {
    return findOutputMeta(nodeId, portName).value("alias").toString();
}

bool OutputConflictDetector::isWildcardType(const QString& typeName) {
    return typeName.isEmpty() || typeName.compare(QStringLiteral("Any"), Qt::CaseInsensitive) == 0;
}

QVariantList OutputConflictDetector::detectAll() const {
    QVariantList out;
    if (!m_bridge) return out;
    const QVariantList nodes = m_bridge->currentNodes();

    // 1) 同名冲突（dupAlias）：同一节点多个输出端口 alias 相同
    for (const QVariant& v : nodes) {
        const QVariantMap node   = v.toMap();
        const QString     nodeId = node.value("id").toString();
        const QVariantList outputs = outputMetaForNode(nodeId);
        // alias → 端口名列表（同一节点内）
        QMap<QString, QStringList> aliasMap;
        for (const QVariant& o : outputs) {
            const QVariantMap om = o.toMap();
            const QString alias  = om.value("alias").toString();
            const QString name   = om.value("name").toString();
            if (alias.isEmpty() || name.isEmpty()) continue;
            aliasMap[alias].append(name);
        }
        for (auto it = aliasMap.begin(); it != aliasMap.end(); ++it) {
            if (it.value().size() > 1) {
                Conflict c;
                c.kind       = QStringLiteral("dupAlias");
                c.nodeId     = nodeId;
                c.portName   = it.key();   // 重复的别名
                c.detail     = QStringLiteral("节点 %1 存在多个输出端口别名相同（%2）：%3")
                                   .arg(nodeId, it.key(), it.value().join(QStringLiteral("、")));
                c.candidates = it.value();
                appendConflict(out, c);
            }
        }
    }

    // 2) 类型不兼容（typeMismatch）：逐条连接校验上游输出→下游输入类型
    const QVariantList conns = m_bridge->connections();
    for (const QVariant& v : conns) {
        const QVariantMap c   = v.toMap();
        const QString fromId  = c.value("fromId").toString();
        const QString fromP   = c.value("fromPort").toString();
        const QString toId    = c.value("toId").toString();
        const QString toP     = c.value("toPort").toString();
        if (!m_bridge->checkPortCompatible(fromId, fromP, toId, toP)) {
            Conflict cf;
            cf.kind       = QStringLiteral("typeMismatch");
            cf.nodeId     = toId;       // 指向下游节点
            cf.portName   = toP;
            cf.detail     = QStringLiteral("端口类型不兼容：%1[%2] → %3[%4]")
                                .arg(fromId, fromP, toId, toP);
            cf.candidates = QStringList{ fromId + QStringLiteral("[") + fromP + QStringLiteral("]") };
            appendConflict(out, cf);
        }
    }

    // 3) 多输入绑定歧义（multiInputAmbiguity）：下游输入被多个上游绑定且能力冲突/别名相同
    for (const QVariant& v : nodes) {
        const QVariantMap node   = v.toMap();
        const QString     nodeId = node.value("id").toString();
        const QVariantList inputs = inputMetaForNode(nodeId);
        for (const QVariant& iv : inputs) {
            const QString inputPort = iv.toMap().value("name").toString();
            if (inputPort.isEmpty()) continue;
            const auto bindings = m_bindings
                                      ? m_bindings->bindingsForInput(nodeId, inputPort)
                                      : QList<PortBindingManager::Binding>{};
            if (bindings.size() < 2) continue;

            QStringList typeSet;    // 非通配的输入类型集合
            QStringList aliasSet;   // 上游输出别名集合
            QStringList candidates;
            for (const PortBindingManager::Binding& b : bindings) {
                const QString upType = outputTypeFor(b.upstreamToolId, b.upstreamPort);
                if (!isWildcardType(upType) && !typeSet.contains(upType)) typeSet.append(upType);
                const QString upAlias = outputAliasFor(b.upstreamToolId, b.upstreamPort);
                if (!upAlias.isEmpty() && !aliasSet.contains(upAlias)) aliasSet.append(upAlias);
                candidates.append(b.upstreamToolId + QStringLiteral("[") + b.upstreamPort +
                                  QStringLiteral("]"));
            }

            const bool typeConflict   = typeSet.size() > 1;   // 类型能力冲突
            const bool aliasConflict  = aliasSet.size() == 1; // 所有上游输出别名相同（语义歧义）
            if (typeConflict || aliasConflict) {
                Conflict cf;
                cf.kind       = QStringLiteral("multiInputAmbiguity");
                cf.nodeId     = nodeId;
                cf.portName   = inputPort;
                cf.candidates = candidates;
                cf.detail     = typeConflict
                                    ? QStringLiteral("下游输入端口 %1[%2] 被多个上游绑定且类型能力冲突（%3），无法明确取哪个")
                                          .arg(nodeId, inputPort, typeSet.join(QStringLiteral("/")))
                                    : QStringLiteral("下游输入端口 %1[%2] 被多个上游绑定且输出别名相同（%3），产生歧义")
                                          .arg(nodeId, inputPort, aliasSet.first());
                appendConflict(out, cf);
            }
        }
    }

    return out;
}

QVariantList OutputConflictDetector::detectForNode(const QString& nodeId) const {
    QVariantList out;
    const QVariantList all = detectAll();
    for (const QVariant& v : all) {
        if (v.toMap().value("nodeId").toString() == nodeId) out.append(v);
    }
    return out;
}

bool OutputConflictDetector::hasConflicts() const {
    return !detectAll().isEmpty();
}