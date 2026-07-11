#include "UI/EditViewBridge.h"

#include "UI/OperatorDescriptors.h"
#include "UI/SchemeSerializer.h"
#include "UI/UndoCommands.h"   // v2.1.0 M4.06：UnodCommand 类型（AddNodeCommand 等）
#include "UI/PreviewManager.h"          // v2.6.0 预览管理
#include "Vision/ToolFactory.h"
#include "Vision/ToolChainExecutor.h"   // v2.5.0 功能 3/5：部署与单算子运行
#include "Core/VisionTool.h"
#include "Core/Scheme.h"
#include "Core/Logger.h"
#include "Core/VariableManager.h"       // v2.6.0 控制变量管理
#include "Core/ImageVariableManager.h"  // v2.6.0 图像变量管理

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
#include <QFile>
#include <QByteArray>
#include <QtConcurrent>   // 异步部署执行
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

// v2.5.0 修复：健壮的图像加载（QFile 读取字节流 + cv::imdecode 解码）
// 替代 cv::imread，解决 Windows 上中文/特殊字符路径加载失败问题
// cv::imread 内部使用 C 标准 fopen，对 UTF-8 路径在 GBK 系统下会失败
// QFile 使用 Qt 的文件系统抽象，正确处理 Unicode 路径
static cv::Mat loadImageRobust(const QString& filePath, int flags = cv::IMREAD_COLOR) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return cv::Mat();
    }
    const QByteArray data = file.readAll();
    file.close();
    if (data.isEmpty()) {
        return cv::Mat();
    }
    // cv::imdecode 从内存字节流解码图像，绕过路径编码问题
    return cv::imdecode(cv::Mat(1, data.size(), CV_8UC1,
                                const_cast<char*>(data.constData())), flags);
}

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
    // P1-A4 修复：保存成功后清零 isDirty（之前仅转发信号，脏标记不清零导致用户误以为未保存）
    connect(m_serializer, &QDV::UI::SchemeSerializer::saveFinished,
            this, [this](const QString& filePath, bool success, const QString& message) {
        if (success) {
            clearDirty();
            setCurrentSchemeFilePath(filePath);
        }
        emit saveFinished(filePath, success, message);
    });
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

    // === v2.6.0 变量管理与预览管理 ===
    // 实例化三个管理器（this 作为 parent，生命周期跟随 bridge）
    m_variableManager = new QDV::VariableManager(this);
    m_imageVariableManager = new QDV::ImageVariableManager(this);
    m_previewManager = new QDV::PreviewManager(this);

    // 依赖注入：PreviewManager 需要 bridge / ImageVariableManager / VariableManager
    m_previewManager->setBridge(this);
    m_previewManager->setImageVariableManager(m_imageVariableManager);
    m_previewManager->setVariableManager(m_variableManager);

    // 信号连接：变量值变更 → PreviewManager 触发预览
    connect(m_variableManager, &QDV::VariableManager::valueChanged,
            m_previewManager, &QDV::PreviewManager::onVariableChanged);
    // 信号连接：变量整体变更（清空/批量加载）→ PreviewManager 触发预览
    connect(m_variableManager, &QDV::VariableManager::variablesChanged,
            m_previewManager, [this]() { m_previewManager->requestPreview(); });
    // 信号连接：节点选中 → PreviewManager 跟随选中节点
    connect(this, &EditViewBridge::nodeSelected,
            m_previewManager, &QDV::PreviewManager::onNodeSelected);
    // 信号连接：连线变更 → PreviewManager 触发预览
    connect(this, &EditViewBridge::connectionsChanged,
            m_previewManager, &QDV::PreviewManager::onConnectionsChanged);
    // v2.6.0 信号连接：方案加载完成 → 接收控制变量 JSON → 写入 VariableManager
    connect(m_serializer, &QDV::UI::SchemeSerializer::variablesLoaded,
            this, [this](const QString& varsJson) {
        if (m_variableManager) {
            loadVariablesFromJson(varsJson);
        }
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

// P1-B03 修复：拖拽中实时移动节点（不推 undo 栈），仅更新 m_currentNodes 触发连线重绘
// v2.5.0 修复：不 emit currentNodesChanged（会导致 Repeater 重建 delegate 中断拖拽），
// 改 emit nodePositionChanged 信号，QML 端 connectionsCanvas 监听此信号重绘连线
void EditViewBridge::moveNodeLive(const QString& nodeId, qreal newX, qreal newY) {
    moveNodeInternal(nodeId, newX, newY, false);  // false = 不 emit currentNodesChanged
    emit nodePositionChanged(nodeId, newX, newY);  // 专门信号，只触发连线重绘
}

// P1-B03 修复：提交节点移动到 undo 栈（onReleased 时调用）
// originX/originY 由 QML 端 onPressed 记录，避免 moveNodeLive 已改 m_currentNodes 导致 oldX 错误
void EditViewBridge::commitNodeMove(const QString& nodeId,
                                    qreal originX, qreal originY,
                                    qreal newX, qreal newY) {
    if (m_undoStack) {
        m_undoStack->push(new QDV::UI::MoveNodeCommand(this, nodeId, originX, originY, newX, newY));
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
        QDV::Logger::warn(QStringLiteral("disconnectEdge: connection not found (%1:%2 -> %3:%4)")
                          .arg(fromId).arg(fromPort).arg(toId).arg(toPort));
        return;
    }

    QDV::Logger::info(QStringLiteral("disconnectEdge: removing connection %1:%2 -> %3:%4 (nodes remain untouched)")
                      .arg(fromId).arg(fromPort).arg(toId).arg(toPort));

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

    // v2.6.0：节点删除后通知 PreviewManager 清理对应图像变量
    if (m_previewManager) {
        m_previewManager->onNodeDeleted(nodeId);
    }
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
    // v5.3.5 调试日志：排查 ComboBox 闪回
    QDV::Logger::info(QString("updateOperatorParams: nodeId=%1 paramsCount=%2")
                     .arg(nodeId).arg(params.size()));
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        QDV::Logger::info(QString("  param %1 = %2 (type=%3)")
                         .arg(it.key()).arg(it.value().toString())
                         .arg(it.value().typeName()));
    }
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
        const QVariant oldVal = currentParams.value(paramName);
        // P0 修复：先判断值是否变化，未变化直接跳过，避免对未修改的旧值（如历史 int 型 colorMode）
        // 进行类型校验导致整个更新被静默拒绝
        if (oldVal == newVal) continue;  // 值未变，跳过
        const QStringList errs = validateParam(type, paramName, newVal);
        if (!errs.isEmpty()) {
            for (const QString& e : errs) {
                emit errorRaised(QStringLiteral("updateOperatorParams"), e);
            }
            return;  // 校验失败：拒绝整个更新
        }
        changes.append({paramName, {oldVal, newVal}});
    }
    if (changes.isEmpty()) {
        QDV::Logger::info("updateOperatorParams: no changes detected, returning");
        return;
    }
    // v5.3.7 诊断日志：打印 changes 列表
    for (const auto& ch : changes) {
        QDV::Logger::info(QString("  change: %1 old=%2 new=%3")
                         .arg(ch.first)
                         .arg(ch.second.first.toString())
                         .arg(ch.second.second.toString()));
    }
    if (m_undoStack) {
        // P1-A14 修复：单参数修改直接 push PropertyChangeCommand（利用 mergeWith 自动合并），
        // 避免滑块拖动产生大量 macro 填满 50 步 undo 栈。
        // 多参数修改仍用 macro 包装（一次 push 一次 undo）。
        if (changes.size() == 1) {
            const auto& ch = changes.first();
            m_undoStack->push(new QDV::UI::PropertyChangeCommand(
                this, nodeId, ch.first, ch.second.first, ch.second.second));
        } else {
            auto* macro = new QUndoCommand(
                QStringLiteral("修改 %1 个参数").arg(changes.size()));
            for (const auto& ch : changes) {
                new QDV::UI::PropertyChangeCommand(this, nodeId, ch.first,
                                                  ch.second.first, ch.second.second, nullptr, macro);
            }
            m_undoStack->push(macro);
        }
    } else {
        // fallback：直接修改
        QDV::Logger::info("updateOperatorParams: using fallback (no undoStack)");
        for (const auto& ch : changes) {
            updateParamInternal(nodeId, ch.first, ch.second.second, false);
        }
        emit currentNodesChanged();
    }
    // v5.3.7 诊断日志：确认参数已写入
    {
        const QVariantMap after = m_currentNodes[foundIdx].toMap().value("params").toMap();
        for (const auto& ch : changes) {
            QDV::Logger::info(QString("  after: %1 = %2")
                             .arg(ch.first)
                             .arg(after.value(ch.first).toString()));
        }
    }
    markDirty();

    // v2.6.0：参数变更后通知 PreviewManager 触发实时预览（300ms 防抖）
    if (m_previewManager) {
        m_previewManager->onNodeParamsChanged(nodeId);
    }
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
            // P0 修复：向后兼容历史保存的方案（int 型枚举索引）
            // 若 value 是 int 且 0 <= int < optionKeys.size()，视为合法索引
            bool ok = false;
            int idx = value.toInt(&ok);
            if (!(ok && idx >= 0 && idx < spec->optionKeys.size())) {
                errs.append(QStringLiteral("参数 %1=%2 不在允许的选项中")
                                .arg(paramName, s));
            }
        }
    }
    // P1-B4-H2 修复：Vector 类型校验（如 trueBranch/falseBranch 工具 ID 列表）
    // 之前 Vector 类型零校验，任意垃圾输入均通过，导致 BranchNode::deserialize 收到字符串
    // 返回空数组 → 分支节点 invalid → 方案数据损坏
    if (spec->type == QDV::UI::ParamType::Vector) {
        QStringList items;
        if (value.typeId() == QMetaType::QStringList) {
            items = value.toStringList();
        } else if (value.typeId() == QMetaType::QVariantList) {
            const QVariantList vl = value.toList();
            for (const QVariant& v : vl) items.append(v.toString());
        } else {
            // 字符串按逗号/换行拆分（兼容历史方案）
            const QString s = value.toString();
            const QStringList parts = s.split(QRegularExpression(QStringLiteral("[,，\n]")),
                                              Qt::SkipEmptyParts);
            for (const QString& p : parts) items.append(p.trimmed());
        }
        // 校验：每个元素非空
        for (const QString& item : items) {
            if (item.isEmpty()) {
                errs.append(QStringLiteral("参数 %1 包含空元素").arg(paramName));
                break;
            }
        }
    }

    // P1-B4-H4 修复：ROI 类型校验
    // ROI 格式："x,y,w,h"（4 个整数，逗号/分号/空格分隔）
    // 校验：4 个字段均可解析为整数；w/h >= 0（允许 0 表示空 ROI）
    if (spec->type == QDV::UI::ParamType::ROI) {
        const QString s = value.toString().trimmed();
        // 允许空字符串（表示未设置 ROI，由算子自行处理）
        if (!s.isEmpty()) {
            const QStringList parts = s.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                              Qt::SkipEmptyParts);
            if (parts.size() != 4) {
                errs.append(QStringLiteral("参数 %1 需为 x,y,w,h 格式（4 个整数），实际 %2 个字段")
                               .arg(paramName).arg(parts.size()));
            } else {
                bool okX = false, okY = false, okW = false, okH = false;
                const int rx = parts[0].toInt(&okX);
                const int ry = parts[1].toInt(&okY);
                const int rw = parts[2].toInt(&okW);
                const int rh = parts[3].toInt(&okH);
                if (!okX || !okY || !okW || !okH) {
                    errs.append(QStringLiteral("参数 %1 包含非整数字段：%2")
                                   .arg(paramName).arg(s));
                } else if (rw < 0 || rh < 0) {
                    errs.append(QStringLiteral("参数 %1 的宽高不能为负数（w=%2, h=%3）")
                                   .arg(paramName).arg(rw).arg(rh));
                }
                // 注：x/y 允许负数（某些坐标系原点不在左上角）
                Q_UNUSED(rx); Q_UNUSED(ry);
            }
        }
    }
    return errs;
}

// =====================================================================
// P1-B4-H6 复制粘贴参数
// =====================================================================
bool EditViewBridge::copyNodeParams(const QString& nodeId) {
    // 查找源节点
    for (const QVariant& v : m_currentNodes) {
        const QVariantMap n = v.toMap();
        if (n.value("id").toString() == nodeId) {
            m_clipType = n.value("type").toString();
            m_clipParams = n.value("params").toMap();
            m_clipHasData = true;
            qDebug() << "[EditViewBridge] copyNodeParams from" << m_clipType
                     << "params count=" << m_clipParams.size();
            return true;
        }
    }
    emit errorRaised(QStringLiteral("copyNodeParams"),
                     QStringLiteral("未找到节点：%1").arg(nodeId));
    return false;
}

bool EditViewBridge::pasteNodeParams(const QString& nodeId) {
    if (!m_clipHasData) {
        emit errorRaised(QStringLiteral("pasteNodeParams"),
                         QStringLiteral("剪贴板为空，请先复制参数"));
        return false;
    }
    // 查找目标节点
    int targetIdx = -1;
    QVariantMap targetNode;
    for (int i = 0; i < m_currentNodes.size(); ++i) {
        const QVariantMap n = m_currentNodes[i].toMap();
        if (n.value("id").toString() == nodeId) {
            targetIdx = i;
            targetNode = n;
            break;
        }
    }
    if (targetIdx < 0) {
        emit errorRaised(QStringLiteral("pasteNodeParams"),
                         QStringLiteral("未找到目标节点：%1").arg(nodeId));
        return false;
    }
    const QString targetType = targetNode.value("type").toString();
    // 类型不同时仍允许粘贴，但只粘贴参数名匹配的字段（更宽松的策略）
    // 这样改名同义的参数（如 hMin/rMin）也能手工迁移，避免一刀切阻断

    // 取目标算子的 ParamSpec 列表，用于校验每个参数
    const QDV::UI::OperatorMeta targetMeta = QDV::UI::OperatorDescriptors::get(targetType);
    QHash<QString, const QDV::UI::ParamSpec*> targetSpecMap;
    for (const QDV::UI::ParamSpec& p : targetMeta.params) {
        targetSpecMap.insert(p.name, &p);
    }

    QVariantMap mergedParams = targetNode.value("params").toMap();
    int appliedCount = 0;
    QStringList skippedNames;
    for (auto it = m_clipParams.constBegin(); it != m_clipParams.constEnd(); ++it) {
        const QString& paramName = it.key();
        // 仅粘贴目标节点已有的参数（避免引入幽灵参数）
        if (!targetSpecMap.contains(paramName)) {
            skippedNames.append(paramName);
            continue;
        }
        // 类型校验：根据 ParamSpec.type 检查值是否可转换
        const QDV::UI::ParamSpec* spec = targetSpecMap.value(paramName);
        const QVariant& clipVal = it.value();
        QVariant converted = clipVal;
        bool ok = true;
        switch (spec->type) {
            case QDV::UI::ParamType::Int:
                converted = QVariant(clipVal.toInt(&ok));
                break;
            case QDV::UI::ParamType::Float:
                converted = QVariant(clipVal.toDouble(&ok));
                break;
            case QDV::UI::ParamType::Bool:
                converted = QVariant(clipVal.toBool());
                break;
            case QDV::UI::ParamType::Enum:
                // Enum 接受 string 或 int，校验是否在 optionKeys 中
                if (spec->optionKeys.contains(clipVal.toString())) {
                    converted = clipVal;
                } else {
                    // 尝试 int 索引（历史方案可能存的是索引而非 key）
                    bool intOk = false;
                    int idx = clipVal.toInt(&intOk);
                    if (intOk && idx >= 0 && idx < spec->optionKeys.size()) {
                        converted = spec->optionKeys.at(idx);
                    } else {
                        ok = false;
                    }
                }
                break;
            case QDV::UI::ParamType::String:
            case QDV::UI::ParamType::ROI:
            case QDV::UI::ParamType::Vector:
                // 字符串/ROI/Vector 直接接受（C++ validateParam 会再校验）
                converted = clipVal;
                break;
        }
        if (!ok) {
            skippedNames.append(paramName);
            continue;
        }
        // C++ 端校验
        const QStringList errs = validateParam(targetType, paramName, converted);
        if (!errs.isEmpty()) {
            skippedNames.append(paramName);
            continue;
        }
        mergedParams.insert(paramName, converted);
        ++appliedCount;
    }

    if (appliedCount == 0) {
        emit errorRaised(QStringLiteral("pasteNodeParams"),
                         QStringLiteral("无可粘贴参数（类型=%1，跳过：%2）")
                             .arg(m_clipType, skippedNames.join(", ")));
        return false;
    }

    // 通过 updateOperatorParams 写入（走 UndoCommand 框架）
    updateOperatorParams(nodeId, mergedParams);

    qDebug() << "[EditViewBridge] pasteNodeParams to" << targetType
             << "applied=" << appliedCount
             << "skipped=" << skippedNames;
    if (!skippedNames.isEmpty()) {
        emit errorRaised(QStringLiteral("pasteNodeParams"),
                         QStringLiteral("已粘贴 %1 个参数，跳过 %2 个不匹配项：%3")
                             .arg(QString::number(appliedCount),
                                  QString::number(skippedNames.size()),
                                  skippedNames.join(", ")));
    }
    return true;
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
    // P1-B9 修复：校验 type 已在 OperatorDescriptors 注册（幽灵算子不可收藏）
    if (getOperatorMeta(type).isEmpty()) {
        emit errorRaised(QStringLiteral("toggleFavorite"),
                         QStringLiteral("未知算子类型：%1").arg(type));
        return false;
    }
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
            QDV::Logger::info(QStringLiteral("removeConnectionInternal: connection removed (%1:%2 -> %3:%4)")
                              .arg(fromId).arg(fromPort).arg(toId).arg(toPort));
            if (emitSignals) emit connectionsChanged();
            return;
        }
    }
    QDV::Logger::warn(QStringLiteral("removeConnectionInternal: connection not found (%1:%2 -> %3:%4)")
                      .arg(fromId).arg(fromPort).arg(toId).arg(toPort));
}

void EditViewBridge::updateParamInternal(const QString& nodeId, const QString& paramName,
                                         const QVariant& value, bool emitSignals) {
    // v5.3.5 调试日志
    QDV::Logger::info(QString("updateParamInternal: nodeId=%1 param=%2 value=%3 emitSignals=%4")
                     .arg(nodeId).arg(paramName).arg(value.toString()).arg(emitSignals));
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
    QDV::Logger::info(QString("EditViewBridge::saveToFile: path=%1 nodes=%2 conns=%3")
                          .arg(filePath).arg(m_currentNodes.size()).arg(m_connections.size()));
    if (!m_serializer) {
        QDV::Logger::error("EditViewBridge::saveToFile: 序列化器未初始化");
        emit saveFinished(filePath, false, QStringLiteral("序列化器未初始化"));
        return;
    }
    if (m_serializer->isBusy()) {
        QDV::Logger::warn("EditViewBridge::saveToFile: 序列化器忙");
        emit saveFinished(filePath, false, QStringLiteral("请等待上一次 I/O 完成"));
        return;
    }
    // v2.6.0：保存前注入控制变量 JSON
    if (m_variableManager) {
        m_serializer->setVariablesJson(variablesToJson());
    }
    m_serializer->saveAsync(filePath, m_currentNodes, m_connections, m_currentSchemeName);
}

void EditViewBridge::loadFromFile(const QString& filePath) {
    QDV::Logger::info(QString("EditViewBridge::loadFromFile: path=%1").arg(filePath));
    if (!m_serializer) {
        QDV::Logger::error("EditViewBridge::loadFromFile: 序列化器未初始化");
        emit loadFinished(filePath, false, QStringLiteral("序列化器未初始化"), QString());
        return;
    }
    if (m_serializer->isBusy()) {
        QDV::Logger::warn("EditViewBridge::loadFromFile: 序列化器忙");
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
    // v2.6.0：新建方案时清空所有变量
    if (m_variableManager) m_variableManager->clear();
    if (m_imageVariableManager) m_imageVariableManager->clear();
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

// =====================================================================
// v2.5.0 功能 3/5：部署与单算子运行
// =====================================================================

void EditViewBridge::setCameraFrame(const QString& tempFilePath) {
    m_cameraFramePath = tempFilePath;
    QDV::Logger::info(QString("EditViewBridge::setCameraFrame: %1").arg(tempFilePath));
}

// =====================================================================
// v2.6.0 变量管理与预览管理 API 实现
// =====================================================================
QObject* EditViewBridge::variableManager() const {
    return m_variableManager;
}

QObject* EditViewBridge::imageVariableManager() const {
    return m_imageVariableManager;
}

QObject* EditViewBridge::previewManager() const {
    return m_previewManager;
}

QString EditViewBridge::variablesToJson() const {
    if (!m_variableManager) return QStringLiteral("[]");
    const QJsonArray arr = m_variableManager->toJson();
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

bool EditViewBridge::loadVariablesFromJson(const QString& jsonStr) {
    if (!m_variableManager) return false;
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isArray()) {
        emit errorRaised(QStringLiteral("loadVariablesFromJson"),
                         QStringLiteral("变量 JSON 格式无效：%1").arg(parseErr.errorString()));
        return false;
    }
    return m_variableManager->fromJson(doc.array());
}

QString EditViewBridge::saveMatToTempPng(const cv::Mat& image, const QString& prefix) {
    if (image.empty()) return QString();
    // 生成临时文件路径
    const QString tempDir = QDir::tempPath();
    const QString fileName = QString("%1_%2.png").arg(prefix).arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    const QString filePath = QDir(tempDir).absoluteFilePath(fileName);

    // v5.2 修复：cv::imwrite 内部用 C 标准 fopen，在 Windows GBK 系统下对 UTF-8 中文路径
    // （如中文用户名的 temp 目录）会失败。改用 QFile + cv::imencode，与 ReadImageTool 的
    // 加载逻辑（QFile + cv::imdecode）对称，彻底解决中文路径问题。
    std::vector<uchar> buf;
    if (!cv::imencode(".png", image, buf)) {
        QDV::Logger::error("EditViewBridge::saveMatToTempPng: cv::imencode failed");
        return QString();
    }
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) {
        QDV::Logger::error(QString("EditViewBridge::saveMatToTempPng: cannot open %1 for writing").arg(filePath));
        return QString();
    }
    const qint64 written = f.write(reinterpret_cast<const char*>(buf.data()), static_cast<qint64>(buf.size()));
    f.close();
    if (written != static_cast<qint64>(buf.size())) {
        QDV::Logger::error(QString("EditViewBridge::saveMatToTempPng: short write to %1").arg(filePath));
        return QString();
    }
    return filePath;
}

QList<QDV::VisionTool*> EditViewBridge::buildToolChainFromNodes(const QStringList& nodeIds) const {
    QList<QDV::VisionTool*> tools;
    tools.reserve(nodeIds.size());

    for (const QString& nodeId : nodeIds) {
        // 查找节点
        QVariantMap nodeData;
        bool found = false;
        for (const QVariant& v : m_currentNodes) {
            const QVariantMap n = v.toMap();
            if (n.value("id").toString() == nodeId) {
                nodeData = n;
                found = true;
                break;
            }
        }
        if (!found) {
            QDV::Logger::warn(QString("buildToolChainFromNodes: 节点 %1 未找到，跳过").arg(nodeId));
            continue;
        }

        const QString type = nodeData.value("type").toString();
        QDV::VisionTool* tool = ToolFactory::instance()->createTool(type);
        if (!tool) {
            QDV::Logger::error(QString("buildToolChainFromNodes: 无法创建算子 %1").arg(type));
            // 清理已创建的 tool
            qDeleteAll(tools);
            return {};
        }

        // 设置参数（QVariantMap → QJsonObject → configure）
        const QVariantMap params = nodeData.value("params").toMap();
        // v2.6.0：解析参数中的 ${varName} 变量绑定（递归处理 Map/List/String）
        // 若未定义变量则保留原值（运行时算子可能容忍或报错）
        QVariantMap resolvedParams = params;
        if (m_variableManager) {
            resolvedParams.clear();
            for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
                resolvedParams[it.key()] = m_variableManager->resolveVariant(it.value());
            }
        }
        QJsonObject jsonParams;
        for (auto it = resolvedParams.constBegin(); it != resolvedParams.constEnd(); ++it) {
            jsonParams[it.key()] = QJsonValue::fromVariant(it.value());
        }
        if (!tool->configure(jsonParams)) {
            // v2.5.0 修复：configure 失败时阻断（避免用默认参数"假执行"掩盖错误）
            QDV::Logger::error(QString("buildToolChainFromNodes: 算子 %1 参数配置失败，已跳过").arg(type));
            delete tool;
            qDeleteAll(tools);
            return {};
        }

        // 设置 tool id 为节点 id（供 ToolChainExecutor 结果映射）
        // P0 修复：之前调用 deserialize({id,name,type}) 会覆盖 configure 已设置的参数，
        // 因为部分子类 deserialize 未使用 contains 检查，缺失字段被覆盖为 0/空。
        // 现改用 setId/setName 直接设置，避免任何副作用。
        tool->setId(nodeId);
        tool->setName(type);

        tools.append(tool);
    }
    return tools;
}

void EditViewBridge::collectUpstreamNodes(const QString& nodeId, QStringList& result, QSet<QString>& visited) const {
    if (visited.contains(nodeId)) return;
    visited.insert(nodeId);

    // 查找所有直接上游节点（connections 中 toId == nodeId 的 fromId）
    for (const QVariant& v : m_connections) {
        const QVariantMap conn = v.toMap();
        if (conn.value("toId").toString() == nodeId) {
            const QString upstreamId = conn.value("fromId").toString();
            collectUpstreamNodes(upstreamId, result, visited);  // DFS 递归上游
        }
    }

    // 所有上游处理完后，将当前节点加入结果（拓扑序：上游在前，当前在后）
    result.append(nodeId);
}

QStringList EditViewBridge::computeUpstreamChain(const QString& targetNodeId) const {
    QStringList result;
    QSet<QString> visited;
    collectUpstreamNodes(targetNodeId, result, visited);
    return result;
}

// v2.5.0 修复：智能解析节点的输入图像路径
// 扫描目标节点的上游链（含自身），查找 ReadImage 节点的 filePath 参数
// 解决问题：用户在前道 ReadImage 配置了图像后，运行后续算子不需要重复选择图像
QString EditViewBridge::resolveInputImageForNode(const QString& nodeId) const {
    // 计算上游链（含目标节点本身）
    const QStringList chain = computeUpstreamChain(nodeId);
    if (chain.isEmpty()) return QString();

    // 遍历链中所有节点，查找 ReadImage 类型且 filePath 非空
    for (const QString& id : chain) {
        for (const QVariant& v : m_currentNodes) {
            const QVariantMap n = v.toMap();
            if (n.value("id").toString() != id) continue;
            if (n.value("type").toString() != QStringLiteral("ReadImage")) continue;

            // 提取 ReadImage 节点的 filePath 参数
            const QVariantMap params = n.value("params").toMap();
            const QString filePath = params.value("filePath").toString();
            if (!filePath.isEmpty() && QFile::exists(filePath)) {
                QDV::Logger::info(QString("resolveInputImageForNode: 命中 ReadImage 节点 %1，filePath=%2")
                                  .arg(id).arg(filePath));
                return filePath;
            }
        }
    }
    return QString();
}

// v5.3.8 修复：统一判断节点是否需要输入图像文件
// 数据源型算子自身不消费输入图像（或仅作为数据源），运行时无需弹窗选择图像
bool EditViewBridge::isInputImageRequired(const QString& nodeId) const {
    QString nodeType;
    for (const QVariant& v : m_currentNodes) {
        const QVariantMap n = v.toMap();
        if (n.value("id").toString() == nodeId) {
            nodeType = n.value("type").toString();
            break;
        }
    }
    if (nodeType.isEmpty()) {
        // 节点不存在时保守返回 true，避免误跳过必要输入
        return true;
    }
    static const QSet<QString> kNoImageRequiredTypes = {
        QStringLiteral("OpenFramegrabber"),  // 打开相机，输出句柄
        QStringLiteral("GrabImage"),          // 从相机抓帧
        QStringLiteral("RobotPose"),          // 机器人位姿数据源
        QStringLiteral("HandEyeCalib"),       // 手眼标定（文件模式自带路径）
        QStringLiteral("ReadImage")           // 读图算子自身提供图像
    };
    return !kNoImageRequiredTypes.contains(nodeType);
}

// v5.3.8 修复：统一解析运行输入源
// 将“是否需要输入图像”和“是否能自动找到输入路径”合并为一个接口，
// 供 QML 的运行按钮、右键菜单统一调用，避免多处硬编码算子类型。
QVariantMap EditViewBridge::resolveRunInput(const QString& nodeId) const {
    QVariantMap result;
    const bool required = isInputImageRequired(nodeId);
    result["required"] = required;
    if (required) {
        const QString path = resolveInputImageForNode(nodeId);
        result["path"] = path;
        result["hint"] = path.isEmpty()
            ? QStringLiteral("上游无 ReadImage 节点或未配置图像，请手动选择输入源")
            : QStringLiteral("已自动解析上游 ReadImage 图像");
    } else {
        result["path"] = QString();
        result["hint"] = QStringLiteral("当前算子为数据源型算子，无需输入图像");
    }
    return result;
}

// v5.3.9 修复：解析整链运行输入源
// 供主工具栏“运行当前方案”使用：若方案含 ReadImage（且 filePath 有效）或相机类算子，
// 则无需弹窗选择输入图像。
QVariantMap EditViewBridge::resolveSchemeRunInput() const {
    QVariantMap result;
    result["path"] = QString();

    // v5.3.9 诊断日志：帮助定位运行时仍弹选择输入图像对话框的问题
    QStringList nodeTypes;
    for (const QVariant& v : m_currentNodes) {
        nodeTypes.append(v.toMap().value("type").toString());
    }
    QDV::Logger::info(QStringLiteral("[resolveSchemeRunInput] nodes=%1").arg(nodeTypes.join(", ")));

    // 1) 优先查找 ReadImage 节点并返回其 filePath
    for (const QVariant& v : m_currentNodes) {
        const QVariantMap n = v.toMap();
        if (n.value("type").toString() != QStringLiteral("ReadImage")) continue;
        const QVariantMap params = n.value("params").toMap();
        const QString fp = params.value("filePath").toString();
        QDV::Logger::info(QStringLiteral("[resolveSchemeRunInput] ReadImage filePath=%1 exists=%2").arg(fp).arg(QFile::exists(fp)));
        if (!fp.isEmpty() && QFile::exists(fp)) {
            result["required"] = true;
            result["path"] = fp;
            result["hint"] = QStringLiteral("已自动解析 ReadImage 节点图像");
            QDV::Logger::info(QStringLiteral("[resolveSchemeRunInput] result: required=true path=%1").arg(fp));
            return result;
        }
    }

    // 2) 若方案包含相机类算子，则无需输入图像（由相机提供）
    static const QSet<QString> kCameraSourceTypes = {
        QStringLiteral("OpenFramegrabber"),
        QStringLiteral("GrabImage")
    };
    for (const QVariant& v : m_currentNodes) {
        const QString type = v.toMap().value("type").toString();
        QDV::Logger::info(QStringLiteral("[resolveSchemeRunInput] checking type=%1").arg(type));
        if (kCameraSourceTypes.contains(type)) {
            result["required"] = false;
            result["hint"] = QStringLiteral("方案包含相机数据源，无需选择输入图像");
            QDV::Logger::info(QStringLiteral("[resolveSchemeRunInput] result: required=false (camera source)"));
            return result;
        }
    }

    // 3) 其余情况需要用户选择
    result["required"] = true;
    result["hint"] = QStringLiteral("当前方案无 ReadImage/相机数据源，请手动选择输入图像");
    QDV::Logger::info(QStringLiteral("[resolveSchemeRunInput] result: required=true (manual)"));
    return result;
}

QVariantMap EditViewBridge::runScheme(const QString& inputImagePath) {
    QVariantMap result;
    result["success"] = false;

    // 确定输入图像路径（v2.5.0 修复：优先级 入参 > 链中 ReadImage > 相机帧）
    QString imagePath = inputImagePath;
    if (imagePath.isEmpty()) {
        // 自动从当前方案节点中查找 ReadImage 的 filePath
        for (const QVariant& v : m_currentNodes) {
            const QVariantMap n = v.toMap();
            if (n.value("type").toString() != QStringLiteral("ReadImage")) continue;
            const QVariantMap params = n.value("params").toMap();
            const QString fp = params.value("filePath").toString();
            if (!fp.isEmpty() && QFile::exists(fp)) {
                imagePath = fp;
                break;
            }
        }
    }
    if (imagePath.isEmpty()) {
        imagePath = m_cameraFramePath;
    }

    cv::Mat inputImage;
    if (imagePath.isEmpty()) {
        // v5.3.9 修复：若方案包含相机类算子（OpenFramegrabber/GrabImage），
        // 它们自身提供图像输入，无需用户选择文件。使用 1x1 占位图像跳过空输入校验。
        bool hasCameraSource = false;
        for (const QVariant& v : m_currentNodes) {
            const QString type = v.toMap().value("type").toString();
            if (type == QStringLiteral("OpenFramegrabber") ||
                type == QStringLiteral("GrabImage")) {
                hasCameraSource = true;
                break;
            }
        }
        if (hasCameraSource) {
            inputImage = cv::Mat(1, 1, CV_8UC3, cv::Scalar(0, 0, 0));
        } else {
            result["error"] = QStringLiteral("未指定输入图像路径");
            emit errorRaised(QStringLiteral("runScheme"), QStringLiteral("未指定输入图像路径"));
            return result;
        }
    } else {
        // 验证文件存在
        QFileInfo fi(imagePath);
        if (!fi.exists()) {
            result["error"] = QStringLiteral("输入图像文件不存在：%1").arg(imagePath);
            emit errorRaised(QStringLiteral("runScheme"), result["error"].toString());
            return result;
        }

        // 加载输入图像（v2.5.0 修复：用 loadImageRobust 替代 cv::imread，兼容中文路径）
        inputImage = loadImageRobust(imagePath, cv::IMREAD_COLOR);
        if (inputImage.empty()) {
            result["error"] = QStringLiteral("无法加载图像：%1").arg(imagePath);
            emit errorRaised(QStringLiteral("runScheme"), result["error"].toString());
            return result;
        }
    }

    // 构建工具链（按 m_currentNodes 顺序）
    QStringList nodeIds;
    for (const QVariant& v : m_currentNodes) {
        const QString id = v.toMap().value("id").toString();
        if (!id.isEmpty()) nodeIds.append(id);
    }
    if (nodeIds.isEmpty()) {
        result["error"] = QStringLiteral("当前方案无算子节点");
        emit errorRaised(QStringLiteral("runScheme"), result["error"].toString());
        return result;
    }

    QList<QDV::VisionTool*> tools = buildToolChainFromNodes(nodeIds);
    if (tools.isEmpty()) {
        result["error"] = QStringLiteral("构建工具链失败");
        emit errorRaised(QStringLiteral("runScheme"), result["error"].toString());
        return result;
    }

    // 执行
    QElapsedTimer timer;
    timer.start();

    ::ToolChainExecutor executor;
    executor.setTools(tools);

    // 收集每个算子的执行结果
    QVariantList toolResults;
    int successCount = 0;
    int failCount = 0;
    QString lastOutputPath;

    // 连接信号收集结果
    QObject::connect(&executor, &::ToolChainExecutor::toolExecuted,
        [this, &toolResults, &successCount, &lastOutputPath](const QString& toolId, const ::ToolResult& tr) {
            QVariantMap trMap;
            trMap["toolId"] = toolId;
            trMap["ok"] = tr.ok;
            trMap["elapsedMs"] = tr.elapsedMs;
            if (!tr.overlayImage.empty()) {
                const QString path = saveMatToTempPng(tr.overlayImage, "qdv_deploy");
                trMap["outputImagePath"] = path;
                if (!path.isEmpty()) lastOutputPath = path;
                // v2.6.0：将输出图像写入 ImageVariableManager（按节点 ID 索引）
                if (m_imageVariableManager && !path.isEmpty()) {
                    QString toolName = toolId;
                    for (const QVariant& v : m_currentNodes) {
                        const QVariantMap n = v.toMap();
                        if (n.value("id").toString() == toolId) {
                            toolName = n.value("type").toString();
                            break;
                        }
                    }
                    m_imageVariableManager->updateImageVariable(
                        toolId, toolName, path,
                        tr.overlayImage.cols, tr.overlayImage.rows, tr.overlayImage.channels());
                }
            }
            toolResults.append(trMap);
            if (tr.ok) successCount++;
        });
    QObject::connect(&executor, &::ToolChainExecutor::toolFailed,
        [&toolResults, &failCount](const QString& toolId, const QString& errMsg) {
            QVariantMap trMap;
            trMap["toolId"] = toolId;
            trMap["ok"] = false;
            trMap["errorMessage"] = errMsg;
            toolResults.append(trMap);
            failCount++;
        });

    const bool ok = executor.execute(inputImage);
    const qint64 elapsedMs = timer.elapsed();

    // 清理 tool 实例（调用方拥有所有权）
    qDeleteAll(tools);

    result["success"] = ok;
    result["totalTools"] = nodeIds.size();
    result["successCount"] = successCount;
    result["failCount"] = failCount;
    result["elapsedMs"] = elapsedMs;
    result["outputImagePath"] = lastOutputPath;
    result["toolResults"] = toolResults;

    QDV::Logger::info(QString("runScheme: %1/%2 success, %3ms")
                          .arg(successCount).arg(nodeIds.size()).arg(elapsedMs));
    return result;
}

void EditViewBridge::runSchemeAsync(const QString& inputImagePath) {
    // 修复"点选无响应"：原 runDeploy 在主线程同步调用 runScheme，整链执行期间 UI 完全冻结。
    // 现将执行移到子线程，主线程保持响应；结果通过 schemeDeployFinished 信号回传。
    // 重入保护：若上一次部署仍在执行，忽略新请求（避免并发执行导致状态混乱）
    if (m_schemeRunning) {
        QDV::Logger::warn("runSchemeAsync: 上一次部署仍在执行，忽略新请求");
        return;
    }
    m_schemeRunning = true;

    if (!m_schemeWatcher) {
        m_schemeWatcher = new QFutureWatcher<QVariantMap>(this);
        connect(m_schemeWatcher, &QFutureWatcher<QVariantMap>::finished, this, [this]() {
            const QVariantMap result = m_schemeWatcher->result();
            m_schemeRunning = false;
            emit schemeDeployFinished(result);
        });
    }

    emit schemeDeployStarted();
    // 注意：runScheme 内部读取 m_currentNodes（只读快照），与 PreviewManager 的异步模式一致；
    // 其发出的 errorRaised 信号跨线程自动走 QueuedConnection，安全。
    const QString path = inputImagePath;
    m_schemeWatcher->setFuture(QtConcurrent::run([this, path]() -> QVariantMap {
        return this->runScheme(path);
    }));
}

void EditViewBridge::runSingleOperatorAsync(const QString& nodeId, const QString& inputImagePath) {
    // 交互式调参专用异步执行：拖动滑块/修改下拉时调用，避免阻塞 UI 主线程。
    // 重入保护：若上一次执行仍在进行，忽略新请求（防抖由 QML 端 Timer 负责）
    if (m_singleOpRunning) {
        return;
    }
    m_singleOpRunning = true;

    if (!m_singleOpWatcher) {
        m_singleOpWatcher = new QFutureWatcher<QVariantMap>(this);
        connect(m_singleOpWatcher, &QFutureWatcher<QVariantMap>::finished, this, [this]() {
            const QVariantMap result = m_singleOpWatcher->result();
            m_singleOpRunning = false;
            emit singleOperatorFinished(result);
        });
    }

    emit singleOperatorStarted();
    const QString nid = nodeId;
    const QString imgPath = inputImagePath;
    m_singleOpWatcher->setFuture(QtConcurrent::run([this, nid, imgPath]() -> QVariantMap {
        return this->runSingleOperator(nid, imgPath);
    }));
}

QVariantMap EditViewBridge::runSingleOperator(const QString& nodeId, const QString& inputImagePath) {
    QVariantMap result;
    result["success"] = false;

    // v5.3.8：先确定目标节点类型，用于后续判断是否为数据源型算子
    static const QSet<QString> kNoImageRequiredTypes = {
        QStringLiteral("OpenFramegrabber"),  // 打开相机，输出句柄
        QStringLiteral("GrabImage"),          // 从相机抓帧
        QStringLiteral("RobotPose"),          // 机器人位姿数据源
        QStringLiteral("HandEyeCalib"),       // 手眼标定（文件模式自带路径）
        QStringLiteral("ReadImage")           // 读图算子自身提供图像
    };
    QString targetNodeType;
    for (const QVariant& v : m_currentNodes) {
        const QVariantMap n = v.toMap();
        if (n.value("id").toString() == nodeId) {
            targetNodeType = n.value("type").toString();
            break;
        }
    }
    const bool isNoImageRequired = kNoImageRequiredTypes.contains(targetNodeType);

    // 确定输入图像路径（v2.5.0 修复：优先级 入参 > 链中 ReadImage > 相机帧）
    // 解决问题：用户在前道 ReadImage 配置了图像后，运行后续算子不需要重复选择图像
    QString imagePath = inputImagePath;
    if (imagePath.isEmpty()) {
        // 自动从上游链（含目标节点）查找 ReadImage 节点的 filePath
        imagePath = resolveInputImageForNode(nodeId);
    }
    if (imagePath.isEmpty()) {
        imagePath = m_cameraFramePath;
    }

    cv::Mat inputImage;
    if (imagePath.isEmpty()) {
        if (isNoImageRequired) {
            // v5.3.8 修复：数据源型算子（相机类/读图类）自身不消费输入图像，
            // 但 ToolChainExecutor 会拒绝空输入。这里使用 1x1 占位图像跳过校验，
            // 实际工具执行时不依赖该图像（OpenFramegrabber 用已注册相机句柄，
            // GrabImage 用 acqHandle，ReadImage 用自身 filePath）。
            inputImage = cv::Mat(1, 1, CV_8UC3, cv::Scalar(0, 0, 0));
        } else {
            result["error"] = QStringLiteral("未指定输入图像路径");
            emit errorRaised(QStringLiteral("runSingleOperator"), QStringLiteral("未指定输入图像路径"));
            return result;
        }
    } else {
        // 验证文件存在
        QFileInfo fi(imagePath);
        if (!fi.exists()) {
            result["error"] = QStringLiteral("输入图像文件不存在：%1").arg(imagePath);
            emit errorRaised(QStringLiteral("runSingleOperator"), result["error"].toString());
            return result;
        }

        // 加载输入图像（v2.5.0 修复：用 loadImageRobust 替代 cv::imread，兼容中文路径）
        inputImage = loadImageRobust(imagePath, cv::IMREAD_COLOR);
        if (inputImage.empty()) {
            result["error"] = QStringLiteral("无法加载图像：%1").arg(imagePath);
            emit errorRaised(QStringLiteral("runSingleOperator"), result["error"].toString());
            return result;
        }
    }

    // 计算上游链（拓扑有序，含目标节点本身）
    const QStringList chain = computeUpstreamChain(nodeId);
    if (chain.isEmpty()) {
        result["error"] = QStringLiteral("未找到节点：%1").arg(nodeId);
        emit errorRaised(QStringLiteral("runSingleOperator"), result["error"].toString());
        return result;
    }

    const int upstreamCount = chain.size() - 1;  // 减去目标节点本身

    // 构建工具链
    QList<QDV::VisionTool*> tools = buildToolChainFromNodes(chain);
    if (tools.isEmpty()) {
        result["error"] = QStringLiteral("构建工具链失败");
        emit errorRaised(QStringLiteral("runSingleOperator"), result["error"].toString());
        return result;
    }

    // 执行
    QElapsedTimer timer;
    timer.start();

    ::ToolChainExecutor executor;
    executor.setTools(tools);

    QVariantList upstreamResults;
    QVariantMap targetResult;
    QString lastOutputPath;

    // 目标节点是 chain 的最后一个
    const QString targetToolId = chain.last();

    QObject::connect(&executor, &::ToolChainExecutor::toolExecuted,
        [this, &chain, &upstreamResults, &targetResult, &lastOutputPath, targetToolId]
        (const QString& toolId, const ::ToolResult& tr) {
            QVariantMap trMap;
            trMap["toolId"] = toolId;
            trMap["ok"] = tr.ok;
            trMap["elapsedMs"] = tr.elapsedMs;
            if (!tr.overlayImage.empty()) {
                const QString path = saveMatToTempPng(tr.overlayImage, "qdv_single");
                trMap["outputImagePath"] = path;
                if (!path.isEmpty()) lastOutputPath = path;
                // v2.6.0：将输出图像写入 ImageVariableManager（按节点 ID 索引）
                if (m_imageVariableManager && !path.isEmpty()) {
                    // 查找节点类型（chain 中节点 ID 对应的 type）
                    QString toolName = toolId;
                    for (const QVariant& v : m_currentNodes) {
                        const QVariantMap n = v.toMap();
                        if (n.value("id").toString() == toolId) {
                            toolName = n.value("type").toString();
                            break;
                        }
                    }
                    m_imageVariableManager->updateImageVariable(
                        toolId, toolName, path,
                        tr.overlayImage.cols, tr.overlayImage.rows, tr.overlayImage.channels());
                }
            }
            if (toolId == targetToolId) {
                targetResult = trMap;
            } else {
                upstreamResults.append(trMap);
            }
        });
    QObject::connect(&executor, &::ToolChainExecutor::toolFailed,
        [&upstreamResults, &targetResult, targetToolId](const QString& toolId, const QString& errMsg) {
            QVariantMap trMap;
            trMap["toolId"] = toolId;
            trMap["ok"] = false;
            trMap["errorMessage"] = errMsg;
            if (toolId == targetToolId) {
                targetResult = trMap;
            } else {
                upstreamResults.append(trMap);
            }
        });

    const bool ok = executor.execute(inputImage);
    const qint64 elapsedMs = timer.elapsed();

    // 清理
    qDeleteAll(tools);

    result["success"] = ok;
    result["upstreamCount"] = upstreamCount;
    result["elapsedMs"] = elapsedMs;
    result["outputImagePath"] = lastOutputPath;
    result["upstreamResults"] = upstreamResults;
    result["targetResult"] = targetResult;

    QDV::Logger::info(QString("runSingleOperator: node=%1, upstream=%2, %3ms")
                          .arg(nodeId).arg(upstreamCount).arg(elapsedMs));
    return result;
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
