#ifndef EDITVIEW_BRIDGE_H
#define EDITVIEW_BRIDGE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>      // v2.2.0：收藏集合
#include <QMap>      // v2.2.0：使用频次

class QUndoStack;
class QJsonObject;   // v2.1.0 M4：方案加载后解析用
namespace QDV { namespace UI { class SchemeSerializer; } }

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
    /// v2.1.0 M4 引入：手动触发 currentNodesChanged 信号（UndoCommand 在多次原子操作时避免重复信号）
    void notifyCurrentNodesChanged() { emit currentNodesChanged(); }
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
    /// 保存当前方案到文件（异步）
    /// @param filePath 目标 .json 文件路径
    void saveToFile(const QString& filePath);
    /// 加载方案文件（异步）
    /// @param filePath 源 .json 文件路径
    void loadFromFile(const QString& filePath);
    /// 新建空方案
    void newScheme();
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
    /// v2.1.0 M4 引入：保存完成（QML 端 toast「已保存」）
    void saveFinished(const QString& filePath, bool success, const QString& message);
    /// v2.1.0 M4 引入：加载完成（QML 端 toast「已加载」；jsonText 由 QML 端读取，UI 层不再使用）
    void loadFinished(const QString& filePath, bool success,
                      const QString& message, const QString& jsonText);
    /// v2.2.0：收藏列表变化
    void favoritesChanged();

private:
    void rebuildOperatorTypes();
    void rebuildCurrentNodes();
    /// v2.1.0 M4 引入：把加载得到的 JSON 文本应用到 m_currentNodes/m_connections
    /// @return true=成功；false=失败（错误通过 errorRaised 信号发出）
    bool applyLoadedJson(const QString& jsonText, const QString& filePath);

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
};

#endif // EDITVIEW_BRIDGE_H
