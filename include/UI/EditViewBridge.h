#ifndef EDITVIEW_BRIDGE_H
#define EDITVIEW_BRIDGE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>      // v2.2.0：收藏集合
#include <QMap>      // v2.2.0：使用频次
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

public:
    explicit EditViewBridge(QObject* parent = nullptr);
    ~EditViewBridge() override;

    // === C++ 端注入接口（由 EditView 在构造后调用） ===
    /// 注入撤销栈（可为 nullptr 表示禁用 undo/redo）
    void setUndoStack(QUndoStack* stack);
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
    Q_INVOKABLE bool hasClipParams() const { return m_clipHasData; }
    /// 剪贴板对应的算子类型（用于 UI 显示"来自 xxx 算子"）
    Q_INVOKABLE QString clipParamsType() const { return m_clipType; }

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
    Q_INVOKABLE bool isSingleOperatorRunning() const { return m_singleOpRunning; }

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
    /// 获取控制变量 JSON（保存方案时调用）
    Q_INVOKABLE QString variablesToJson() const;
    /// 加载控制变量 JSON（加载方案时调用）
    Q_INVOKABLE bool loadVariablesFromJson(const QString& jsonStr);

signals:
    void operatorsChanged();
    void currentNodesChanged();
    void connectionsChanged();
    void currentSchemeNameChanged();
    void currentSchemeFilePathChanged();
    void isDirtyChanged();
    void canUndoChanged();
    void canRedoChanged();
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

private:
    void rebuildOperatorTypes();
    void rebuildCurrentNodes();
    /// v2.1.0 M4 引入：把加载得到的 JSON 文本应用到 m_currentNodes/m_connections
    /// @return true=成功；false=失败（错误通过 errorRaised 信号发出）
    bool applyLoadedJson(const QString& jsonText, const QString& filePath);

    // v2.5.0 功能 3/5：部署与单算子运行的内部辅助方法
    /// 将 m_currentNodes 的子集转换为 VisionTool 列表（调用方拥有所有权，需手动释放）
    /// @param nodeIds 要转换的节点 ID 列表（按执行顺序）
    /// @return VisionTool 指针列表（失败时返回空列表，已创建的 tool 会被释放）
    QList<QDV::VisionTool*> buildToolChainFromNodes(const QStringList& nodeIds) const;
    /// 计算目标节点的上游链（拓扑有序，从最上游到目标节点本身）
    /// @param targetNodeId 目标节点 ID
    /// @return 拓扑有序的节点 ID 列表（含目标节点本身）
    QStringList computeUpstreamChain(const QString& targetNodeId) const;
    /// 递归收集上游节点（DFS，结果插入到 orderedSet 前面以实现拓扑序）
    void collectUpstreamNodes(const QString& nodeId, QStringList& result, QSet<QString>& visited) const;
    /// 保存 cv::Mat 到临时 PNG 文件，返回文件路径（供 QML 端显示）
    static QString saveMatToTempPng(const cv::Mat& image, const QString& prefix);

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

    // P1-B4-H6：参数剪贴板（单条，覆盖式）
    bool            m_clipHasData = false;  ///< 剪贴板是否有数据
    QString         m_clipType;             ///< 剪贴板参数来源的算子类型
    QVariantMap     m_clipParams;           ///< 剪贴板参数内容

    // v2.5.0 预留：相机帧路径（供 runScheme 在 inputImagePath 为空时使用）
    QString         m_cameraFramePath;      ///< 相机帧临时文件路径

    // v2.6.0 变量管理与预览管理
    QDV::VariableManager*       m_variableManager = nullptr;       ///< 控制变量管理器
    QDV::ImageVariableManager*  m_imageVariableManager = nullptr;  ///< 图像变量管理器
    QDV::PreviewManager*        m_previewManager = nullptr;        ///< 预览管理器

    // 异步部署执行（修复主线程阻塞）
    QFutureWatcher<QVariantMap>* m_schemeWatcher = nullptr;
    bool m_schemeRunning = false;

    // 异步单算子执行（交互式调参，修复主线程阻塞）
    QFutureWatcher<QVariantMap>* m_singleOpWatcher = nullptr;
    bool m_singleOpRunning = false;
};

#endif // EDITVIEW_BRIDGE_H
