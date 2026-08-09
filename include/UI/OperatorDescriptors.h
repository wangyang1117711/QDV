#ifndef OPERATOR_DESCRIPTORS_H
#define OPERATOR_DESCRIPTORS_H

/**
 * @file OperatorDescriptors.h
 * @brief 算子元数据（v2.1.0 M3 引入）
 *
 * 设计目标：
 * - 数据驱动 QML 端动态表单（ParamForm.qml 按 ParamSpec.type 切换 7 种 Component）
 * - 与 src/Vision/*Tool.cpp 解耦（AI 零侵入）：元数据硬编码在此，不反射 C++ 算子
 * - 通过 OperatorDescriptors 单例提供查询接口（all / get / categories / byCategory）
 *
 * 不在本期：
 * - 算子元数据自动反射（需修改 src/Vision/* 违反 AI 零侵入）
 * - 算子元数据 JSON 配置文件（YAGNI：硬编码足够）
 */

#include <QString>
#include <QStringList>
#include <QList>
#include <QVariant>
#include <QMap>

namespace QDV {
namespace UI {

/// 算子参数类型（7 种，对应 ParamForm.qml 的 7 种 Component）
enum class ParamType {
    Int,        ///< 整数（SpinBox）
    Float,      ///< 浮点（DoubleSpinBox）
    Enum,       ///< 枚举（ComboBox）
    Bool,       ///< 布尔（CheckBox）
    String,     ///< 文本（TextField）
    ROI,        ///< 感兴趣区域（ROISelector 子控件）
    Vector,     ///< 向量/列表（VectorField，逗号分隔数字或 JSON 数组）
};

/// 单个算子参数的元数据（驱动 QML 表单一个控件）
struct ParamSpec {
    QString      name;          ///< 内部名（"threshold"）
    QString      cnName;        ///< 中文显示名（"阈值"）
    ParamType    type = ParamType::String;
    QVariant     defaultValue;  ///< 默认值
    QVariant     minValue;      ///< 数值型最小值（Int/Float 用）
    QVariant     maxValue;      ///< 数值型最大值
    QVariant     step;          ///< 数值型步长
    QStringList  options;       ///< Enum 类型的可选项（"方法1" "方法2"）
    QStringList  optionKeys;    ///< Enum 类型的内部 key（"method1" "method2"，与 options 一一对应）
    QString      help;          ///< 帮助气泡文本
    QString      unit;          ///< 单位（"px" "ms" "%"）

    // v2.0 阶段二 Task 6：AI 算子参数支持提示词库 Enum
    // 非空时（如 "textPrompts"/"categoryLabels"），UI 下拉从 PromptLibrary 取值
    // 含义：标记该 String/Vector 类型参数的取值来源是共享提示词库
    //   - UI 层据此渲染"目标类型多选下拉"（builtin 12 类 + custom + recent）
    //   - 多选后用 " . " 拼接为提示词串填入参数值
    //   - 空字符串=普通参数（默认，向后兼容）
    QString      promptLibrarySource;  ///< 提示词库源标记（"textPrompts"/"categoryLabels" 等，空=普通参数）

    /// 序列化为 QVariantMap（QML 端 JS 友好）
    QVariantMap toMap() const;
    /// 从 QVariantMap 反序列化（QML 端回传用）
    static ParamSpec fromMap(const QVariantMap& m);
};

/// 算子整体元数据
struct OperatorMeta {
    QString             type;        ///< 内部类型（"ImagePreprocess"，与 VisionTool::type() 一致）
    QString             cnName;      ///< 中文显示名（"图像预处理"）
    QString             category;    ///< 分类（"输入" "预处理" "检测" "几何" "变换" "分支" "AI"）
    QString             subGroup;    ///< v2.2.0：子分组（如预处理下的"空间滤波""频域滤波"，空=无子分组）
    QString             iconPath;    ///< 图标路径（"qrc:/icons/preprocess.png"，暂无图时为空）
    QString             description; ///< 简要描述（帮助气泡用）
    QList<ParamSpec>    params;      ///< 参数列表
    QList<QVariantMap>  outputs;     ///< v3.0.0：输出参数列表，每项 {name, cnName, typeName, desc, color}
                                     ///< v5.4.0 升级（AI 分类输出扩展）：
                                     ///<   - defaultEnabled (bool, 默认 true)：该输出是否默认启用
                                     ///<     单目标必填项（classId/className/confidence）为 true；
                                     ///<     多目标专属项（classArray/confidenceArray）为 false
                                     ///<   - multiTargetOnly (bool, 默认 false)：是否仅多目标场景下可用
                                     ///<     true 表示该项仅在多目标检测场景下输出，单目标场景应隐藏
                                     ///< 输出项配置增强（spec：editor-output-connection-optimization）：
                                     ///<   - group (string, 默认取 typeName)：输出项分组名，用于分类展示
                                     ///<   - alias (string, 默认取 cnName)：输出项别名，用于显示/重命名
                                     ///<   - priority (int, 默认 100)：冲突消歧优先级，越小越优先
                                     ///<   以上字段在 OperatorMeta::toMap/fromMap 中统一归一化，缺失回退默认值

    /// 序列化为 QVariantMap（QML 端 JS 友好）
    QVariantMap toMap() const;
    /// 从 QVariantMap 反序列化
    static OperatorMeta fromMap(const QVariantMap& m);
};

/// 算子元数据注册中心（单例）
class OperatorDescriptors {
public:
    /// 获取所有算子元数据（按 category 排序，同 category 内按 cnName 排序）
    static QList<OperatorMeta> all();
    /// 获取所有分类（按出现顺序去重）
    static QStringList categories();
    /// 按 type 查询单个算子元数据（不存在返回空 OperatorMeta）
    static OperatorMeta get(const QString& type);
    /// 按 category 查询算子列表
    static QList<OperatorMeta> byCategory(const QString& category);
    /// v2.2.0：按关键字搜索算子（匹配 cnName、type、description、category）
    static QList<OperatorMeta> search(const QString& keyword);
    /// v2.2.0：获取指定分类下的所有子分组名（去重排序）
    static QStringList subGroups(const QString& category);
    /// 检查 type 是否存在
    static bool has(const QString& type);
    /// 获取所有算子 type（用于 EditViewBridge::operatorTypes 暴露给 QML）
    static QStringList allTypes();

    // v2.1.0 M5：算子元数据外部化（config/operators.json）
    /// 默认 config 路径（exe 同级 config/operators.json）
    static QString defaultConfigPath();
    /// 从 JSON 文件加载算子元数据（成功返回 true）。失败时回退到 buildRegistry()，不抛异常
    /// @param path  绝对或相对路径
    /// @param outError  失败时写入原因
    static bool loadFromJson(const QString& path, QString* outError = nullptr);
    /// 把当前 buildRegistry() 序列化为 JSON 写入 path（成功返回 true）
    /// 用于 config 初次生成 / 重置。生成的是 indented UTF-8
    static bool exportToJson(const QString& path);
    /// 重置单例缓存（测试/手动 reload 用）
    static void reset();

    /// Phase 2: 注册外部动态算子元数据（由 OperatorPluginLoader 调用）
    /// 不重复添加同 type；成功返回 true
    static bool registerExternalOperator(const OperatorMeta& meta);

private:
    /// 内部注册表（首次调用时初始化）
    static QList<OperatorMeta> buildRegistry();
    /// 单例缓存
    static QList<OperatorMeta> s_registry;
    static bool                s_initialized;
    /// Phase 2: 外部动态算子元数据（与 s_registry 分离）
    static QList<OperatorMeta> s_externalRegistry;
};

} // namespace UI
} // namespace QDV

#endif // OPERATOR_DESCRIPTORS_H
