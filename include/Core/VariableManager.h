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

namespace QDV {

/**
 * @brief 控制变量管理器（v2.6.0）
 *
 * 功能：
 * 1. 管理数值型(int/double)、字符串型(string)、布尔型(bool)变量
 * 2. 支持 CRUD：创建/查询/更新/删除
 * 3. 变量绑定算子参数：参数值可为 "${varName}" 形式，运行时解析替换
 * 4. 变量变更信号：修改变量 → 发信号 → PreviewManager 自动重运行
 * 5. 随方案保存加载：serialize/deserialize
 *
 * 设计要点：
 * - 线程安全：所有方法在主线程调用（Qt 信号槽机制）
 * - 变量名规范：字母+数字+下划线，首字符必须为字母或下划线
 * - 变量名唯一：重复创建返回 false
 */
class VariableManager : public QObject {
    Q_OBJECT
public:
    /// 变量类型
    enum class Type {
        Int,        ///< 整数型
        Double,     ///< 浮点型
        String,     ///< 字符串型
        Bool        ///< 布尔型
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

    // === 类型工具 ===
    static QString typeToString(Type t);
    static Type stringToType(const QString& s, bool* ok = nullptr);
    static bool isValidName(const QString& name);

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
    QHash<QString, Variable> m_variables;
};

} // namespace QDV

#endif // QDV_VARIABLE_MANAGER_H
