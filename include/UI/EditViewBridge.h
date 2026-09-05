#ifndef EDITVIEW_BRIDGE_H
#define EDITVIEW_BRIDGE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>      // v2.2.0：收藏集合
#include <QMap>      // v2.2.0：使用频次
#include <QHash>     // v6.x：运行后节点输出值缓存
#include <QMutex>    // v6.x：节点输出值缓存线程安全
#include <QFutureWatcher>  // 异步部署执行

class QUndoStack;
class QJsonObject;   // v2.1.0 M4：方案加载后解析用
namespace QDV { namespace UI { class SchemeSerializer; } }
// v2.5.0 功能 3/5：部署与单算子运行所需的前置声明
namespace QDV { class VisionTool; }   // VisionTool 在 QDV 命名空间内（Core/VisionTool.h）
namespace cv { class Mat; }           // OpenCV Mat 前置声明（避免头文件强依赖 opencv）
class ToolChainExecutor;              // ToolChainExecutor 在全局命名空间（Vision/ToolChainExecutor.h）
struct ToolResult;                    // ToolResult 在全局命名空间（Core/VisionTool.h）
// v2.6.0 变量管理与预览管理前置声明
namespace QDV { class VariableManager; }
namespace QDV { class ImageVariableManager; }
namespace QDV { class PreviewManager; }
// 算子流程导出编排器前置声明
class SchemeExporter;
// v2.7.0 O1a：算子库桥接器前置声明
class OperatorLibraryBridge;
// v2.7.0 Phase 3.1 Task 6：从 EditViewBridge 拆分的协作者前置声明
class ClipboardManager;        // 算子参数剪贴板
class SchemeRunController;     // 方案运行控制
class OutputConfigManager;     // 算子输出配置
class PortBindingManager;      // 端口绑定数据模型（多输入/多输出查询与校验）
class OutputConflictDetector;  // 输入/输出项冲突检测引擎（Task 6）
class OperatorRecommender;     // 智能算子推荐引擎（spec：editor-output-connection-optimization，Task 9）

/**
 * @brief C++ ↔ QML 桥接器（v2.1.0 M2 改造）
 *
 * 职责：
 * 1. 暴露 QML 端需要的属性：可用算子列表 / 当前方案节点 / 撤销重做状态
 * 2. 暴露 QML 端需要的槽：添加算子 / 移动节点 / 删除节点 / 撤销 / 重做
 * 3. 转发 SchemeManager 变化（命名变化、节点增删）到 QML
 *
 * 设计要点：
 * - 单例不强制（每个 EditView 可独立一个 bridge）
 * - 不在头文件暴露 Scheme/ VisionTool 指针，QML 端仅看到 QVariantList/Map
 *   （避免 QML 直接持有 C++ 智能指针导致生命周期问题）
 * - 与 QUndoStack 解耦，通过 slot 注入（避免 include 循环）
 */
class EditViewBridge : public QObject {
    Q_OBJECT

    friend class PortBindingManager;   // 端口绑定模型需复用端口类型校验（spec：editor-output-connection-optimization）
    friend class OutputConflictDetector; // 冲突检测引擎需复用端口类型校验（复用 checkPortCompatible）
    friend class OperatorRecommender;  // 推荐引擎需访问 m_useCounts/m_recents/m_currentNodes/m_connections（spec Task 9）

    // === QML 可读属性 ===
    Q_PROPERTY(QStringList operatorTypes      READ operatorTypes      NOTIFY operatorsChanged)
    Q_PROPERTY(QVariantList currentNodes      READ currentNodes       NOTIFY currentNodesChanged)
    Q_PROPERTY(QVariantList connections       READ connections        NOTIFY connectionsChanged)
    Q_PROPERTY(QString     currentSchemeName  READ currentSchemeName  NOTIFY currentSchemeNameChanged)
    Q_PROPERTY(QString     currentSchemeFilePath READ currentSchemeFilePath NOTIFY currentSchemeFilePathChanged)
    Q_PROPERTY(bool        isDirty             READ isDirty            NOTIFY isDirtyChanged)
    Q_PROPERTY(bool        canUndo            READ canUndo            NOTIFY canUndoChanged)
    Q_PROPERTY(bool        canRedo            READ canRedo            NOTIFY canRedoChanged)
    // v2.6.0 变量管理与预览管理（Q_PROPERTY 让 QML 端 bridge.xxxManager 直接返回实例）
    Q_PROPERTY(QObject* variableManager       READ variableManager    CONSTANT)
    Q_PROPERTY(QObject* imageVariableManager  READ imageVariableManager CONSTANT)
    Q_PROPERTY(QObject* previewManager        READ previewManager     CONSTANT)
    // v2.7.0 O1a：算子库桥接器（QML 端通过 bridge.operatorLibraryBridge 访问）
    Q_PROPERTY(QObject* operatorLibraryBridge READ operatorLibraryBridge CONSTANT)
    // 端口绑定数据模型（spec：editor-output-connection-optimization）
    Q_PROPERTY(QObject* portBindingManager READ portBindingManager CONSTANT)
    // 输入/输出项冲突检测引擎（spec：editor-output-connection-optimization，Task 6）
    Q_PROPERTY(QObject* conflictDetector READ conflictDetector CONSTANT)
    // 智能算子推荐引擎（spec：editor-output-connection-optimization，Task 9）
    Q_PROPERTY(QObject* recommender READ recommender CONSTANT)
    // v2.7.0 Phase 3.1：子链/分支/并行分组容器可视化（只读）
    // 数据源：SchemeManager::instance()->currentScheme()，不在 bridge 中缓存
    Q_PROPERTY(QVariantList subChainGroups  READ subChainGroups  NOTIFY subChainGroupsChanged)
    Q_PROPERTY(QVariantList branchGroups    READ branchGroups    NOTIFY branchGroupsChanged)
    Q_PROPERTY(QVariantList parallelGroups  READ parallelGroups  NOTIFY parallelGroupsChanged)

public:
    explicit EditViewBridge(QObject* parent = nullptr);
    ~EditViewBridge() override;

    // === C++ 端注入接口（由 EditView 在构造后调用） ===
    /// 注入撤销栈（可为 nullptr 表示禁用 undo/redo）
    void setUndoStack(QUndoStack* stack);
    /// v2.7.0 Phase 3.1 Task 6：访问内部撤销栈（供 OutputConfigManager 等协作者 push 命令）
    QUndoStack* undoStack() const { return m_undoStack; }
    /// 通知方案已加载（QML 端刷新 currentNodes / currentSchemeName）
    void notifySchemeChanged();
    /// 通知方案名已变（QML 端刷新 currentSchemeName）
    void notifySchemeNameChanged(const QString& newName);

    // === Property getters ===
    QStringList  operatorTypes()     const { return m_operatorTypes; }
    QVariantList currentNodes()      const { return m_currentNodes; }
    /// v2.1.0 M4 引入：返回 m_currentNodes 非 const 引用（UndoCommand 使用，避免拷贝）
    QVariantList& currentNodesRef()  { return m_currentNodes; }
    /// P1-A5 修复：返回 m_connections 非 const 引用（RemoveNodeCommand 清理连接用）
    QVariantList& connectionsRef()   { return m_connections; }
    /// v2.1.0 M4 引入：手动触发 currentNodesChanged 信号（UndoCommand 在多次原子操作时避免重复信号）
    void notifyCurrentNodesChanged() { emit currentNodesChanged(); }
    /// P1-A5 修复：手动触发 connectionsChanged 信号
    void notifyConnectionsChanged()  { emit connectionsChanged(); }
    QString      currentSchemeName() const { return m_currentSchemeName; }
    bool         canUndo()           const;
    bool         canRedo()           const;

    // === v2.7.0 Phase 3.1：分组容器数据 getter ===
    // 数据源：SchemeManager::instance()->currentScheme()
    // 返回 QVariantList，元素结构参见任务文档（subChainGroups/branchGroups/parallelGroups）
    QVariantList subChainGroups() const;
    QVariantList branchGroups() const;
    QVariantList parallelGroups() const;

    // === v2.1.0 M4 内部方法（供 UndoCommand 调用；不自动 emit 信号） ===
    /// 内部：移动节点（不发信号）
    void moveNodeInternal(const QString& nodeId, qreal newX, qreal newY, bool emitSignals);
    /// 内部：添加连接（M4 阶段画布未实现连接，方法保留空操作）
    void addConnectionInternal(const QString& fromId, const QString& fromPort,
                               const QString& toId,   const QString& toPort,
                               bool emitSignals);
    /// 内部：移除连接（M4 阶段空操作）
    void removeConnectionInternal(const QString& fromId, const QString& fromPort,
                                  const QString& toId,   const QString& toPort,
                                  bool emitSignals);
    /// 内部：更新单个参数（不发信号）
    void updateParamInternal(const QString& nodeId, const QString& paramName,
                             const QVariant& value, bool emitSignals);
    /// 内部：返回 m_connections（M4 阶段为空，预留）
    QVariantList connections() const { return m_connections; }

    // === v2.1.0 M4 I/O 槽（QML 端点击「保存」「加载」调用） ===
    // v5.3.3 修复：移到 public slots 区域，否则 QML 报 "is not a function"
    /// 保存当前方案到文件（异步）
    /// @param filePath 目标 .json 文件路径
    Q_INVOKABLE void saveToFile(const QString& filePath);
    /// 加载方案文件（异步）
    /// @param filePath 源 .json 文件路径
    Q_INVOKABLE void loadFromFile(const QString& filePath);
    /// 新建空方案
    Q_INVOKABLE void newScheme();
    /// 标记方案已被修改（每次 addOperator/moveNode/updateOperatorParams 后自动调用）
    void markDirty();
    /// 标记方案已保存（清空 dirty 标志，保存成功后调用）
    void clearDirty();

    /// v2.1.0 M4 引入：访问当前方案文件路径
    QString currentSchemeFilePath() const { return m_currentSchemeFilePath; }
    /// v2.1.0 M4 引入：设置当前方案文件路径（save/load 时由 slot 更新）
    void setCurrentSchemeFilePath(const QString& path) {
        if (m_currentSchemeFilePath == path) return;
        m_currentSchemeFilePath = path;
        emit currentSchemeFilePathChanged();
    }
    /// v2.1.0 M4 引入：方案是否被修改（决定「保存」按钮 enabled）
    bool isDirty() const { return m_isDirty; }

public slots:
    /// QML 端调用：添加一个算子到当前方案（坐标、类型）
    /// 返回新节点 ID；空字符串表示失败
    QString addOperator(const QString& type, qreal x, qreal y);
    /// QML 端调用：移动节点（M4 Undo 框架时扩展为 NodeMoveCommand）
    void moveNode(const QString& nodeId, qreal newX, qreal newY);
    /// P1-B03 修复：拖拽中实时移动节点（不推 undo 栈，仅 emit currentNodesChanged 触发连线重绘）
    /// 配合 commitNodeMove 使用：拖拽中调 moveNodeLive，onReleased 时调 commitNodeMove 推栈
    Q_INVOKABLE void moveNodeLive(const QString& nodeId, qreal newX, qreal newY);
    /// P1-B03 修复：提交节点移动到 undo 栈（onReleased 时调用）
    /// @param originX/originY 拖拽前坐标（由 QML 端 onPressed 记录）
    /// @param newX/newY 拖拽后坐标
    Q_INVOKABLE void commitNodeMove(const QString& nodeId,
                                    qreal originX, qreal originY,
                                    qreal newX, qreal newY);
    /// QML 端调用：连接两个节点
    void connectNodes(const QString& fromId, const QString& fromPort,
                      const QString& toId,   const QString& toPort);
    /// QML 端调用：删除一条连线
    void disconnectEdge(const QString& fromId, const QString& fromPort,
                        const QString& toId,   const QString& toPort);
    /// QML 端调用：删除节点
    void deleteNode(const QString& nodeId);
    /// QML 端调用：撤销
    void undo();
    /// QML 端调用：重做
    void redo();
    /// QML 端调用：通知节点已选中（供 PropertyPreviewPanel 联动）
    void selectNode(const QString& nodeId);
    /// QML 端调用：双击节点（触发模态详编；M3 实现）
    void openNodeEditor(const QString& nodeId);

    // === v2.1.0 M3 算子元数据槽（供 QML 动态表单使用） ===
    /// QML 端调用：返回指定 type 的算子元数据（OperatorMeta.toMap()）
    QVariantMap getOperatorMeta(const QString& type) const;

    /// v6.x：返回节点最近一次运行后的输出值（ToolResult.ports），供变量管理面板显示
    Q_INVOKABLE QVariantMap getNodeOutputValues(const QString& nodeId) const;
    /// v6.x：写入节点运行后的输出值（SchemeRunController 回调，线程安全）
    void setNodeOutputValues(const QString& nodeId, const QVariantMap& ports);

    // === v2.8.0 算子帮助内容提供器（转发至 OperatorHelpProvider 单例） ===
    /// QML 端调用：返回算子短描述（coreMeaning 优先，回退 description/cnName，≤100 字符）
    Q_INVOKABLE QString getShortDesc(const QString& type) const;
    /// 诊断：把 QML 层消息转发到 Logger 日志文件（定位推荐面板显示问题用）
    Q_INVOKABLE void logDiag(const QString& msg) const;
    /// QML 端调用：返回算子完整文档（含 coreMeaning/scenario/params/caveats/relatedOperators）
    Q_INVOKABLE QVariantMap getOperatorDoc(const QString& type) const;
    /// QML 端调用：返回参数的 help 字段（为空时回退 "cnName (类型名)"）
    Q_INVOKABLE QString getParamHelp(const QString& type, const QString& paramName) const;
    /// P1-3 typed ports：返回指定算子 type 的输入/输出端口元数据
    /// @return {inputs: [{name,cnName,type,dir,desc}, ...], outputs: [...]}
    /// 旧算子未声明端口时返回空列表（QML 端按兼容模式渲染单一 Image 端口）
    Q_INVOKABLE QVariantMap getOperatorPorts(const QString& type) const;
    /// QML 端调用：返回所有算子分类（按出现顺序去重）
    QVariantList operatorCategories() const;
    /// QML 端调用：返回指定分类下的算子列表（每个元素为 OperatorMeta.toMap()）
    QVariantList operatorsInCategory(const QString& category) const;
    /// v2.2.0：搜索算子（匹配 cnName/type/description/category/subGroup）
    QVariantList searchOperators(const QString& keyword) const;
    /// v2.2.0：返回指定分类下的子分组名列表
    QVariantList operatorSubGroups(const QString& category) const;
    /// QML 端调用：返回指定节点的当前参数（{paramName: value, ...}）
    /// 节点不存在时返回空 map
    QVariantMap getOperatorParams(const QString& nodeId) const;
    /// QML 端调用：更新节点参数（写入 m_currentNodes[i].params）
    /// 校验失败时 emit errorRaised
    void updateOperatorParams(const QString& nodeId, const QVariantMap& params);
    /// v5.4：返回指定节点的输出开关配置（{outputName: {enabled: bool}, ...}）
    /// 节点不存在时返回空 map
    Q_INVOKABLE QVariantMap getOutputConfig(const QString& nodeId) const;
    /// v5.4：更新指定节点的输出开关配置（走 UndoCommand 可撤销路径）
    /// @param nodeId 目标节点 ID
    /// @param outputs 完整的输出配置 map（整体替换）
    Q_INVOKABLE void updateOutputConfig(const QString& nodeId, const QVariantMap& outputs);
    /// QML 端调用：校验单参数（返回错误列表；空列表表示通过）
    QStringList validateParam(const QString& type, const QString& paramName, const QVariant& value) const;

    // === P1-B4-H6 复制粘贴参数 ===
    /// 复制指定节点的参数到内部剪贴板
    /// @return true=成功；false=节点不存在
    Q_INVOKABLE bool copyNodeParams(const QString& nodeId);
    /// 将剪贴板参数粘贴到目标节点（仅覆盖参数名匹配的字段，类型不同会跳过）
    /// @return true=成功；false=无剪贴板/目标节点不存在/类型不匹配
    Q_INVOKABLE bool pasteNodeParams(const QString& nodeId);
    /// 剪贴板是否有数据
    Q_INVOKABLE bool hasClipParams() const;
    /// 剪贴板对应的算子类型（用于 UI 显示"来自 xxx 算子"）
    Q_INVOKABLE QString clipParamsType() const;

    // === v2.2.0 收藏/最近/常用 ===
    /// 切换算子收藏状态，返回切换后是否收藏
    bool toggleFavorite(const QString& type);
    /// 是否已收藏
    bool isFavorite(const QString& type) const;
    /// 收藏算子 type 列表
    QVariantList favorites() const;
    /// 最近使用的算子 type 列表（最多 10 个，按最近优先）
    QVariantList recents() const;
    /// 常用算子 type 列表（使用频次排序，最多 10 个）
    QVariantList commons() const;
    /// v2.2.0：标记算子被使用（内部在 addOperator 时调用，更新 recents/commons）
    void markUsed(const QString& type);

    // === v3.1.0 图像分析与处理槽 ===
    /// 分析图像并返回结构化结果（QML 端调用）
    /// @param filePath 图像文件路径
    /// @return {ok, error, width, height, channels, format, fileSize,
    ///          features: [{name, value, unit}],
    ///          histogram: [{value}],
    ///          base64Thumbnail: "..."}
    Q_INVOKABLE QVariantMap analyzeImage(const QString& filePath);
    /// 处理图像并返回结果图像路径（QML 端调用）
    /// @param filePath 输入图像路径
    /// @param operation 处理操作（"grayscale","threshold","edge","blur", etc.）
    /// @param params 操作参数
    /// @return {ok, outputPath, width, height, elapsedMs, error}
    Q_INVOKABLE QVariantMap processImage(const QString& filePath,
                                          const QString& operation,
                                          const QVariantMap& params);
    /// 获取支持的图像格式列表
    Q_INVOKABLE QStringList supportedImageFormats() const;

    // === v2.5.0 功能 3：部署（整链运行） ===
    /// 运行当前方案的所有算子
    /// @param inputImagePath 输入图像文件路径（空字符串时使用 setCameraFrame 设置的帧）
    /// @return {success, totalTools, successCount, failCount, elapsedMs,
    ///          outputImagePath, toolResults: [{toolId, toolName, ok, elapsedMs, errorMessage}]}
    Q_INVOKABLE QVariantMap runScheme(const QString& inputImagePath);

    /// 异步部署执行（修复主线程阻塞导致"点选无响应"）
    /// 立即返回，执行在子线程进行；通过 schemeDeployStarted/schemeDeployFinished 通知
    Q_INVOKABLE void runSchemeAsync(const QString& inputImagePath);

    // === v2.5.0 功能 5：单算子运行（执行上游链 + 选中算子） ===
    /// 运行指定节点及其上游链
    /// @param nodeId 目标节点 ID
    /// @param inputImagePath 输入图像文件路径
    /// @return {success, upstreamCount, elapsedMs, outputImagePath,
    ///          upstreamResults: [...], targetResult: {...}}
    Q_INVOKABLE QVariantMap runSingleOperator(const QString& nodeId, const QString& inputImagePath);

    /// 异步单算子执行（交互式调参用，修复主线程阻塞）
    /// 立即返回，执行在子线程进行；通过 singleOperatorStarted/singleOperatorFinished 通知
    /// @param nodeId 目标节点 ID
    /// @param inputImagePath 输入图像路径（空则自动解析上游 ReadImage）
    Q_INVOKABLE void runSingleOperatorAsync(const QString& nodeId, const QString& inputImagePath);

    /// 查询单算子异步执行是否正在进行（QML 端用于禁用控件/显示忙碌）
    Q_INVOKABLE bool isSingleOperatorRunning() const;

    /// v2.5.0 修复：智能解析节点的输入图像路径
    /// 扫描目标节点的上游链（含自身），查找 ReadImage 节点的 filePath 参数
    /// @param nodeId 目标节点 ID
    /// @return 找到的图像路径（非空表示可自动执行，空表示需要用户手动选择）
    Q_INVOKABLE QString resolveInputImageForNode(const QString& nodeId) const;

    // v5.3.8 修复：统一判断节点是否需要输入图像文件
    /// 对 OpenFramegrabber/GrabImage/RobotPose/HandEyeCalib/ReadImage 等数据源型算子返回 false
    /// @param nodeId 目标节点 ID
    /// @return true=需要用户选择输入图像文件；false=算子自身提供数据，无需图像文件
    Q_INVOKABLE bool isInputImageRequired(const QString& nodeId) const;

    // v5.3.8 修复：统一解析运行输入源（供 QML 的运行按钮/右键菜单使用）
    /// @param nodeId 目标节点 ID
    /// @return { required: bool, path: string, hint: string }
    ///   required=false 表示相机/数据源算子，可直接执行；
    ///   required=true 且 path 非空表示找到 ReadImage 上游，可直接执行；
    ///   required=true 且 path 为空表示需要用户手动选择图像。
    Q_INVOKABLE QVariantMap resolveRunInput(const QString& nodeId) const;

    // v5.3.9 修复：解析整链运行输入源（供主工具栏“运行当前方案”使用）
    /// @return { required: bool, path: string, hint: string }
    ///   required=false 表示方案包含相机/ReadImage 等数据源，可直接执行；
    ///   required=true 且 path 非空表示找到 ReadImage 节点，可直接执行；
    ///   required=true 且 path 为空表示需要用户手动选择图像。
    Q_INVOKABLE QVariantMap resolveSchemeRunInput() const;

    // === v2.5.0 预留：相机帧接口（供后续相机采集功能使用） ===
    /// 设置相机帧（保存到临时文件，供 runScheme 在 inputImagePath 为空时使用）
    Q_INVOKABLE void setCameraFrame(const QString& tempFilePath);

    /// 获取当前相机帧路径（供 QML 端判断是否有相机输入）
    Q_INVOKABLE QString cameraFramePath() const { return m_cameraFramePath; }

    // === v2.6.0 变量管理与预览管理 ===
    /// 获取 VariableManager（QML 端通过 bridge.variableManager 访问）
    Q_INVOKABLE QObject* variableManager() const;
    /// 获取 ImageVariableManager
    Q_INVOKABLE QObject* imageVariableManager() const;
    /// 获取 PreviewManager
    Q_INVOKABLE QObject* previewManager() const;
    /// v2.7.0 O1a：获取 OperatorLibraryBridge
    Q_INVOKABLE QObject* operatorLibraryBridge() const;
    /// 获取 PortBindingManager（端口绑定数据模型）
    Q_INVOKABLE QObject* portBindingManager() const;
    /// 获取 OutputConflictDetector（输入/输出项冲突检测引擎，Task 6）
    Q_INVOKABLE QObject* conflictDetector() const;
    /// 获取 OperatorRecommender（智能算子推荐引擎，spec Task 9）
    Q_INVOKABLE QObject* recommender() const;
    /// 获取控制变量 JSON（保存方案时调用）
    Q_INVOKABLE QString variablesToJson() const;
    /// 加载控制变量 JSON（加载方案时调用）
    Q_INVOKABLE bool loadVariablesFromJson(const QString& jsonStr);

    // === v2.6.0 Task 18：模型库管理（ModelLibraryDialog 调用） ===
    /// 返回 manifest 中所有模型条目（每个元素为 QVariantMap，字段同 manifest.json：
    /// file_name / display_name / description / sha256 / expected_size / version /
    /// labels_file / registered_at）
    Q_INVOKABLE QVariantList getAvailableModels();
    /// v5.4.0：获取模型库中已注册的模型列表（用于算子参数编辑器的模型下拉选择）
    /// 返回 [{modelId, displayName, filePath, type, inputSize}, ...]
    Q_INVOKABLE QVariantList getRegisteredModels() const;
    /// v5.4.2：获取指定零样本模型类型下的可用模型列表（用于 ZeroShotDetect 算子
    /// modelPath 下拉，与 modelType 联动过滤）。
    /// 扫描专门零样本目录（models/clip、models/grounding_sam、models/mobile_sam、
    /// models/patch_core、models/zero_shot 等），按模型类型对应的关键文件过滤，
    /// 返回 [{displayName, filePath, type}, ...]，filePath 为模型目录路径。
    /// 未支持的类型（如 LocateAnything）返回空列表。
    /// @param modelType 模型类型字符串（AnomalyCLIP/GroundingDINO/MobileSAM/PatchCore）
    Q_INVOKABLE QVariantList getZeroShotModelsByType(const QString& modelType);
    /// 当前用户是否拥有模型删除权限（可扩展为读取角色配置；当前默认允许）
    Q_INVOKABLE bool canDeleteModel() const;
    /// 删除指定模型（事务性：rename .trash + manifest 记录）
    /// @param modelId 模型 ID（display_name 或 file_name 去 .onnx 后缀）
    /// @param deleteRelatedData 是否同时清理关联数据（labels、训练记录、评估报告等）
    /// @return true=成功；false=未找到/删除失败/无权限
    Q_INVOKABLE bool deleteModel(const QString& modelId, bool deleteRelatedData = false);
    /// 导入 ONNX 模型到 models/ 目录（displayName 从文件名推导）
    /// @param onnxPath 源 ONNX 文件绝对路径
    /// @return true=成功；false=源文件不存在/复制失败
    Q_INVOKABLE bool importModel(const QString& onnxPath);
    /// 校验模型完整性（流式 SHA256 + 大小比对）
    /// @param modelId 模型 ID
    /// @return true=校验通过；false=文件缺失/哈希不匹配
    Q_INVOKABLE bool verifyModel(const QString& modelId);

    // === 算子流程导出 ===
    /// 导出当前方案为 DLL/EXE/Python（异步，通过 exportProgress/exportFinished 通知）
    /// @param config QVariantMap 含字段：exportName/interfaceName/outputPath/
    ///                exportDll/exportExe/exportPython(bool)/embedScheme(bool)/
    ///                generateDoc(bool)/generateExamples(bool)/version/author/description
    Q_INVOKABLE void exportScheme(const QVariantMap& config);

    /// 运行导出自检（调 ToolChainVerifier + 示例图试跑，不阻断导出）
    /// @param sampleImage 示例图路径（空则尝试解析上游 ReadImage）
    /// @return {passed: bool, details: [{tool, passed, message}]}
    Q_INVOKABLE QVariantMap runExportSelfCheck(const QString& sampleImage);

    /// 当前是否正在导出
    Q_INVOKABLE bool isExporting() const;

signals:
    void operatorsChanged();
    void currentNodesChanged();
    void connectionsChanged();
    /// 输入/输出项冲突检测引擎冲突集合变化（来自 OutputConflictDetector，Task 6）
    void conflictsChanged();
    void currentSchemeNameChanged();
    void currentSchemeFilePathChanged();
    void isDirtyChanged();
    void canUndoChanged();
    void canRedoChanged();
    // v2.7.0 Phase 3.1：分组容器数据变化信号（loadFromFile 后 emit）
    void subChainGroupsChanged();
    void branchGroupsChanged();
    void parallelGroupsChanged();
    /// 通知 QML 某个节点被选中（PropertyPreviewPanel 响应）
    void nodeSelected(const QString& nodeId);
    /// 通知 QML 弹出模态编辑器
    void openEditorRequested(const QString& nodeId);
    /// 通知错误（QML 端转 toast）
    void errorRaised(const QString& phase, const QString& message);
    /// v2.5.0 修复：节点实时移动信号（拖拽中触发，不导致 currentNodesChanged 重建 delegate）
    /// QML 端 connectionsCanvas 监听此信号重绘连线，避免 Repeater 重建中断拖拽
    void nodePositionChanged(const QString& nodeId, qreal worldX, qreal worldY);
    /// v2.1.0 M4 引入：保存完成（QML 端 toast「已保存」）
    void saveFinished(const QString& filePath, bool success, const QString& message);
    /// v2.1.0 M4 引入：加载完成（QML 端 toast「已加载」；jsonText 由 QML 端读取，UI 层不再使用）
    void loadFinished(const QString& filePath, bool success,
                      const QString& message, const QString& jsonText);
    /// v2.2.0：收藏列表变化
    void favoritesChanged();

    /// 异步部署开始（runSchemeAsync 触发，UI 可显示忙碌指示）
    void schemeDeployStarted();
    /// 异步部署完成，result 同 runScheme 返回值
    void schemeDeployFinished(const QVariantMap& result);

    /// 异步单算子执行开始（runSingleOperatorAsync 触发）
    void singleOperatorStarted();
    /// 异步单算子执行完成，result 同 runSingleOperator 返回值
    void singleOperatorFinished(const QVariantMap& result);

    /// v6.x：节点运行后输出值（ports）已更新（QML 变量管理面板据此刷新输出参数值）
    void nodeOutputsUpdated();

    // === 算子流程导出信号 ===
    /// 导出进度更新（0-100）
    void exportProgress(int percent);
    /// 导出完成（success=true 时 message 含产物路径）
    void exportFinished(bool success, const QString& message);

private:
    void rebuildOperatorTypes();
    void rebuildCurrentNodes();
    /// v2.1.0 M4 引入：把加载得到的 JSON 文本应用到 m_currentNodes/m_connections
    /// @return true=成功；false=失败（错误通过 errorRaised 信号发出）
    bool applyLoadedJson(const QString& jsonText, const QString& filePath);

    // P1-3 typed ports：端口类型校验辅助
    /// 根据 nodeId 查找节点算子 type（节点不存在返回空字符串）
    QString nodeTypeById(const QString& nodeId) const;
    /// 校验 fromId:fromPort → toId:toPort 的端口类型兼容性
    /// @return true=兼容或任一端未声明端口（放行）；false=类型不兼容（应阻断）
    bool checkPortCompatible(const QString& fromId, const QString& fromPort,
                             const QString& toId,   const QString& toPort) const;

    // v2.5.0 功能 3/5：部署与单算子运行的内部辅助方法已迁移至 SchemeRunController
    // （buildToolChainFromNodes / computeUpstreamChain / collectUpstreamNodes /
    //   saveMatToTempPng），EditViewBridge 通过 m_schemeRunController 委托调用。

    QStringList  m_operatorTypes;
    QVariantList m_currentNodes;
    QVariantList m_connections;        ///< v2.1.0 M4 引入：节点连接列表 [{fromId,fromPort,toId,toPort}, ...]
    QString      m_currentSchemeName;
    QString      m_currentSchemeFilePath;   ///< v2.1.0 M4 引入：当前方案文件路径（保存/加载用）
    bool         m_isDirty = false;         ///< v2.1.0 M4 引入：方案是否被修改（保存/加载按钮 enabled 用）
    QUndoStack*  m_undoStack = nullptr;
    QString      m_selectedNodeId;
    QDV::UI::SchemeSerializer* m_serializer = nullptr;   ///< v2.1.0 M4 引入：异步 I/O 序列化器

    // v2.2.0：收藏/最近/常用追踪
    QSet<QString>   m_favorites;          ///< 收藏的算子 type 集合
    QStringList     m_recents;            ///< 最近使用（最多 10）
    QMap<QString,int> m_useCounts;        ///< 使用频次（type → 次数）

    // v2.5.0 预留：相机帧路径（供 runScheme 在 inputImagePath 为空时使用）
    QString         m_cameraFramePath;      ///< 相机帧临时文件路径

    // v2.6.0 变量管理与预览管理
    QDV::VariableManager*       m_variableManager = nullptr;       ///< 控制变量管理器
    QDV::ImageVariableManager*  m_imageVariableManager = nullptr;  ///< 图像变量管理器
    QDV::PreviewManager*        m_previewManager = nullptr;        ///< 预览管理器

    // v2.7.0 O1a：算子库桥接器
    OperatorLibraryBridge* m_operatorLibraryBridge = nullptr;     ///< 算子库桥接器

    // v2.7.0 Phase 3.1 Task 6：拆分出的专职协作者（生命周期跟随 bridge）
    ClipboardManager*      m_clipboardManager = nullptr;      ///< 算子参数剪贴板
    SchemeRunController*   m_schemeRunController = nullptr;   ///< 方案运行控制
    OutputConfigManager*   m_outputConfigManager = nullptr;   ///< 算子输出配置
    PortBindingManager*    m_portBindingManager = nullptr;    ///< 端口绑定数据模型
    OutputConflictDetector* m_conflictDetector = nullptr;     ///< 输入/输出项冲突检测引擎（Task 6）
    OperatorRecommender*   m_recommender = nullptr;           ///< 智能算子推荐引擎（spec Task 9）

    // v6.x：节点最近一次运行后的输出值缓存（nodeId → ToolResult.ports）
    QHash<QString, QVariantMap> m_nodeOutputs;
    mutable QMutex              m_nodeOutputsMutex;           ///< 保护 m_nodeOutputs（工作线程写入）

    // 算子流程导出编排器
    SchemeExporter* m_exporter = nullptr;
};

#endif // EDITVIEW_BRIDGE_H
