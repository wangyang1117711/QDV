#ifndef PROMPT_LIBRARY_H
#define PROMPT_LIBRARY_H

/**
 * @file PromptLibrary.h
 * @brief 共享提示词库（v2.0 阶段二 Task 5）
 *
 * 设计目标：
 * - 跨 AI 算子共享的提示词库，支持 builtin(12 类默认)/custom(用户添加)/recent(最近使用) 三节
 * - 单例模式，线程安全（QMutex 互斥）
 * - 加载/保存 JSON 配置（config/prompt_library.json）
 * - 供 ZeroShotDetectTool / YoloDetect / AiClassify 等 AI 算子复用
 *
 * 与 OperatorDescriptors 的关系：
 * - ParamSpec.promptLibrarySource 字段标记某参数从提示词库取值
 * - UI 层（QML ParamForm 或 QWidget）据此渲染多选下拉
 *
 * 与 ZeroShotPanel 的关系：
 * - ZeroShotPanel 通过依赖注入接收 PromptLibrary 数据（QVariantList）
 * - 由 ZeroShotDetectView 桥接，保持 ZeroShotKit CMake 独立性
 */

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVariantMap>
#include <QVariantList>
#include <QPair>
#include <QMutex>
#include <QJsonObject>
#include <QJsonArray>

namespace QDV {

/// 提示词条目来源
enum class PromptSource {
    Builtin,  ///< 内置（12 类默认缺陷，不可删除）
    Custom,   ///< 用户自定义
    Recent    ///< 最近使用
};

/// 单个提示词条目
struct PromptEntry {
    QString       name;            ///< 内部名（"scratch"）
    QString       prompt;          ///< 英文提示词（"scratch"，可含空格如 "missing part"）
    QString       cnName;          ///< 中文显示名（"划痕"）
    QString       scene;           ///< 适用场景描述
    QString       exampleImagePath;///< 可选示例图路径（仅元数据，不参与推理）

    // v2.0 阶段五 Task 13：第二层 训练兑底流程标记
    // 当该类别通过训练产出了专用 YOLO 模型时，hasSpecializedModel=true
    // specializedModelPath 指向算子类型（如 "YoloTrained_rust_v1"），供 UI 提示用户切换
    // 注意：specializedModelPath 存的是算子 type 或模型名（用于显示与切换提示），
    //       实际推理路径由算子自身 modelPath 参数锁定（由 OperatorLibraryController 注册时写入）
    bool          hasSpecializedModel = false;     ///< 是否已训练专用模型
    QString       specializedModelPath;            ///< 专用模型算子 type 或路径（仅元数据）

    PromptSource  source = PromptSource::Builtin;

    /// 序列化为 QVariantMap（UI/JSON 友好）
    QVariantMap toMap() const;
    /// 从 QVariantMap 反序列化
    static PromptEntry fromMap(const QVariantMap& m, PromptSource source = PromptSource::Builtin);
    /// source → 字符串（"builtin"/"custom"/"recent"）
    static QString sourceToString(PromptSource s);
    /// 字符串 → source
    static PromptSource sourceFromString(const QString& s);
};

/**
 * @brief 共享提示词库（单例，线程安全）
 *
 * 加载策略：
 * - 默认从 <applicationDirPath>/config/prompt_library.json 加载
 * - 文件不存在/解析失败 → 回退到内置 12 类默认（builtinDefaults()）
 * - 保存时写回原路径（目录不存在则创建）
 *
 * recent 节策略：
 * - 记录最近使用的提示词组合（整条拼接串，如 "scratch . dent"）
 * - 最多 10 条，去重（新条目置顶，超限丢弃尾部）
 */
class PromptLibrary : public QObject {
    Q_OBJECT

public:
    /// 单例获取
    static PromptLibrary* instance();

    /// 最近使用上限
    static constexpr int RECENT_MAX = 10;

    // ------------------------------------------------------------------
    // 加载 / 保存
    // ------------------------------------------------------------------

    /// 默认配置路径：<applicationDirPath>/config/prompt_library.json
    static QString defaultConfigPath();

    /// 从 JSON 文件加载（成功返回 true）。失败时回退到 builtinDefaults()，不抛异常
    /// @param path  绝对或相对路径；为空时使用 defaultConfigPath()
    /// @param outError  失败时写入原因
    bool loadFromJson(const QString& path = QString(), QString* outError = nullptr);

    /// 保存到 JSON 文件（成功返回 true）
    /// @param path  为空时使用 defaultConfigPath()
    bool saveToJson(const QString& path = QString());

    // ------------------------------------------------------------------
    // 查询
    // ------------------------------------------------------------------

    /// 内置 12 类默认缺陷（静态，不依赖加载状态）
    static QList<PromptEntry> builtinDefaults();

    /// 当前 builtin 节（加载后或默认）
    QList<PromptEntry> builtin() const;
    /// 当前 custom 节
    QList<PromptEntry> custom() const;
    /// 当前 recent 节
    QList<PromptEntry> recent() const;

    /// 合并所有条目（builtin + custom + recent），供 UI 多选下拉使用
    /// 每项含 source 字段，UI 可据此分组显示
    QVariantList allEntriesAsVariant() const;

    /// 按 prompt 文本查找条目（不区分 builtin/custom/recent）
    PromptEntry findByPrompt(const QString& prompt) const;

    // ------------------------------------------------------------------
    // 修改（线程安全，自动持久化到 m_loadedPath）
    // ------------------------------------------------------------------

    /// 添加自定义类别
    /// @param name  内部名（非空，重复时覆盖同名 custom 条目）
    /// @param prompt  英文提示词（非空）
    /// @param cnName  中文名（可选）
    /// @param scene  适用场景（可选）
    /// @param exampleImagePath  示例图路径（可选）
    /// @return 成功返回 true（name/prompt 为空或与 builtin 冲突时返回 false）
    bool addCustom(const QString& name, const QString& prompt,
                   const QString& cnName = QString(),
                   const QString& scene = QString(),
                   const QString& exampleImagePath = QString());

    /// 记录最近使用的提示词组合（去重，最多 10 条，新条目置顶）
    /// @param prompts  本次使用的提示词列表（如 ["scratch", "dent"]）
    void addRecent(const QStringList& prompts);

    /// 清空 recent 节（不影响 builtin/custom）
    void clearRecent();

    /// 删除指定 name 的 custom 条目（builtin 不可删）
    bool removeCustom(const QString& name);

    // ==================================================================
    // v2.0 阶段五 Task 13：专用模型标记（第二层 训练兑底流程）
    // ==================================================================

    /// 为指定 prompt 标记已训练专用模型（训练完成后调用）
    /// @param prompt       类别的英文提示词（如 "rust"）
    /// @param modelPath    专用模型算子 type 或模型名（如 "YoloTrained_rust_v1"）
    /// @return 成功返回 true（找不到对应 prompt 时返回 false）
    /// 找到条目后会在 builtin/custom 中标记（recent 不标记，因为 recent 是组合）
    bool setSpecializedModel(const QString& prompt, const QString& modelPath);

    /// 清除指定 prompt 的专用模型标记（用户撤销/重训时调用）
    bool clearSpecializedModel(const QString& prompt);

    /// 查询指定 prompt 是否已标记专用模型，返回 {has, modelPath}
    QPair<bool, QString> querySpecializedModel(const QString& prompt) const;

    /// 当前已加载的配置文件路径（loadFromJson 后有效）
    QString loadedPath() const { return m_loadedPath; }

signals:
    /// 库内容变化（addCustom/addRecent/removeCustom/clearRecent/loadFromJson/setSpecializedModel 后发射）
    void libraryChanged();

    /// v2.0 阶段五 Task 13：专用模型标记变化信号
    /// @param prompt       类别的英文提示词
    /// @param hasModel     true=已标记专用模型，false=已清除
    /// @param modelPath    专用模型算子 type 或路径（hasModel=false 时为空）
    void specializedModelChanged(const QString& prompt, bool hasModel, const QString& modelPath);

private:
    PromptLibrary(QObject* parent = nullptr);

    // 线程安全：所有读写加锁
    mutable QMutex m_mutex;

    QList<PromptEntry> m_builtin;   ///< 内置 12 类
    QList<PromptEntry> m_custom;    ///< 用户自定义
    QList<PromptEntry> m_recent;    ///< 最近使用（最多 10 条）

    QString m_loadedPath;           ///< 已加载的配置文件路径

    static PromptLibrary* s_instance;
    static QMutex         s_instanceMutex;

    // 内部：将 PromptEntry 列表转为 QJsonArray
    static QJsonArray entriesToJson(const QList<PromptEntry>& entries);
    // 内部：从 QJsonArray 解析 PromptEntry 列表
    static QList<PromptEntry> jsonToEntries(const QJsonArray& arr, PromptSource source);
};

} // namespace QDV

#endif // PROMPT_LIBRARY_H
