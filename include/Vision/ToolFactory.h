#ifndef TOOLFACTORY_H
#define TOOLFACTORY_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <functional>
#include "VisionTool.h"

namespace QDV { class IInferenceEngine; }

// P1-B11 架构约束（文档化）：
// 当前 ToolFactory 的 registerTool 虽然是 public，但实际注册全部发生在编译期
// （通过 REGISTER_TOOL 宏的静态变量初始化），运行时无法从用户配置/插件加载自定义算子。
// 如需支持运行时自定义算子，需新增：
//   1. 从 plugins/ 目录动态加载 .dll 并调用 registerTool
//   2. UI 提供算子注册接口（脚本算子 / Python 算子）
//   3. operators.json 增加自定义算子元数据加载
// 用户已决策：P1-B 阶段只剔除不实现，自定义算子机制推迟到 P2+ 阶段。
class ToolFactory {
public:
    static ToolFactory* instance();

    void registerTool(const QString& type, std::function<QDV::VisionTool*()> creator);
    QDV::VisionTool* createTool(const QString& type);
    QStringList getAvailableToolTypes() const;

    // v5.3：注入 AI 推理引擎，createTool 创建 AiClassifyTool 时自动注入
    void setInferenceEngine(QDV::IInferenceEngine* engine);

private:
    ToolFactory();

    QMap<QString, std::function<QDV::VisionTool*()>> m_creators;
    QDV::IInferenceEngine* m_inferenceEngine = nullptr;
    static ToolFactory* s_instance;
};

#define REGISTER_TOOL(type, className) \
    static bool _registered_##className = []() { \
        ToolFactory::instance()->registerTool(type, []() -> QDV::VisionTool* { return new className(); }); \
        return true; \
    }()

#endif // TOOLFACTORY_H