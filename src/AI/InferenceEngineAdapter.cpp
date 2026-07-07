// InferenceEngineAdapter 实现文件
//
// 所有方法均为内联实现（见 InferenceEngineAdapter.h），本 .cpp 仅用于：
// 1. 确保 InferenceEngineAdapter 的 TU（翻译单元）存在于 AI 模块中
// 2. 满足 CMakeLists.txt 中 AI_SOURCES 列表对 .cpp 文件的要求
// 3. 未来若需添加非模板/非内联方法（如统计、缓存），在此扩展
//
// 注：所有方法的 nullptr 检查在头文件内联实现中已完成（RT-006 RT-007）

#include "AI/InferenceEngineAdapter.h"

namespace QDV {

// 占位：当前 InferenceEngineAdapter 全部内联，无额外实现
// 后续扩展点：
// - 引用计数 / 共享所有权
// - 调用日志 / 性能埋点
// - 多后端切换管理

} // namespace QDV
