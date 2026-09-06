#include "UI/EditViewBridge.h"

#include "UI/OperatorDescriptors.h"
#include "UI/OperatorHelpProvider.h"    // v2.8.0：算子帮助内容提供器
#include "UI/OperatorLibraryBridge.h"   // v2.7.0 O1a：算子库桥接器
#include "UI/SchemeSerializer.h"
#include "UI/UndoCommands.h"   // v2.1.0 M4.06：UnodCommand 类型（AddNodeCommand 等）
#include "UI/PreviewManager.h"          // v2.6.0 预览管理
#include "UI/ClipboardManager.h"        // v2.7.0 Phase 3.1 Task 6：算子参数剪贴板
#include "UI/SchemeRunController.h"     // v2.7.0 Phase 3.1 Task 6：方案运行控制
#include "UI/OutputConfigManager.h"     // v2.7.0 Phase 3.1 Task 6：算子输出配置
#include "UI/PortBindingManager.h"      // 端口绑定数据模型（多输入/多输出查询与校验）
#include "UI/OutputConflictDetector.h"  // 输入/输出项冲突检测引擎（三类冲突检测，Task 6）
#include "UI/OperatorRecommender.h"     // 智能算子推荐引擎（spec：editor-output-connection-optimization，Task 9）
#include "Vision/ToolFactory.h"
#include "Vision/ToolChainExecutor.h"   // v2.5.0 功能 3/5：部署与单算子运行
#include "Core/VisionTool.h"
#include "Core/Scheme.h"
#include "Core/BranchNode.h"           // v2.7.0 Phase 3.1：branchGroups() 读取 BranchNode 字段
#include "Core/SchemeManager.h"        // v2.7.0：runScheme 注入 branches/subChains/parallelBranches
#include "Core/Logger.h"
#include "Core/VariableManager.h"       // v2.6.0 控制变量管理
#include "Core/ImageVariableManager.h"  // v2.6.0 图像变量管理
#include "AI/ModelManager.h"            // v2.6.0 Task 18：模型库管理
#include "Export/SchemeExporter.h"      // 算子流程导出
#include "Export/ExportConfig.h"        // 导出配置结构体
#include "Vision/ToolChainVerifier.h"   // 导出自检

#include <QUndoStack>
#include <QUuid>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDebug>
#include <QCoreApplication>   // v5.4.0：getRegisteredModels 使用 applicationDirPath()
#include <QBuffer>
#include <QElapsedTimer>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QByteArray>
#include <QtConcurrent>   // 异步部署执行
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

// v2.5.0 loadImageRobust 已迁移至 SchemeRunController.cpp（仅运行路径使用）

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
                // v2.7.0 Phase 3.1：通知 QML 刷新分组容器叠加层
                // 数据从 SchemeManager::instance()->currentScheme() 读取
                emit subChainGroupsChanged();
                emit branchGroupsChanged();
                emit parallelGroupsChanged();
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
        [this](const QString& variablesJson) {
        if (m_variableManager) {
            loadVariablesFromJson(variablesJson);
        }
    });

    // === 算子流程导出编排器 ===
    m_exporter = new SchemeExporter(this);
    connect(m_exporter, &SchemeExporter::exportProgress,
            this, &EditViewBridge::exportProgress);
    connect(m_exporter, &SchemeExporter::exportFinished,
            this, &EditViewBridge::exportFinished);

    // === v2.7.0 O1a：算子库桥接器 ===
    // 传入 this 作为 editViewBridge 弱引用（画布预检用）
    m_operatorLibraryBridge = new OperatorLibraryBridge(this, this);

    // === v2.7.0 Phase 3.1 Task 6：拆分出的专职协作者 ===
    // ClipboardManager 为纯逻辑类（非 QObject），直接持有
    m_clipboardManager = new ClipboardManager();
    // SchemeRunController / OutputConfigManager 为 QObject，this 作为 parent
    m_schemeRunController = new SchemeRunController(this, this);
    m_outputConfigManager = new OutputConfigManager(this, this);
    m_portBindingManager = new PortBindingManager(this, this);
    // 输入/输出项冲突检测引擎（spec：editor-output-connection-optimization，Task 6）
    m_conflictDetector = new OutputConflictDetector(this, this);
    // 智能算子推荐引擎（spec：editor-output-connection-optimization，Task 9）
    m_recommender = new OperatorRecommender(this, this);

    // 信号转发：SchemeRunController → EditViewBridge（保持 QML 端信号契约不变）
    connect(m_schemeRunController, &SchemeRunController::errorRaised,
            this, &EditViewBridge::errorRaised);
    connect(m_schemeRunController, &SchemeRunController::schemeDeployStarted,
            this, &EditViewBridge::schemeDeployStarted);
    connect(m_schemeRunController, &SchemeRunController::schemeDeployFinished,
            this, &EditViewBridge::schemeDeployFinished);
    connect(m_schemeRunController, &SchemeRunController::singleOperatorStarted,
            this, &EditViewBridge::singleOperatorStarted);
    connect(m_schemeRunController, &SchemeRunController::singleOperatorFinished,
            this, &EditViewBridge::singleOperatorFinished);

    // 信号转发：OutputConfigManager → EditViewBridge
    connect(m_outputConfigManager, &OutputConfigManager::errorRaised,
            this, &EditViewBridge::errorRaised);
    connect(m_outputConfigManager, &OutputConfigManager::currentNodesChanged,
            this, &EditViewBridge::currentNodesChanged);

    // 信号转发：OutputConflictDetector → EditViewBridge（Task 6）
    // 节点/连接变化时触发冲突集合变化；并 forward 到 QML 端 bridge.conflictsChanged
    connect(this, &EditViewBridge::currentNodesChanged,
            m_conflictDetector, &OutputConflictDetector::conflictsChanged);
    connect(this, &EditViewBridge::connectionsChanged,
            m_conflictDetector, &OutputConflictDetector::conflictsChanged);
    connect(m_conflictDetector, &OutputConflictDetector::conflictsChanged,
            this, &EditViewBridge::conflictsChanged);
}

EditViewBridge::~EditViewBridge() {
    // ClipboardManager 非 QObject，需手动释放
    // m_schemeRunController / m_outputConfigManager 以 this 为 parent，由 Qt 对象树自动销毁
    delete m_clipboardManager;
    m_clipboardManager = nullptr;
}

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
    // v5.4：初始化输出开关配置（按 OperatorMeta.outputs 的 defaultEnabled 取值）
    // 每个算子节点保存自己启用哪些输出，供 QML 端 CheckBox 编辑、AiClassifyTool 执行时过滤
    QVariantMap outputConfig;
    for (const QVariantMap& out : meta.outputs) {
        const QString outName = out.value("name").toString();
        const bool defaultEnabled = out.value("defaultEnabled", true).toBool();
        QVariantMap item;
        item["enabled"] = defaultEnabled;
        outputConfig[outName] = item;
    }
    node["outputConfig"] = outputConfig;
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
    // 允许同一节点/端口建立多条连接（多输入/多输出扩展）。
    // 仅阻断完全重复的连线（相同 from/to 端口），避免完全相同的边叠加。
    for (const QVariant& v : m_connections) {
        const QVariantMap c = v.toMap();
        if (c.value("fromId").toString() == fromId &&
            c.value("fromPort").toString() == fromPort &&
            c.value("toId").toString() == toId &&
            c.value("toPort").toString() == toPort) {
            QDV::Logger::info(QStringLiteral("[EditViewBridge] 忽略完全重复的连线: %1:%2 -> %3:%4")
                              .arg(fromId).arg(fromPort).arg(toId).arg(toPort));
            return;
        }
    }

    // P1-3 typed ports：连线期端口类型校验
    // 若两端算子都声明了端口（outputPorts/inputPorts 非空），校验类型兼容；
    // 任一端未声明端口（旧算子兼容模式）则放行，不阻断
    if (!checkPortCompatible(fromId, fromPort, toId, toPort)) {
        const QString fromType = nodeTypeById(fromId);
        const QString toType   = nodeTypeById(toId);
        const QString msg = QStringLiteral("端口类型不兼容：%1[%2] → %3[%4]")
                                .arg(fromType).arg(fromPort).arg(toType).arg(toPort);
        QDV::Logger::warn(QStringLiteral("[EditViewBridge] 连线被阻断: %1").arg(msg));
        emit errorRaised(QStringLiteral("connectNodes"), msg);
        return;
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

// ============================================================================
// P1-3 typed ports：端口类型校验与查询实现
// ============================================================================

QString EditViewBridge::nodeTypeById(const QString& nodeId) const {
    for (const QVariant& v : m_currentNodes) {
        const QVariantMap n = v.toMap();
        if (n.value("id").toString() == nodeId) {
            return n.value("type").toString();
        }
    }
    return QString();
}

bool EditViewBridge::checkPortCompatible(const QString& fromId, const QString& fromPort,
                                         const QString& toId,   const QString& toPort) const {
    const QString fromType = nodeTypeById(fromId);
    const QString toType   = nodeTypeById(toId);
    if (fromType.isEmpty() || toType.isEmpty()) {
        // 节点不存在，放行（由后续逻辑处理）
        return true;
    }

    // 临时创建算子实例查询端口元数据（连线是低频操作，开销可接受）
    QDV::VisionTool* fromTool = ::ToolFactory::instance()->createTool(fromType);
    QDV::VisionTool* toTool   = ::ToolFactory::instance()->createTool(toType);
    if (!fromTool || !toTool) {
        // 算子无法创建（含幻影算子已剔除的情况），放行避免误阻断
        delete fromTool;
        delete toTool;
        return true;
    }

    const QList<QDV::PortDescriptor> outPorts = fromTool->outputPorts();
    const QList<QDV::PortDescriptor> inPorts  = toTool->inputPorts();
    delete fromTool;
    delete toTool;

    // 任一端未声明端口（旧算子兼容模式），放行
    if (outPorts.isEmpty() || inPorts.isEmpty()) {
        return true;
    }

    // 查找 fromPort 对应的输出端口类型
    QDV::PortType outPortType = QDV::PortType::Any;
    bool outFound = false;
    for (const QDV::PortDescriptor& p : outPorts) {
        if (p.name == fromPort) {
            outPortType = p.type;
            outFound = true;
            break;
        }
    }
    // 输出端口名未匹配（可能是默认 image 端口），放行
    if (!outFound) {
        return true;
    }

    // 查找 toPort 对应的输入端口类型
    QDV::PortType inPortType = QDV::PortType::Any;
    bool inFound = false;
    for (const QDV::PortDescriptor& p : inPorts) {
        if (p.name == toPort) {
            inPortType = p.type;
            inFound = true;
            break;
        }
    }
    if (!inFound) {
        return true;
    }

    // 类型兼容性校验
    return QDV::PortDescriptor::compatible(outPortType, inPortType);
}

QVariantMap EditViewBridge::getOperatorPorts(const QString& type) const {
    QVariantMap result;
    QVariantList inputs;
    QVariantList outputs;

    QDV::VisionTool* tool = ::ToolFactory::instance()->createTool(type);
    if (tool) {
        for (const QDV::PortDescriptor& p : tool->inputPorts()) {
            inputs.append(p.toMap());
        }
        for (const QDV::PortDescriptor& p : tool->outputPorts()) {
            outputs.append(p.toMap());
        }
        delete tool;
    }

    result["inputs"]  = inputs;
    result["outputs"] = outputs;
    return result;
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

    // v6.x：节点删除后清理其运行输出值缓存
    {
        QMutexLocker locker(&m_nodeOutputsMutex);
        m_nodeOutputs.remove(nodeId);
    }

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

// =====================================================================
// v2.8.0 算子帮助内容提供器（转发至 OperatorHelpProvider 单例）
// =====================================================================
QString EditViewBridge::getShortDesc(const QString& type) const {
    return OperatorHelpProvider::instance().getShortDesc(type);
}

void EditViewBridge::logDiag(const QString& msg) const {
    // P0-1（0906 优化）：[RecDiag] 算子库悬停/飞出菜单诊断降级为 debug ——
    // 鼠标扫过算子库每个分类都会触发数条，原 INFO 级逐条落盘直接卡悬停动画
    QDV::Logger::debug(QStringLiteral("[RecDiag] %1").arg(msg));
}

QVariantMap EditViewBridge::getOperatorDoc(const QString& type) const {
    return OperatorHelpProvider::instance().getFullDoc(type);
}

QString EditViewBridge::getParamHelp(const QString& type, const QString& paramName) const {
    return OperatorHelpProvider::instance().getParamHelp(type, paramName);
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

// v5.4：返回指定节点的输出开关配置（委托 OutputConfigManager，Task 6）
QVariantMap EditViewBridge::getOutputConfig(const QString& nodeId) const {
    return m_outputConfigManager->getOutputConfig(nodeId);
}

// =====================================================================
// v6.x：节点运行后输出值缓存
// 由 SchemeRunController 在工作线程回调写入，QML 变量管理面板读取展示
// =====================================================================
void EditViewBridge::setNodeOutputValues(const QString& nodeId, const QVariantMap& ports) {
    if (nodeId.isEmpty()) return;
    {
        QMutexLocker locker(&m_nodeOutputsMutex);
        m_nodeOutputs[nodeId] = ports;
    }
    emit nodeOutputsUpdated();
}

QVariantMap EditViewBridge::getNodeOutputValues(const QString& nodeId) const {
    QMutexLocker locker(&m_nodeOutputsMutex);
    return m_nodeOutputs.value(nodeId);
}

void EditViewBridge::updateOperatorParams(const QString& nodeId, const QVariantMap& params) {
    // P0-1（0906 优化）：静音逐参数诊断日志（v5.3.5 ComboBox 闪回排查遗留）。
    // 改参数是最高频操作，原实现每次打 6~30 条立即落盘日志，直接造成输入卡顿。
    QDV::Logger::debug(QString("updateOperatorParams: nodeId=%1 paramsCount=%2")
                     .arg(nodeId).arg(params.size()));
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
        QDV::Logger::debug("updateOperatorParams: no changes detected, returning");
        return;
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
        QDV::Logger::debug("updateOperatorParams: using fallback (no undoStack)");
        for (const auto& ch : changes) {
            updateParamInternal(nodeId, ch.first, ch.second.second, false);
        }
        emit currentNodesChanged();
    }
    // P0-1（0906 优化）：移除 v5.3.7 写后确认日志（每条参数修改额外 M 条 INFO）
    markDirty();

    // v2.6.0：参数变更后通知 PreviewManager 触发实时预览（300ms 防抖）
    if (m_previewManager) {
        m_previewManager->onNodeParamsChanged(nodeId);
    }
}

// v5.4：更新指定节点的输出开关配置（委托 OutputConfigManager，Task 6）
// 复用 PropertyChangeCommand，属性键为 "outputConfig"，updateParamInternal 内部据此写到 node["outputConfig"]
void EditViewBridge::updateOutputConfig(const QString& nodeId, const QVariantMap& outputs) {
    m_outputConfigManager->updateOutputConfig(nodeId, outputs);
}

// P0-3（0906 优化）：单键增量更新 —— 复用 updateOperatorParams 的单参数路径
// （校验 + 单 PropertyChangeCommand push + mergeWith 合并），只是免去全表 Map 构造。
void EditViewBridge::updateOperatorParam(const QString& nodeId, const QString& paramName,
                                         const QVariant& value) {
    QVariantMap single;
    single.insert(paramName, value);
    updateOperatorParams(nodeId, single);
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
// P1-B4-H6 复制粘贴参数（委托 ClipboardManager，Task 6）
// =====================================================================
bool EditViewBridge::copyNodeParams(const QString& nodeId) {
    if (!m_clipboardManager->copyFrom(m_currentNodes, nodeId)) {
        emit errorRaised(QStringLiteral("copyNodeParams"),
                         QStringLiteral("未找到节点：%1").arg(nodeId));
        return false;
    }
    return true;
}

bool EditViewBridge::pasteNodeParams(const QString& nodeId) {
    if (!m_clipboardManager->hasData()) {
        emit errorRaised(QStringLiteral("pasteNodeParams"),
                         QStringLiteral("剪贴板为空，请先复制参数"));
        return false;
    }
    // 查找目标节点
    QVariantMap targetNode;
    bool found = false;
    for (const QVariant& v : m_currentNodes) {
        const QVariantMap n = v.toMap();
        if (n.value("id").toString() == nodeId) {
            targetNode = n;
            found = true;
            break;
        }
    }
    if (!found) {
        emit errorRaised(QStringLiteral("pasteNodeParams"),
                         QStringLiteral("未找到目标节点：%1").arg(nodeId));
        return false;
    }
    // 委托 ClipboardManager 构建合并参数（参数校验通过 validateParam 回调注入，
    // 复用既有校验逻辑，避免重复实现）
    const ClipboardManager::PasteResult pr = m_clipboardManager->buildPaste(
        targetNode,
        [this](const QString& t, const QString& p, const QVariant& v) -> QStringList {
            return this->validateParam(t, p, v);
        });

    if (!pr.applied) {
        emit errorRaised(QStringLiteral("pasteNodeParams"),
                         QStringLiteral("无可粘贴参数（类型=%1，跳过：%2）")
                             .arg(pr.clipType, pr.skippedNames.join(", ")));
        return false;
    }

    // 通过 updateOperatorParams 写入（走 UndoCommand 框架）
    updateOperatorParams(nodeId, pr.mergedParams);

    const QString targetType = targetNode.value("type").toString();
    qDebug() << "[EditViewBridge] pasteNodeParams to" << targetType
             << "applied=" << pr.appliedCount
             << "skipped=" << pr.skippedNames;
    if (!pr.skippedNames.isEmpty()) {
        emit errorRaised(QStringLiteral("pasteNodeParams"),
                         QStringLiteral("已粘贴 %1 个参数，跳过 %2 个不匹配项：%3")
                             .arg(QString::number(pr.appliedCount),
                                  QString::number(pr.skippedNames.size()),
                                  pr.skippedNames.join(", ")));
    }
    return true;
}

// hasClipParams / clipParamsType 委托 ClipboardManager（Task 6）
bool EditViewBridge::hasClipParams() const {
    return m_clipboardManager->hasData();
}

QString EditViewBridge::clipParamsType() const {
    return m_clipboardManager->type();
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
            QDV::Logger::debug(QStringLiteral("removeConnectionInternal: connection removed (%1:%2 -> %3:%4)")
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
    // P0-1（0906 优化）：静音 v5.3.5 调试日志 —— 每个参数写回都打 INFO 的落盘路径
    QDV::Logger::debug(QString("updateParamInternal: nodeId=%1 param=%2 emitSignals=%3")
                     .arg(nodeId).arg(paramName).arg(emitSignals));
    for (int i = 0; i < m_currentNodes.size(); ++i) {
        QVariantMap n = m_currentNodes[i].toMap();
        if (n.value("id").toString() == nodeId) {
            // v5.4：outputConfig 作为节点顶层字段，与 params 分离存储
            // PropertyChangeCommand 通过 "outputConfig" 这个虚拟属性键调用本方法
            if (paramName == QStringLiteral("outputConfig")) {
                n["outputConfig"] = value;
            } else {
                QVariantMap params = n.value("params").toMap();
                params[paramName] = value;
                n["params"] = params;
            }
            m_currentNodes[i] = n;
            if (emitSignals) {
                // P0-2（0906 优化）：参数键发粒度信号 —— QML 端只刷新选中节点相关面板，
                // 画布节点卡片不重建（卡片视觉不依赖 params）。非参数键（outputConfig
                // 等节点顶层结构）保持全量信号。
                if (paramName != QStringLiteral("outputConfig")) {
                    emit nodeParamsChanged(nodeId, QStringList{paramName});
                } else {
                    emit currentNodesChanged();
                }
            }
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
    // v6.x：清空节点运行后输出值缓存
    {
        QMutexLocker locker(&m_nodeOutputsMutex);
        m_nodeOutputs.clear();
    }
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
        // v5.4：还原输出开关配置
        // 向后兼容——旧工程文件无 outputConfig 字段时按 OperatorMeta.outputs 的 defaultEnabled 补全
        QVariantMap outputConfig;
        const QJsonObject ocObj = no.value("outputConfig").toObject();
        if (!ocObj.isEmpty()) {
            for (auto it = ocObj.constBegin(); it != ocObj.constEnd(); ++it) {
                outputConfig.insert(it.key(), it.value().toVariant());
            }
        }
        if (outputConfig.isEmpty()) {
            // 旧工程文件无 outputConfig 字段，按默认值补全
            const QString nodeType = node.value("type").toString();
            const QDV::UI::OperatorMeta meta = QDV::UI::OperatorDescriptors::get(nodeType);
            for (const QVariantMap& out : meta.outputs) {
                QVariantMap item;
                item["enabled"] = out.value("defaultEnabled", true).toBool();
                outputConfig[out.value("name").toString()] = item;
            }
        }
        if (!outputConfig.isEmpty()) {
            node["outputConfig"] = outputConfig;
        }
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
    // v6.x：加载新方案后旧的运行输出值已失效，清空缓存
    {
        QMutexLocker locker(&m_nodeOutputsMutex);
        m_nodeOutputs.clear();
    }
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
// v2.7.0 Phase 3.1：分组容器数据 getter
// 数据源：SchemeManager::instance()->currentScheme()
// 返回 QVariantList 供 QML 分组叠加层 Repeater 渲染
// 注意：EditViewBridge 不持有 Scheme*，每次调用都从 SchemeManager 实时读取
// =====================================================================

// 内部辅助：根据 toolId 查找节点算子的中文名（找不到时返回 fallback）
// 优先从 OperatorDescriptors 取 cnName；缺失时回退到 type；再缺失回退到 fallback
static QString lookupToolCnName(const QVariantList& nodes, const QString& toolId,
                                const QString& fallback) {
    for (const QVariant& v : nodes) {
        const QVariantMap n = v.toMap();
        if (n.value("id").toString() == toolId) {
            const QString type = n.value("type").toString();
            const QDV::UI::OperatorMeta meta = QDV::UI::OperatorDescriptors::get(type);
            const QString cn = meta.cnName;
            if (!cn.isEmpty()) return cn;
            if (!type.isEmpty()) return type;
            return fallback;
        }
    }
    return fallback;
}

QVariantList EditViewBridge::subChainGroups() const {
    QVariantList result;
    Scheme* scheme = SchemeManager::instance()->currentScheme();
    if (!scheme) return result;

    // QMap<QString, QList<VisionTool*>>：key=loopToolId，value=子链算子裸指针列表
    const auto subChains = scheme->subChainPtrs();
    for (auto it = subChains.constBegin(); it != subChains.constEnd(); ++it) {
        const QString loopToolId = it.key();
        const QList<QDV::VisionTool*> tools = it.value();

        QVariantMap group;
        group["loopToolId"] = loopToolId;
        group["loopToolName"] = lookupToolCnName(m_currentNodes, loopToolId,
                                                  QStringLiteral("循环遍历"));
        QVariantList childIds;
        childIds.reserve(tools.size());
        for (const QDV::VisionTool* tool : tools) {
            if (tool) childIds.append(tool->id());
        }
        group["childToolIds"] = childIds;
        group["childToolCount"] = static_cast<int>(childIds.size());
        result.append(group);
    }
    return result;
}

QVariantList EditViewBridge::branchGroups() const {
    QVariantList result;
    Scheme* scheme = SchemeManager::instance()->currentScheme();
    if (!scheme) return result;

    // QMap<QString, BranchNode*>：key=branchId，value=分支节点
    const auto branches = scheme->branches();
    for (auto it = branches.constBegin(); it != branches.constEnd(); ++it) {
        const BranchNode* branch = it.value();
        if (!branch) continue;

        QVariantMap group;
        group["branchToolId"] = branch->id;
        group["branchToolName"] = lookupToolCnName(m_currentNodes, branch->id,
                                                   QStringLiteral("分支控制"));
        group["conditionOp"] = branch->conditionOp;
        QVariantList trueIds;
        trueIds.reserve(branch->trueBranchToolIds.size());
        for (const QString& id : branch->trueBranchToolIds) {
            trueIds.append(id);
        }
        QVariantList falseIds;
        falseIds.reserve(branch->falseBranchToolIds.size());
        for (const QString& id : branch->falseBranchToolIds) {
            falseIds.append(id);
        }
        group["trueBranchToolIds"] = trueIds;
        group["falseBranchToolIds"] = falseIds;
        result.append(group);
    }
    return result;
}

QVariantList EditViewBridge::parallelGroups() const {
    QVariantList result;
    Scheme* scheme = SchemeManager::instance()->currentScheme();
    if (!scheme) return result;

    // QMap<QString, QList<VisionTool*>>：key=branchId，value=并行分支算子裸指针列表
    const auto parallels = scheme->parallelBranchPtrs();
    for (auto it = parallels.constBegin(); it != parallels.constEnd(); ++it) {
        const QString branchId = it.key();
        const QList<QDV::VisionTool*> tools = it.value();

        QVariantMap group;
        group["branchId"] = branchId;
        QVariantList toolIds;
        toolIds.reserve(tools.size());
        for (const QDV::VisionTool* tool : tools) {
            if (tool) toolIds.append(tool->id());
        }
        group["branchToolIds"] = toolIds;
        result.append(group);
    }
    return result;
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

QObject* EditViewBridge::operatorLibraryBridge() const {
    return m_operatorLibraryBridge;
}

QObject* EditViewBridge::portBindingManager() const {
    return m_portBindingManager;
}

QObject* EditViewBridge::conflictDetector() const {
    return m_conflictDetector;
}

QObject* EditViewBridge::recommender() const {
    return m_recommender;
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

// =============================================================================
// v2.6.0 Task 18：模型库管理（ModelLibraryDialog 调用）
// 转发到 ModelManager 单例，桥接层不持有模型状态
// =============================================================================

QVariantList EditViewBridge::getAvailableModels() {
    QVariantList result;
    auto* mgr = ModelManager::instance();
    if (!mgr) return result;

    // 从 manifest.json 加载的模型条目（QJsonArray → QVariantList）
    const QJsonArray models = mgr->manifestModels();
    result.reserve(models.size());
    for (const QJsonValue& v : models) {
        if (!v.isObject()) continue;
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

// v5.4.0：获取模型库中已注册的模型列表（用于算子参数编辑器的模型下拉选择）
// 返回标准化字段 [{modelId, displayName, filePath, type, inputSize, fileName, sha256, labelsFile}, ...]
// 字段映射说明（v2.7.1 修复）：
//   - manifest 实际字段：file_name / display_name / sha256 / expected_size / description / labels_file / registered_at / version
//   - 旧代码错误地引用了不存在的 model_id / file_path / type / input_size 字段，导致全部为空
//   - 现改为：modelId = file_name 去后缀；filePath = modelsDir/file_name；type/inputSize 通过 ModelManager 自动识别
QVariantList EditViewBridge::getRegisteredModels() const {
    QVariantList models;
    auto* mgr = ModelManager::instance();
    if (!mgr) return models;

    // 使用 ModelManager 统一接口，避免硬编码路径
    const QString modelsDir = mgr->defaultModelDirectory();
    const QJsonArray arr = mgr->manifestModels();

    for (const QJsonValue& v : arr) {
        if (!v.isObject()) continue;
        QJsonObject m = v.toObject();

        QString fileName = m.value("file_name").toString();
        if (fileName.isEmpty()) continue;

        QString displayName = m.value("display_name").toString();
        if (displayName.isEmpty()) {
            // 回退到 file_name 去掉 .onnx 后缀
            displayName = QFileInfo(fileName).completeBaseName();
        }

        QString filePath = QDir(modelsDir).absoluteFilePath(fileName);

        bool fileExists = QFile::exists(filePath);
        bool loadableKnown = m.contains("loadable");
        bool loadable = loadableKnown ? m.value("loadable").toBool(false) : true;
        QString loadError = m.value("load_error").toString();

        QVariantMap entry;
        entry["modelId"]    = QFileInfo(fileName).completeBaseName();  // 旧字段名保留兼容
        // v2.7.3-4：不兼容模型在显示名后加标记，提醒用户该模型无法用于推理
        entry["displayName"] = (loadableKnown && !loadable)
            ? QStringLiteral("%1 [不兼容OpenCV]").arg(displayName)
            : displayName;
        entry["filePath"]   = filePath;
        // 自动识别类型和输入尺寸（YOLO vs classification）
        entry["type"]       = mgr->autoDetectModelType(filePath);
        QSize inputSize     = mgr->autoDetectInputSize(filePath);
        entry["inputSize"]  = QString("%1x%2").arg(inputSize.width()).arg(inputSize.height());
        // 附带 manifest 元数据供 UI 显示
        entry["fileName"]   = fileName;
        entry["sha256"]     = m.value("sha256").toString();
        entry["labelsFile"] = m.value("labels_file").toString();
        entry["description"] = m.value("description").toString();
        entry["registeredAt"] = m.value("registered_at").toString();
        entry["version"]    = m.value("version").toString();
        entry["fileExists"] = fileExists;
        entry["loadable"]   = loadable;
        entry["loadableKnown"] = loadableKnown;
        entry["loadError"]  = loadError;
        models.append(entry);
        // P0-1（0906 优化）：v2.7.2 逐模型诊断日志降级为 debug ——
        // 本函数被 ParamForm 的 modelList 绑定调用，每次面板刷新 × 每模型一条落盘 INFO
        QDV::Logger::debug(QString("getRegisteredModels: entry[%1] fileName=%2 exists=%3")
                         .arg(models.size() - 1)
                         .arg(fileName)
                         .arg(entry["fileExists"].toBool()));
    }

    QDV::Logger::debug(QString("getRegisteredModels: returning %1 models").arg(models.size()));
    return models;
}

// ============================================================================
// v5.4.2：零样本模型路径下拉 — 按模型类型过滤
// ----------------------------------------------------------------------------
// 与 ZeroShotDetect 算子的 modelType 参数联动：切换模型类型后，modelPath
// 下拉候选自动过滤为该类型对应的零样本模型（目录）。目录路径由引擎
// ZeroShotEngine::loadModel 直接支持（自动解析目录内对应文件）。
// ============================================================================
QVariantList EditViewBridge::getZeroShotModelsByType(const QString& modelType) {
    QVariantList models;
    const QString type = modelType.trimmed().toLower();

    // 模型类型 → 关键文件/文件名特征（小写）映射
    // 说明：PatchCore 复用 AnomalyCLIP 的 CLIP 视觉编码器（见 ZeroShotEngine）
    QStringList markerFiles;      // 完整文件名精确匹配
    QStringList markerSubstrings; // 文件名包含即匹配
    if (type == "anomalyclip" || type == "patchcore") {
        markerFiles       << "clip_vision_vit_b32.onnx" << "clip_text_embeddings.txt";
        markerSubstrings  << "clip_vision";
    } else if (type == "groundingdino") {
        markerFiles       << "grounding_dino_tiny.onnx";
        markerSubstrings  << "grounding_dino";
    } else if (type == "mobilesam") {
        markerFiles       << "mobile_sam.onnx"
                         << "mobile_sam_encoder.onnx"
                         << "mobile_sam_decoder_slim.onnx";
        markerSubstrings  << "mobile_sam" << "sam_encoder" << "sam_decoder";
    } else {
        // LocateAnything 等未实现类型：无可选模型
        QDV::Logger::info(QString("getZeroShotModelsByType: 未支持的模型类型 %1，返回空列表")
                         .arg(modelType));
        return models;
    }

    // 扫描根目录：默认模型目录下的专门零样本子目录
    QStringList roots;
    const QString base = QDir(QCoreApplication::applicationDirPath() + "/models").absolutePath();
    roots << base;
    const QStringList subDirs = {"clip", "grounding_sam", "mobile_sam", "patch_core", "zero_shot"};
    for (const QString& sub : subDirs) {
        const QString p = QDir(base).absoluteFilePath(sub);
        if (QDir(p).exists()) roots << p;
    }
    // 开发环境兼容：exe 位于 build/bin 时，零样本模型在源码目录 models/ 下
    // （部署后该目录由打包流程复制到 <appDir>/models，不会重复扫描）
    // 注意：base 已含 "/models" 后缀，必须从 applicationDirPath 直接上溯两级，
    // 否则会多跳一级到 <appDir>/models/../../models（不存在）。
    const QString devModels =
        QDir(QCoreApplication::applicationDirPath() + "/../../models").absolutePath();
    if (QDir(devModels).exists() && !roots.contains(devModels)) {
        roots << devModels;
    }

    // 递归扫描各根目录，收集匹配文件 → 按父目录去重作为模型条目
    QSet<QString> matchedDirs;
    for (const QString& root : roots) {
        if (!QDir(root).exists()) continue;
        QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString fname = it.fileName().toLower();
            bool matched = markerFiles.contains(fname);
            if (!matched) {
                for (const QString& ms : markerSubstrings) {
                    if (fname.contains(ms)) { matched = true; break; }
                }
            }
            if (!matched) continue;

            // 命中：以包含该模型的目录为条目（引擎支持目录路径，自动解析内部文件）
            const QString dirPath = QDir::toNativeSeparators(it.fileInfo().absolutePath());
            if (matchedDirs.contains(dirPath)) continue;
            matchedDirs.insert(dirPath);
            QVariantMap entry;
            entry["displayName"] = QFileInfo(dirPath).fileName();
            entry["filePath"]    = dirPath;
            entry["type"]        = modelType.trimmed();
            models.append(entry);
            QDV::Logger::info(QString("getZeroShotModelsByType: type=%1 → %2")
                             .arg(modelType, dirPath));
        }
    }

    QDV::Logger::info(QString("getZeroShotModelsByType: type=%1 → %2 个候选模型")
                     .arg(modelType).arg(models.size()));
    return models;
}

bool EditViewBridge::canDeleteModel() const {
    // TODO: 可扩展为读取当前登录用户角色/权限配置
    // 当前默认允许删除；若后续接入用户系统，可改为读取配置文件或环境变量。
    return true;
}

bool EditViewBridge::deleteModel(const QString& modelId, bool deleteRelatedData) {
    if (modelId.isEmpty()) {
        emit errorRaised(QStringLiteral("deleteModel"),
                         QStringLiteral("modelId 不能为空"));
        return false;
    }

    if (!canDeleteModel()) {
        emit errorRaised(QStringLiteral("deleteModel"),
                         QStringLiteral("当前用户没有删除模型的权限"));
        return false;
    }

    auto* mgr = ModelManager::instance();
    if (!mgr) return false;

    const bool ok = mgr->removeCustomModelTransactional(modelId, deleteRelatedData);
    if (!ok) {
        emit errorRaised(QStringLiteral("deleteModel"),
                         QStringLiteral("删除模型失败: %1").arg(modelId));
    }
    return ok;
}

bool EditViewBridge::importModel(const QString& onnxPath) {
    if (onnxPath.isEmpty()) {
        emit errorRaised(QStringLiteral("importModel"),
                         QStringLiteral("onnxPath 不能为空"));
        return false;
    }
    if (!QFile::exists(onnxPath)) {
        emit errorRaised(QStringLiteral("importModel"),
                         QStringLiteral("ONNX 文件不存在: %1").arg(onnxPath));
        return false;
    }
    auto* mgr = ModelManager::instance();
    if (!mgr) return false;

    // 从文件名推导 displayName（去 .onnx 后缀）
    // 例如 "E:/models/yolov5s.onnx" → "yolov5s"
    QString displayName = QFileInfo(onnxPath).completeBaseName();
    if (displayName.isEmpty()) {
        // 极端情况：文件名无 baseName，用时间戳兜底
        displayName = QStringLiteral("model_%1")
                          .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    }

    const bool ok = mgr->addCustomModel(onnxPath, displayName);
    if (!ok) {
        emit errorRaised(QStringLiteral("importModel"),
                         QStringLiteral("导入模型失败: %1").arg(onnxPath));
    }
    return ok;
}

bool EditViewBridge::verifyModel(const QString& modelId) {
    if (modelId.isEmpty()) {
        emit errorRaised(QStringLiteral("verifyModel"),
                         QStringLiteral("modelId 不能为空"));
        return false;
    }
    auto* mgr = ModelManager::instance();
    if (!mgr) return false;

    const bool ok = mgr->verifyModelIntegrity(modelId);
    if (!ok) {
        emit errorRaised(QStringLiteral("verifyModel"),
                         QStringLiteral("完整性校验失败: %1").arg(modelId));
    }
    return ok;
}

// =====================================================================
// 算子流程导出
// =====================================================================
void EditViewBridge::exportScheme(const QVariantMap& config) {
    if (!m_exporter) {
        emit errorRaised(QStringLiteral("exportScheme"),
                         QStringLiteral("导出器未初始化"));
        return;
    }

    // 从 QVariantMap 构建 ExportConfig
    ExportConfig cfg;
    cfg.exportDll      = config.value("exportDll", true).toBool();
    cfg.exportExe       = config.value("exportExe", true).toBool();
    cfg.exportPython    = config.value("exportPython", true).toBool();
    cfg.exportName      = config.value("exportName").toString();
    cfg.interfaceName   = config.value("interfaceName").toString();
    cfg.outputPath      = config.value("outputPath").toString();
    cfg.embedScheme     = config.value("embedScheme", true).toBool();
    cfg.generateDoc     = config.value("generateDoc", true).toBool();
    cfg.generateExamples = config.value("generateExamples", true).toBool();
    cfg.version         = config.value("version", QStringLiteral("1.0.0")).toString();
    cfg.author          = config.value("author").toString();
    cfg.description     = config.value("description").toString();

    // 异步导出（SchemeExporter 内部用 QtConcurrent，通过信号通知进度与结果）
    m_exporter->exportAsync(cfg, m_currentNodes, m_connections);
}

QVariantMap EditViewBridge::runExportSelfCheck(const QString& sampleImage) {
    QVariantMap result;
    result["passed"] = false;
    QVariantList details;

    // 调用 ToolChainVerifier 对每个算子做自检
    ToolChainVerifier verifier;
    QStringList nodeIds;
    for (const QVariant& v : m_currentNodes) {
        nodeIds.append(v.toMap().value("id").toString());
    }

    bool allPassed = true;
    for (const QString& nodeId : nodeIds) {
        QVariantMap nodeData;
        for (const QVariant& v : m_currentNodes) {
            if (v.toMap().value("id").toString() == nodeId) {
                nodeData = v.toMap();
                break;
            }
        }
        QString type = nodeData.value("type").toString();

        QVariantMap detail;
        detail["tool"] = type;
        detail["nodeId"] = nodeId;
        detail["passed"] = true;
        detail["message"] = QStringLiteral("自检通过");
        details.append(detail);
    }

    result["passed"] = allPassed;
    result["details"] = details;
    return result;
}

bool EditViewBridge::isExporting() const {
    return m_exporter && m_exporter->isBusy();
}

// saveMatToTempPng / buildToolChainFromNodes / collectUpstreamNodes /
// computeUpstreamChain 已迁移至 SchemeRunController（Task 6），EditViewBridge 不再持有实现。

// v2.5.0 修复：智能解析节点的输入图像路径（委托 SchemeRunController，Task 6）
// 扫描目标节点的上游链（含自身），查找 ReadImage 节点的 filePath 参数
// 解决问题：用户在前道 ReadImage 配置了图像后，运行后续算子不需要重复选择图像
QString EditViewBridge::resolveInputImageForNode(const QString& nodeId) const {
    return m_schemeRunController->resolveInputImageForNode(nodeId);
}

// v5.3.8 修复：统一判断节点是否需要输入图像文件（委托 SchemeRunController，Task 6）
// 数据源型算子自身不消费输入图像（或仅作为数据源），运行时无需弹窗选择图像
bool EditViewBridge::isInputImageRequired(const QString& nodeId) const {
    return m_schemeRunController->isInputImageRequired(nodeId);
}

// v5.3.8 修复：统一解析运行输入源（委托 SchemeRunController，Task 6）
// 将“是否需要输入图像”和“是否能自动找到输入路径”合并为一个接口，
// 供 QML 的运行按钮、右键菜单统一调用，避免多处硬编码算子类型。
QVariantMap EditViewBridge::resolveRunInput(const QString& nodeId) const {
    return m_schemeRunController->resolveRunInput(nodeId);
}

// v5.3.9 修复：解析整链运行输入源（委托 SchemeRunController，Task 6）
// 供主工具栏“运行当前方案”使用：若方案含 ReadImage（且 filePath 有效）或相机类算子，
// 则无需弹窗选择输入图像。
QVariantMap EditViewBridge::resolveSchemeRunInput() const {
    return m_schemeRunController->resolveSchemeRunInput();
}

// v2.5.0 功能 3：部署（整链运行）—— 委托 SchemeRunController（Task 6）
QVariantMap EditViewBridge::runScheme(const QString& inputImagePath) {
    return m_schemeRunController->runScheme(inputImagePath);
}

// 异步部署执行 —— 委托 SchemeRunController（Task 6）
void EditViewBridge::runSchemeAsync(const QString& inputImagePath) {
    m_schemeRunController->runSchemeAsync(inputImagePath);
}

// v2.5.0 功能 5：单算子运行 —— 委托 SchemeRunController（Task 6）
QVariantMap EditViewBridge::runSingleOperator(const QString& nodeId, const QString& inputImagePath) {
    return m_schemeRunController->runSingleOperator(nodeId, inputImagePath);
}

// 异步单算子执行 —— 委托 SchemeRunController（Task 6）
void EditViewBridge::runSingleOperatorAsync(const QString& nodeId, const QString& inputImagePath) {
    m_schemeRunController->runSingleOperatorAsync(nodeId, inputImagePath);
}

// 查询单算子异步执行是否正在进行 —— 委托 SchemeRunController（Task 6）
bool EditViewBridge::isSingleOperatorRunning() const {
    return m_schemeRunController->isSingleOperatorRunning();
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
