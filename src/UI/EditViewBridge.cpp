#include "UI/EditViewBridge.h"

#include "UI/OperatorDescriptors.h"
#include "UI/SchemeSerializer.h"
#include "UI/UndoCommands.h"   // v2.1.0 M4.06：UnodCommand 类型（AddNodeCommand 等）
#include "Vision/ToolFactory.h"
#include "Core/VisionTool.h"
#include "Core/Scheme.h"

#include <QUndoStack>
#include <QUuid>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDebug>
#include <QBuffer>
#include <QElapsedTimer>
#include <QDir>
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

EditViewBridge::EditViewBridge(QObject* parent) : QObject(parent) {
    rebuildOperatorTypes();
    m_currentSchemeName = QStringLiteral("未命名方案");

    // v2.1.0 M7：bridge 自管 QUndoStack（之前依赖 EditView 注入，
    // 全 QML 重构后 EditView 不再持有 QUndoStack，bridge 内部创建并 connect 50 步上限）。
    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(50);
    connect(m_undoStack, &QUndoStack::canUndoChanged, this, &EditViewBridge::canUndoChanged);
    connect(m_undoStack, &QUndoStack::canRedoChanged, this, &EditViewBridge::canRedoChanged);

    // v2.1.0 M4：创建异步 I/O 序列化器
    m_serializer = new QDV::UI::SchemeSerializer(this);
    connect(m_serializer, &QDV::UI::SchemeSerializer::saveFinished,
            this, &EditViewBridge::saveFinished);
    connect(m_serializer, &QDV::UI::SchemeSerializer::loadFinished,
            this, [this](const QString& filePath, bool success,
                         const QString& message, const QString& jsonText) {
        if (success) {
            // 解析 JSON → 写回 m_currentNodes/m_connections
            if (applyLoadedJson(jsonText, filePath)) {
                setCurrentSchemeFilePath(filePath);
                m_isDirty = false;
                emit isDirtyChanged();
                // 加载成功后清空 undo 栈（新方案基线）
                if (m_undoStack) m_undoStack->clear();
            } else {
                emit loadFinished(filePath, false,
                                  QStringLiteral("JSON 内容不合法"), QString());
                return;
            }
        }
        emit loadFinished(filePath, success, message, QString());
    });
}

EditViewBridge::~EditViewBridge() = default;

// =====================================================================
// 注入与通知（C++ 端调用 → 触发 QML 更新）
// =====================================================================
void EditViewBridge::setUndoStack(QUndoStack* stack) {
    if (m_undoStack == stack) return;
    // 断开旧栈
    if (m_undoStack) {
        m_undoStack->disconnect(this);
    }
    m_undoStack = stack;
    if (m_undoStack) {
        // v2.1.0 M4.07：撤销/重做上限 50 步
        m_undoStack->setUndoLimit(50);
        connect(m_undoStack, &QUndoStack::canUndoChanged, this, &EditViewBridge::canUndoChanged);
        connect(m_undoStack, &QUndoStack::canRedoChanged, this, &EditViewBridge::canRedoChanged);
    }
    emit canUndoChanged();
    emit canRedoChanged();
}

void EditViewBridge::notifySchemeChanged() {
    rebuildCurrentNodes();
    emit currentNodesChanged();
}

void EditViewBridge::notifySchemeNameChanged(const QString& newName) {
    if (m_currentSchemeName == newName) return;
    m_currentSchemeName = newName;
    emit currentSchemeNameChanged();
}

// =====================================================================
// 重建可用算子列表（来自 ToolFactory）
// =====================================================================
void EditViewBridge::rebuildOperatorTypes() {
    m_operatorTypes = ToolFactory::instance()->getAvailableToolTypes();
    emit operatorsChanged();
}

// =====================================================================
// 重建当前方案节点列表（占位实现；M2 完整版绑定到 Scheme）
// 这里只生成空列表，避免 QML 端空指针
// =====================================================================
void EditViewBridge::rebuildCurrentNodes() {
    m_currentNodes.clear();
    // TODO(M2.10+)：从 Scheme::toolChain() 读取 VisionTool 列表并序列化为 QVariantMap
    // 现在 EditView 未传 Scheme 进来，保持空列表
}

// =====================================================================
// QML 槽函数（v2.1.0 M4：增删改走 UndoCommand 框架）
// =====================================================================
QString EditViewBridge::addOperator(const QString& type, qreal x, qreal y) {
    QDV::VisionTool* tool = ToolFactory::instance()->createTool(type);
    if (!tool) {
        emit errorRaised(QStringLiteral("addOperator"),
                         QStringLiteral("未知算子类型：%1").arg(type));
        return QString();
    }
    const QString nodeId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVariantMap node;
    node["id"]    = nodeId;
    node["type"]  = type;
    node["x"]     = x;
    node["y"]     = y;
    // 用 OperatorMeta 默认值填充节点 params
    const QDV::UI::OperatorMeta meta = QDV::UI::OperatorDescriptors::get(type);
    QVariantMap defaultParams;
    for (const QDV::UI::ParamSpec& p : meta.params) {
        defaultParams.insert(p.name, p.defaultValue);
    }
    node["params"] = defaultParams;
    if (m_undoStack) {
        // M4：通过 UndoCommand 推入（首次 push 自动调 redo）
        m_undoStack->push(new QDV::UI::AddNodeCommand(this, node));
    } else {
        // 无 UndoStack fallback：直接修改（M2 行为）
        m_currentNodes.append(node);
        emit currentNodesChanged();
    }
    markDirty();
    markUsed(type);  // v2.2.0：追踪使用记录
    qDebug() << "[EditViewBridge] addOperator" << type << "at (" << x << "," << y << ") id=" << nodeId;
    delete tool;
    return nodeId;
}

void EditViewBridge::moveNode(const QString& nodeId, qreal newX, qreal newY) {
    // 找旧坐标（UndoCommand 需要 oldX/oldY）
    qreal oldX = 0, oldY = 0;
    bool found = false;
    for (const QVariant& v : m_currentNodes) {
        const QVariantMap n = v.toMap();
        if (n.value("id").toString() == nodeId) {
            oldX = n.value("x").toDouble();
            oldY = n.value("y").toDouble();
            found = true;
            break;
        }
    }
    if (!found) {
        emit errorRaised(QStringLiteral("moveNode"),
                         QStringLiteral("未找到节点：%1").arg(nodeId));
        return;
    }
    if (m_undoStack) {
        m_undoStack->push(new QDV::UI::MoveNodeCommand(this, nodeId, oldX, oldY, newX, newY));
    } else {
        moveNodeInternal(nodeId, newX, newY, true);
    }
    markDirty();
}

void EditViewBridge::connectNodes(const QString& fromId, const QString& fromPort,
                                  const QString& toId,   const QString& toPort) {
    if (fromId.isEmpty() || toId.isEmpty()) {
        emit errorRaised(QStringLiteral("connectNodes"),
                         QStringLiteral("连接端点不能为空"));
        return;
    }
    if (fromId == toId) {
        emit errorRaised(QStringLiteral("connectNodes"),
                         QStringLiteral("不支持节点自连接"));
        return;
    }
    for (const QVariant& v : m_connections) {
        const QVariantMap c = v.toMap();
        if (c.value("fromId").toString() == fromId &&
            c.value("fromPort").toString() == fromPort &&
            c.value("toId").toString() == toId &&
            c.value("toPort").toString() == toPort) {
            return;
        }
    }

    if (m_undoStack) {
        m_undoStack->push(new QDV::UI::ConnectNodesCommand(this, fromId, fromPort, toId, toPort));
    } else {
        addConnectionInternal(fromId, fromPort, toId, toPort, true);
    }
    markDirty();
}

void EditViewBridge::disconnectEdge(const QString& fromId, const QString& fromPort,
                                    const QString& toId,   const QString& toPort) {
    bool exists = false;
    for (const QVariant& v : m_connections) {
        const QVariantMap c = v.toMap();
        if (c.value("fromId").toString() == fromId &&
            c.value("fromPort").toString() == fromPort &&
            c.value("toId").toString() == toId &&
            c.value("toPort").toString() == toPort) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        return;
    }

    if (m_undoStack) {
        m_undoStack->push(new QDV::UI::DisconnectNodesCommand(this, fromId, fromPort, toId, toPort));
    } else {
        removeConnectionInternal(fromId, fromPort, toId, toPort, true);
    }
    markDirty();
}

void EditViewBridge::deleteNode(const QString& nodeId) {
    // 检查节点是否存在
    bool exists = false;
    for (const QVariant& v : m_currentNodes) {
        if (v.toMap().value("id").toString() == nodeId) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        emit errorRaised(QStringLiteral("deleteNode"),
                         QStringLiteral("未找到节点：%1").arg(nodeId));
        return;
    }
    if (m_undoStack) {
        m_undoStack->push(new QDV::UI::RemoveNodeCommand(this, nodeId));
    } else {
        // fallback：直接删除（M2 行为）
        for (int i = m_currentNodes.size() - 1; i >= 0; --i) {
            if (m_currentNodes[i].toMap()["id"].toString() == nodeId) {
                m_currentNodes.removeAt(i);
                break;
            }
        }
        emit currentNodesChanged();
    }
    markDirty();
}

void EditViewBridge::undo() {
    if (m_undoStack) m_undoStack->undo();
}

void EditViewBridge::redo() {
    if (m_undoStack) m_undoStack->redo();
}

void EditViewBridge::selectNode(const QString& nodeId) {
    if (m_selectedNodeId == nodeId) return;
    m_selectedNodeId = nodeId;
    emit nodeSelected(nodeId);
}

void EditViewBridge::openNodeEditor(const QString& nodeId) {
    emit openEditorRequested(nodeId);
}

// =====================================================================
// 状态查询
// =====================================================================
bool EditViewBridge::canUndo() const {
    return m_undoStack && m_undoStack->canUndo();
}

bool EditViewBridge::canRedo() const {
    return m_undoStack && m_undoStack->canRedo();
}

// =====================================================================
// v2.1.0 M3 算子元数据槽（供 QML 动态表单使用）
// =====================================================================
QVariantMap EditViewBridge::getOperatorMeta(const QString& type) const {
    const QDV::UI::OperatorMeta meta = QDV::UI::OperatorDescriptors::get(type);
    return meta.toMap();
}

QVariantList EditViewBridge::operatorCategories() const {
    QVariantList result;
    const QStringList cats = QDV::UI::OperatorDescriptors::categories();
    for (const QString& c : cats) {
        result.append(c);
    }
    return result;
}

QVariantList EditViewBridge::operatorsInCategory(const QString& category) const {
    QVariantList result;
    const QList<QDV::UI::OperatorMeta> list = QDV::UI::OperatorDescriptors::byCategory(category);
    for (const QDV::UI::OperatorMeta& om : list) {
        result.append(om.toMap());
    }
    return result;
}

QVariantMap EditViewBridge::getOperatorParams(const QString& nodeId) const {
    for (const QVariant& v : m_currentNodes) {
        const QVariantMap n = v.toMap();
        if (n.value("id").toString() == nodeId) {
            return n.value("params").toMap();
        }
    }
    return QVariantMap{};  // 节点不存在返回空
}

void EditViewBridge::updateOperatorParams(const QString& nodeId, const QVariantMap& params) {
    int foundIdx = -1;
    for (int i = 0; i < m_currentNodes.size(); ++i) {
        if (m_currentNodes[i].toMap().value("id").toString() == nodeId) {
            foundIdx = i;
            break;
        }
    }
    if (foundIdx < 0) {
        emit errorRaised(QStringLiteral("updateOperatorParams"),
                         QStringLiteral("未找到节点：%1").arg(nodeId));
        return;
    }
    // 校验
    const QVariantMap node = m_currentNodes[foundIdx].toMap();
    const QString type = node.value("type").toString();
    const QVariantMap currentParams = node.value("params").toMap();
    const QDV::UI::OperatorMeta meta = QDV::UI::OperatorDescriptors::get(type);
    // 收集所有需要变更的字段（值确实变化了）
    QList<QPair<QString, QPair<QVariant, QVariant>>> changes;  // {{name, {old, new}}, ...}
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        const QString paramName = it.key();
        const QVariant newVal = it.value();
        const QStringList errs = validateParam(type, paramName, newVal);
        if (!errs.isEmpty()) {
            for (const QString& e : errs) {
                emit errorRaised(QStringLiteral("updateOperatorParams"), e);
            }
            return;  // 校验失败：拒绝整个更新
        }
        const QVariant oldVal = currentParams.value(paramName);
        if (oldVal == newVal) continue;  // 值未变，跳过
        changes.append({paramName, {oldVal, newVal}});
    }
    if (changes.isEmpty()) return;
    if (m_undoStack) {
        // M4：用 macro command 包装多个 PropertyChangeCommand（一次 push 一次 undo）
        auto* macro = new QUndoCommand(
            QStringLiteral("修改 %1 个参数").arg(changes.size()));
        for (const auto& ch : changes) {
            new QDV::UI::PropertyChangeCommand(this, nodeId, ch.first,
                                              ch.second.first, ch.second.second, nullptr, macro);
        }
        m_undoStack->push(macro);
    } else {
        // fallback：直接修改
        for (const auto& ch : changes) {
            updateParamInternal(nodeId, ch.first, ch.second.second, false);
        }
        emit currentNodesChanged();
    }
    markDirty();
}

QStringList EditViewBridge::validateParam(const QString& type, const QString& paramName, const QVariant& value) const {
    QStringList errs;
    const QDV::UI::OperatorMeta meta = QDV::UI::OperatorDescriptors::get(type);
    if (meta.type.isEmpty()) {
        errs.append(QStringLiteral("未知算子类型：%1").arg(type));
        return errs;
    }
    // 查找对应 ParamSpec
    const QDV::UI::ParamSpec* spec = nullptr;
    for (const QDV::UI::ParamSpec& p : meta.params) {
        if (p.name == paramName) { spec = &p; break; }
    }
    if (!spec) {
        errs.append(QStringLiteral("算子 %1 无参数 %2").arg(type, paramName));
        return errs;
    }
    // 类型校验
    if (value.isNull() || !value.isValid()) {
        errs.append(QStringLiteral("参数 %1 不能为空").arg(paramName));
        return errs;
    }
    // 数值型 min/max 校验
    if (spec->type == QDV::UI::ParamType::Int || spec->type == QDV::UI::ParamType::Float) {
        const double v = value.toDouble();
        if (spec->minValue.isValid() && v < spec->minValue.toDouble()) {
            errs.append(QStringLiteral("参数 %1=%2 小于最小值 %3")
                            .arg(paramName).arg(v).arg(spec->minValue.toDouble()));
        }
        if (spec->maxValue.isValid() && v > spec->maxValue.toDouble()) {
            errs.append(QStringLiteral("参数 %1=%2 超过最大值 %3")
                            .arg(paramName).arg(v).arg(spec->maxValue.toDouble()));
        }
    }
    // Enum 校验
    if (spec->type == QDV::UI::ParamType::Enum) {
        const QString s = value.toString();
        if (!spec->optionKeys.isEmpty() && !spec->optionKeys.contains(s)) {
            errs.append(QStringLiteral("参数 %1=%2 不在允许的选项中")
                            .arg(paramName, s));
        }
    }
    return errs;
}

// =====================================================================
// v2.2.0 搜索 + 子分组 + 收藏/最近/常用
// =====================================================================
QVariantList EditViewBridge::searchOperators(const QString& keyword) const {
    QVariantList result;
    if (keyword.trimmed().isEmpty()) {
        // 空关键字 → 返回全部
        const QList<QDV::UI::OperatorMeta> all = QDV::UI::OperatorDescriptors::all();
        for (const QDV::UI::OperatorMeta& om : all) {
            result.append(om.toMap());
        }
        return result;
    }
    const QList<QDV::UI::OperatorMeta> hits = QDV::UI::OperatorDescriptors::search(keyword);
    for (const QDV::UI::OperatorMeta& om : hits) {
        result.append(om.toMap());
    }
    return result;
}

QVariantList EditViewBridge::operatorSubGroups(const QString& category) const {
    QVariantList result;
    const QStringList sgs = QDV::UI::OperatorDescriptors::subGroups(category);
    for (const QString& sg : sgs) {
        result.append(sg);
    }
    return result;
}

bool EditViewBridge::toggleFavorite(const QString& type) {
    if (m_favorites.contains(type)) {
        m_favorites.remove(type);
        emit favoritesChanged();
        return false;
    } else {
        m_favorites.insert(type);
        emit favoritesChanged();
        return true;
    }
}

bool EditViewBridge::isFavorite(const QString& type) const {
    return m_favorites.contains(type);
}

QVariantList EditViewBridge::favorites() const {
    QVariantList result;
    for (const QString& t : m_favorites) {
        QVariantMap m = getOperatorMeta(t);
        if (!m.isEmpty()) result.append(m);
    }
    return result;
}

QVariantList EditViewBridge::recents() const {
    QVariantList result;
    for (const QString& t : m_recents) {
        QVariantMap m = getOperatorMeta(t);
        if (!m.isEmpty()) result.append(m);
    }
    return result;
}

QVariantList EditViewBridge::commons() const {
    // 按使用频次降序排列
    QList<QPair<QString,int>> sorted;
    for (auto it = m_useCounts.constBegin(); it != m_useCounts.constEnd(); ++it) {
        sorted.append({it.key(), it.value()});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const QPair<QString,int>& a, const QPair<QString,int>& b) {
                  return a.second > b.second;
              });
    QVariantList result;
    for (int i = 0; i < qMin(10, sorted.size()); ++i) {
        QVariantMap m = getOperatorMeta(sorted[i].first);
        if (!m.isEmpty()) result.append(m);
    }
    return result;
}

void EditViewBridge::markUsed(const QString& type) {
    // 更新最近使用（去重后放最前）
    m_recents.removeAll(type);
    m_recents.prepend(type);
    while (m_recents.size() > 10) m_recents.removeLast();
    // 更新使用频次
    m_useCounts[type]++;
}

// =====================================================================
// v2.1.0 M4 内部方法（UndoCommand 调用；emitSignals=false 时不发信号）
// =====================================================================
void EditViewBridge::moveNodeInternal(const QString& nodeId, qreal newX, qreal newY, bool emitSignals) {
    for (int i = 0; i < m_currentNodes.size(); ++i) {
        QVariantMap n = m_currentNodes[i].toMap();
        if (n.value("id").toString() == nodeId) {
            n["x"] = newX;
            n["y"] = newY;
            m_currentNodes[i] = n;
            if (emitSignals) emit currentNodesChanged();
            return;
        }
    }
    emit errorRaised(QStringLiteral("moveNodeInternal"),
                     QStringLiteral("未找到节点：%1").arg(nodeId));
}

void EditViewBridge::addConnectionInternal(const QString& fromId, const QString& fromPort,
                                           const QString& toId,   const QString& toPort,
                                           bool emitSignals) {
    for (const QVariant& v : m_connections) {
        const QVariantMap c = v.toMap();
        if (c.value("fromId").toString() == fromId &&
            c.value("fromPort").toString() == fromPort &&
            c.value("toId").toString() == toId &&
            c.value("toPort").toString() == toPort) {
            return;
        }
    }
    QVariantMap conn;
    conn["fromId"]   = fromId;
    conn["fromPort"] = fromPort;
    conn["toId"]     = toId;
    conn["toPort"]   = toPort;
    m_connections.append(conn);
    if (emitSignals) emit connectionsChanged();
}

void EditViewBridge::removeConnectionInternal(const QString& fromId, const QString& fromPort,
                                              const QString& toId,   const QString& toPort,
                                              bool emitSignals) {
    for (int i = m_connections.size() - 1; i >= 0; --i) {
        const QVariantMap c = m_connections[i].toMap();
        if (c.value("fromId").toString() == fromId &&
            c.value("fromPort").toString() == fromPort &&
            c.value("toId").toString() == toId &&
            c.value("toPort").toString() == toPort) {
            m_connections.removeAt(i);
            if (emitSignals) emit connectionsChanged();
            return;
        }
    }
}

void EditViewBridge::updateParamInternal(const QString& nodeId, const QString& paramName,
                                         const QVariant& value, bool emitSignals) {
    for (int i = 0; i < m_currentNodes.size(); ++i) {
        QVariantMap n = m_currentNodes[i].toMap();
        if (n.value("id").toString() == nodeId) {
            QVariantMap params = n.value("params").toMap();
            params[paramName] = value;
            n["params"] = params;
            m_currentNodes[i] = n;
            if (emitSignals) emit currentNodesChanged();
            return;
        }
    }
    emit errorRaised(QStringLiteral("updateParamInternal"),
                     QStringLiteral("未找到节点：%1").arg(nodeId));
}

// =====================================================================
// v2.1.0 M4 I/O 槽（QML 端点击「保存」「加载」调用；异步由 SchemeSerializer 实现）
// =====================================================================
void EditViewBridge::markDirty() {
    if (m_isDirty) return;
    m_isDirty = true;
    emit isDirtyChanged();
}

void EditViewBridge::clearDirty() {
    if (!m_isDirty) return;
    m_isDirty = false;
    emit isDirtyChanged();
}

void EditViewBridge::saveToFile(const QString& filePath) {
    if (!m_serializer) {
        emit saveFinished(filePath, false, QStringLiteral("序列化器未初始化"));
        return;
    }
    if (m_serializer->isBusy()) {
        emit saveFinished(filePath, false, QStringLiteral("请等待上一次 I/O 完成"));
        return;
    }
    m_serializer->saveAsync(filePath, m_currentNodes, m_connections, m_currentSchemeName);
}

void EditViewBridge::loadFromFile(const QString& filePath) {
    if (!m_serializer) {
        emit loadFinished(filePath, false, QStringLiteral("序列化器未初始化"), QString());
        return;
    }
    if (m_serializer->isBusy()) {
        emit loadFinished(filePath, false, QStringLiteral("请等待上一次 I/O 完成"), QString());
        return;
    }
    m_serializer->loadAsync(filePath);
}

void EditViewBridge::newScheme() {
    m_currentNodes.clear();
    m_connections.clear();
    m_currentSchemeName = QStringLiteral("未命名方案");
    m_currentSchemeFilePath.clear();
    m_isDirty = false;
    if (m_undoStack) m_undoStack->clear();
    emit currentNodesChanged();
    emit connectionsChanged();
    emit currentSchemeNameChanged();
    emit currentSchemeFilePathChanged();
    emit isDirtyChanged();
}

// =====================================================================
// v2.1.0 M4 私有：把加载得到的 JSON 应用到 m_currentNodes/m_connections
// =====================================================================
bool EditViewBridge::applyLoadedJson(const QString& jsonText, const QString& filePath) {
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        emit errorRaised(QStringLiteral("loadFromFile"),
                         QStringLiteral("JSON 解析失败：%1").arg(pe.errorString()));
        return false;
    }
    const QJsonObject root = doc.object();
    // 读取方案名
    const QString newName = root.value("schemeName").toString(m_currentSchemeName);
    // 读取节点
    QVariantList newNodes;
    const QJsonArray nodesArr = root.value("nodes").toArray();
    for (const QJsonValue& v : nodesArr) {
        const QJsonObject no = v.toObject();
        QVariantMap node;
        node["id"]   = no.value("id").toString();
        node["type"] = no.value("type").toString();
        node["x"]    = no.value("x").toDouble();
        node["y"]    = no.value("y").toDouble();
        // params 还原
        const QJsonObject paramsObj = no.value("params").toObject();
        QVariantMap params;
        for (auto it = paramsObj.constBegin(); it != paramsObj.constEnd(); ++it) {
            params.insert(it.key(), it.value().toVariant());
        }
        node["params"] = params;
        newNodes.append(node);
    }
    // 读取连接
    QVariantList newConns;
    const QJsonArray connArr = root.value("connections").toArray();
    for (const QJsonValue& v : connArr) {
        const QJsonObject co = v.toObject();
        QVariantMap c;
        c["fromId"]   = co.value("fromId").toString();
        c["fromPort"] = co.value("fromPort").toString();
        c["toId"]     = co.value("toId").toString();
        c["toPort"]   = co.value("toPort").toString();
        newConns.append(c);
    }
    // 提交
    m_currentNodes = newNodes;
    m_connections  = newConns;
    if (newName != m_currentSchemeName) {
        m_currentSchemeName = newName;
        emit currentSchemeNameChanged();
    }
    emit currentNodesChanged();
    emit connectionsChanged();
    qDebug() << "[EditViewBridge] loaded" << newNodes.size() << "nodes, "
             << newConns.size() << "connections from" << QFileInfo(filePath).fileName();
    return true;
}

// =====================================================================
// v3.1.0 图像分析与处理
// =====================================================================

QStringList EditViewBridge::supportedImageFormats() const {
    return {"JPG", "JPEG", "PNG", "BMP", "TIFF", "TIF", "WEBP",
            "PBM", "PGM", "PPM", "SR", "RAS", "EXR", "HDR"};
}

QVariantMap EditViewBridge::analyzeImage(const QString& filePath) {
    QVariantMap result;
    result["ok"] = false;

    QElapsedTimer timer;
    timer.start();

    // 1. 验证文件
    QFileInfo fi(filePath);
    if (!fi.exists()) {
        result["error"] = QString("文件不存在: %1").arg(filePath);
        return result;
    }
    if (!fi.isReadable()) {
        result["error"] = QString("文件不可读: %1").arg(filePath);
        return result;
    }

    // 2. 加载图像
    cv::Mat img = cv::imread(filePath.toStdString(), cv::IMREAD_UNCHANGED);
    if (img.empty()) {
        // 尝试灰度模式加载
        img = cv::imread(filePath.toStdString(), cv::IMREAD_GRAYSCALE);
        if (img.empty()) {
            result["error"] = QString("无法解码图像: %1（格式不支持或文件损坏）").arg(filePath);
            return result;
        }
    }

    qint64 loadMs = timer.elapsed();

    // 3. 基础元数据
    result["ok"] = true;
    result["width"] = img.cols;
    result["height"] = img.rows;
    result["channels"] = img.channels();
    result["format"] = fi.suffix().toUpper();
    result["fileSize"] = fi.size();
    result["loadMs"] = loadMs;
    result["depth"] = img.depth();  // CV_8U=0, CV_8S=1, CV_16U=2, CV_16S=3, CV_32S=4, CV_32F=5, CV_64F=6
    result["totalPixels"] = static_cast<qint64>(img.cols) * img.rows;

    // 4. 统计分析
    QVariantList features;

    // 图像统计（基于一定区域进行，避免大图上全像素统计过慢）
    cv::Mat sample;
    if (img.total() > 2000000) {  // > 2M 像素，降采样
        double scale = std::sqrt(2000000.0 / img.total());
        cv::resize(img, sample, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        sample = img;
    }

    cv::Scalar mean, stddev;
    cv::meanStdDev(sample, mean, stddev);

    // 如果是多通道
    if (sample.channels() >= 3) {
        features.append(QVariantMap{{"label", "平均 R"}, {"value", mean[2]}, {"unit", ""}});
        features.append(QVariantMap{{"label", "平均 G"}, {"value", mean[1]}, {"unit", ""}});
        features.append(QVariantMap{{"label", "平均 B"}, {"value", mean[0]}, {"unit", ""}});
    } else {
        features.append(QVariantMap{{"label", "平均灰度"}, {"value", mean[0]}, {"unit", ""}});
    }
    features.append(QVariantMap{{"label", "标准差"}, {"value", stddev[0]}, {"unit", ""}});

    // 图像类型判断
    QString imgType = "未知";
    if (img.channels() == 1) {
        imgType = "灰度图";
    } else if (img.channels() == 3) {
        imgType = "RGB 彩色图";
    } else if (img.channels() == 4) {
        imgType = "RGBA（含透明通道）";
    }
    result["imageType"] = imgType;

    // 5. 直方图（简化：256 bins）
    QVariantList histogram;
    if (img.channels() == 1) {
        int histSize = 256;
        float range[] = {0, 256};
        const float* histRange = {range};
        cv::Mat hist;
        cv::calcHist(&img, 1, nullptr, cv::Mat(), hist, 1, &histSize, &histRange, true, false);
        for (int i = 0; i < histSize; ++i) {
            histogram.append(QVariantMap{{"bin", i}, {"value", hist.at<float>(i)}});
        }
    } else if (img.channels() >= 3) {
        // 转换为灰度后计算直方图
        cv::Mat gray;
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        int histSize = 256;
        float range[] = {0, 256};
        const float* histRange = {range};
        cv::Mat hist;
        cv::calcHist(&gray, 1, nullptr, cv::Mat(), hist, 1, &histSize, &histRange, true, false);
        for (int i = 0; i < histSize; ++i) {
            histogram.append(QVariantMap{{"bin", i}, {"value", hist.at<float>(i)}});
        }
    }
    result["features"] = features;
    result["histogram"] = histogram;

    // 6. 生成缩略图 (base64)
    cv::Mat thumbnail;
    double thumbScale = std::min(200.0 / img.cols, 200.0 / img.rows);
    if (thumbScale < 1.0) {
        cv::resize(img, thumbnail, cv::Size(), thumbScale, thumbScale, cv::INTER_AREA);
    } else {
        thumbnail = img;
    }
    // 编码为 PNG 后转 base64
    std::vector<uchar> buf;
    cv::imencode(".png", thumbnail, buf);
    QByteArray ba(reinterpret_cast<const char*>(buf.data()), static_cast<int>(buf.size()));
    result["base64Thumbnail"] = QString::fromLatin1(ba.toBase64());

    qint64 totalMs = timer.elapsed();
    result["totalMs"] = totalMs;

    // 7. 性能: 对于 1920x1080 图像，需在 500ms 内完成
    if (img.cols >= 1920 && img.rows >= 1080 && totalMs > 500) {
        qWarning() << "[EditViewBridge::analyzeImage] Performance warning:"
                     << img.cols << "x" << img.rows << "took" << totalMs << "ms (target: 500ms)";
    }

    qDebug() << "[EditViewBridge::analyzeImage]" << fi.fileName()
             << img.cols << "x" << img.rows << "took" << totalMs << "ms";
    return result;
}

QVariantMap EditViewBridge::processImage(const QString& filePath,
                                          const QString& operation,
                                          const QVariantMap& params) {
    QVariantMap result;
    result["ok"] = false;

    QElapsedTimer timer;
    timer.start();

    // 加载图像
    cv::Mat img = cv::imread(filePath.toStdString(), cv::IMREAD_UNCHANGED);
    if (img.empty()) {
        img = cv::imread(filePath.toStdString(), cv::IMREAD_GRAYSCALE);
        if (img.empty()) {
            result["error"] = QString("无法加载图像: %1").arg(filePath);
            return result;
        }
    }

    result["inputWidth"] = img.cols;
    result["inputHeight"] = img.rows;

    cv::Mat processed;

    // 支持的处理操作
    if (operation == "grayscale") {
        // 转灰度
        if (img.channels() >= 3) {
            cv::cvtColor(img, processed, cv::COLOR_BGR2GRAY);
        } else {
            processed = img.clone();
        }

    } else if (operation == "threshold") {
        // 二值化
        double thresh = params.value("thresh", 128.0).toDouble();
        double maxval = params.value("maxval", 255.0).toDouble();
        int threshType = params.value("type", 0).toInt();
        cv::Mat gray;
        if (img.channels() >= 3) cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        else gray = img;
        cv::threshold(gray, processed, thresh, maxval, threshType);

    } else if (operation == "edge_canny") {
        // Canny 边缘检测
        double t1 = params.value("threshold1", 50.0).toDouble();
        double t2 = params.value("threshold2", 150.0).toDouble();
        int aperture = params.value("aperture", 3).toInt();
        cv::Mat gray;
        if (img.channels() >= 3) cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        else gray = img;
        cv::Canny(gray, processed, t1, t2, aperture);

    } else if (operation == "blur_gaussian") {
        // 高斯模糊
        int ksize = params.value("ksize", 5).toInt();
        double sigma = params.value("sigma", 0.0).toDouble();
        // ksize 必须是奇数
        if (ksize % 2 == 0) ksize += 1;
        cv::GaussianBlur(img, processed, cv::Size(ksize, ksize), sigma);

    } else if (operation == "blur_median") {
        // 中值模糊
        int ksize = params.value("ksize", 5).toInt();
        if (ksize % 2 == 0) ksize += 1;
        cv::medianBlur(img, processed, ksize);

    } else if (operation == "resize") {
        // 缩放
        int newW = params.value("width", img.cols / 2).toInt();
        int newH = params.value("height", img.rows / 2).toInt();
        int interp = params.value("interpolation", 1).toInt();
        cv::resize(img, processed, cv::Size(newW, newH), 0, 0, interp);

    } else if (operation == "equalize_hist") {
        // 直方图均衡化
        cv::Mat gray;
        if (img.channels() >= 3) cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        else gray = img;
        cv::equalizeHist(gray, processed);

    } else if (operation == "morph_erode") {
        // 腐蚀
        int ksize = params.value("ksize", 3).toInt();
        int iterations = params.value("iterations", 1).toInt();
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(ksize, ksize));
        cv::erode(img, processed, kernel, cv::Point(-1, -1), iterations);

    } else if (operation == "morph_dilate") {
        // 膨胀
        int ksize = params.value("ksize", 3).toInt();
        int iterations = params.value("iterations", 1).toInt();
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(ksize, ksize));
        cv::dilate(img, processed, kernel, cv::Point(-1, -1), iterations);

    } else {
        result["error"] = QString("不支持的操作: %1").arg(operation);
        return result;
    }

    qint64 procMs = timer.elapsed();

    // 保存处理结果到临时文件
    QFileInfo fi(filePath);
    QString outDir = fi.absolutePath();
    QString outName = fi.completeBaseName() + "_" + operation + ".png";
    QString outPath = QDir(outDir).absoluteFilePath(outName);

    bool saved = cv::imwrite(outPath.toStdString(), processed);
    if (!saved) {
        // 回退到临时目录
        outPath = QDir::temp().absoluteFilePath(outName);
        saved = cv::imwrite(outPath.toStdString(), processed);
    }

    result["ok"] = saved;
    result["outputPath"] = outPath;
    result["width"] = processed.cols;
    result["height"] = processed.rows;
    result["channels"] = processed.channels();
    result["elapsedMs"] = procMs;

    // 生成缩略图 base64
    cv::Mat thumb;
    double ts = std::min(200.0 / processed.cols, 200.0 / processed.rows);
    if (ts < 1.0) cv::resize(processed, thumb, cv::Size(), ts, ts, cv::INTER_AREA);
    else thumb = processed;
    std::vector<uchar> buf;
    cv::imencode(".png", thumb, buf);
    QByteArray ba(reinterpret_cast<const char*>(buf.data()), static_cast<int>(buf.size()));
    result["base64Thumbnail"] = QString::fromLatin1(ba.toBase64());

    qint64 totalMs = timer.elapsed();
    result["totalMs"] = totalMs;

    qDebug() << "[EditViewBridge::processImage]" << operation << fi.fileName()
             << "->" << processed.cols << "x" << processed.rows
             << "took" << totalMs << "ms";

    return result;
}
