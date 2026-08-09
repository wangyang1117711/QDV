#ifndef QDV_VARIABLE_MANAGER_H
#define QDV_VARIABLE_MANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QRecursiveMutex>
#include <QRectF>

namespace QDV {

/**
 * @brief 控制变量管理器（v2.7.0 - 海康VM对齐升级）
 *
 * 功能：
 * 1. 管理数值型(int/double)、字符串型(string)、布尔型(bool)、ROI型变量
 * 2. 支持 CRUD：创建/查询/更新/删除
 * 3. 变量绑定算子参数：参数值可为 "${varName}" 形式，运行时解析替换
 * 4. 变量变更信号：修改变量 → 发信号 → PreviewManager 自动重运行
 * 5. 随方案保存加载：serialize/deserialize
 *
 * 设计要点（v2.7.0 升级）：
 * - 线程安全：所有读写方法用 QMutex 保护，支持 PreviewManager 子线程调用
 * - 变量类型扩展：新增 ROI/Region/Points 类型（对齐海康VM手册要求）
 * - 变量名规范：字母+数字+下划线，首字符必须为字母或下划线
 * - 变量名唯一：重复创建返回 false
 */
class VariableManager : public QObject {
    Q_OBJECT
public:
    /// 变量类型（v2.7.0：新增 ROI/Region/Points）
    enum class Type {
        Int,        ///< 整数型
        Double,     ///< 浮点型
        String,     ///< 字符串型
        Bool,       ///< 布尔型
        Roi,        ///< ROI 区域型（x,y,w,h）- 对齐 VM 手册"全局变量自定义工具"
        Region,     ///< Region 掩码型（二值图区域，与 PortType::Region 对齐）
        Points      ///< 点集型（QPointF 列表，与 PortType::Points 对齐）
    };
    Q_ENUM(Type)

    /// 变量结构
    struct Variable {
        QString name;       ///< 变量名（唯一）
        Type type;          ///< 类型
        QVariant value;     ///< 当前值
        QString description;///< 描述（可选）
    };

    explicit VariableManager(QObject* parent = nullptr);
    ~VariableManager() override;

    // === CRUD ===
    /// 创建变量（变量名必须唯一，符合命名规范）
    /// @return true=成功；false=变量名已存在或非法
    Q_INVOKABLE bool createVariable(const QString& name, const QString& typeStr,
                                     const QVariant& value, const QString& description = QString());
    /// 删除变量
    Q_INVOKABLE bool removeVariable(const QString& name);
    /// 修改变量值（类型必须匹配）
    /// @return true=成功；false=变量不存在或类型不匹配
    Q_INVOKABLE bool setValue(const QString& name, const QVariant& value);
    /// 修改变量描述
    Q_INVOKABLE bool setDescription(const QString& name, const QString& description);
    /// 获取变量值
    Q_INVOKABLE QVariant value(const QString& name) const;
    /// 获取变量完整信息（{name, type, value, description}）
    Q_INVOKABLE QVariantMap variable(const QString& name) const;
    /// 获取所有变量列表
    Q_INVOKABLE QVariantList variables() const;
    /// 变量数量
    Q_INVOKABLE int count() const;
    /// 是否存在指定变量
    Q_INVOKABLE bool exists(const QString& name) const;
    /// 清空所有变量
    Q_INVOKABLE void clear();

    // === 变量绑定解析 ===
    /// 解析参数值中的变量引用 "${varName}"
    /// @param input 输入字符串（可为 "${thresh}" 或 "prefix_${var}_suffix"）
    /// @param ok 是否全部解析成功（false 表示有未定义变量）
    /// @return 解析后的字符串；若 input 不含 ${} 则原样返回
    Q_INVOKABLE QString resolveBinding(const QString& input, bool* ok = nullptr) const;

    /// 解析 QVariant 中的变量引用（递归处理 Map/List/String）
    /// @return 解析后的 QVariant；若含未定义变量则返回原值
    Q_INVOKABLE QVariant resolveVariant(const QVariant& input) const;

    /// 提取字符串中所有 ${varName} 引用的变量名列表
    Q_INVOKABLE QStringList extractReferences(const QString& input) const;

    // === 序列化 ===
    /// 序列化为 JSON 数组（保存方案时调用）
    QJsonArray toJson() const;
    /// 从 JSON 数组反序列化（加载方案时调用；会清空现有变量）
    bool fromJson(const QJsonArray& arr);

    // === v5.4 算子输出 → 全局变量映射 ===
    /// v5.4：注册算子输出为全局变量
    /// @param nodeId 算子节点 ID（UUID）
    /// @param outputName 输出参数名（如 "className"）
    /// @param typeName 输出类型（"int"/"string"/"double"/"string[]"/"double[]"）
    /// 注册后变量名为 "<nodeId>.<outputName>"，下游算子可通过此名引用
    /// 注意：算子输出变量名含点和连字符（UUID），不经过 isValidName 校验，
    ///       直接写入 m_variables（与用户变量分离，不破坏现有命名规范）
    void registerOperatorOutput(const QString& nodeId, const QString& outputName,
                                const QString& typeName = QStringLiteral("string"));

    /// v5.4：反注册算子输出变量
    /// @param nodeId 算子节点 ID
    /// @param outputName 输出参数名；若为空，反注册该节点的所有输出
    void unregisterOperatorOutput(const QString& nodeId, const QString& outputName = QString());

    /// v5.4：更新已注册算子输出变量的值（ToolChain 执行后调用）
    /// @param nodeId 算子节点 ID
    /// @param outputs 输出字段 -> 值的映射（来自 ToolResult.data）
    /// 仅更新已注册的变量，不主动创建新变量
    void updateOperatorOutputValues(const QString& nodeId, const QJsonObject& outputs);

    /// v5.4：查询某算子输出是否已注册
    bool isOperatorOutputRegistered(const QString& nodeId, const QString& outputName) const;

    // === 类型工具 ===
    static QString typeToString(Type t);
    static Type stringToType(const QString& s, bool* ok = nullptr);
    static bool isValidName(const QString& name);

    // === v2.7.0 ROI/Region/Points 辅助方法 ===
    /// 将 ROI 变量值转为 QRectF（x,y,w,h）
    static QRectF toRoi(const QVariant& v);
    /// 将 QRectF 转为 ROI 变量值（QVariantMap: {x,y,w,h}）
    static QVariant fromRoi(const QRectF& r);
    /// 将点集变量值转为 QPointF 列表
    static QList<QPointF> toPoints(const QVariant& v);
    /// 将 QPointF 列表转为点集变量值
    static QVariant fromPoints(const QList<QPointF>& pts);

signals:
    /// 变量被创建
    void variableCreated(const QString& name);
    /// 变量被删除
    void variableRemoved(const QString& name);
    /// 变量值被修改（触发 PreviewManager 重运行）
    void valueChanged(const QString& name, const QVariant& newValue);
    /// 变量描述被修改
    void descriptionChanged(const QString& name, const QString& newDesc);
    /// 整体变更（清空/批量加载后触发，QML 端刷新列表）
    void variablesChanged();

private:
    /// v2.7.0：线程安全递归互斥锁，保护所有读写操作
    /// 背景：PreviewManager::doPreview 通过 QtConcurrent::run 在子线程执行算子，
    ///       算子内调用 VariableManager 时若无线程保护会导致 QHash 崩溃
    /// 使用 QRecursiveMutex：因 resolveVariant 内部调用 resolveBinding（同线程二次加锁）
    mutable QRecursiveMutex m_mutex;
    QHash<QString, Variable> m_variables;
};

} // namespace QDV

#endif // QDV_VARIABLE_MANAGER_H
