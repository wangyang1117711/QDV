#ifndef ZEROSHOTKIT_LOGGER_H
#define ZEROSHOTKIT_LOGGER_H

// ============================================================================
// ZeroShotKit 日志适配器
// 默认输出到 stdout，集成方可通过 CMake 定义覆盖：
//   target_compile_definitions(ZeroShotKitCore PRIVATE
//     "ZSU_LOG_INFO(msg)=MyLogger::info(msg)")
//
// 注意：宏参数 msg 可以是 QString 或 const char* 字符串字面量。
//       内部用 QString(msg) 构造临时对象，兼容两种类型。
// ============================================================================

#include <QString>
#include <cstdio>

#ifndef ZSU_LOG_INFO
#define ZSU_LOG_INFO(msg)  std::printf("[ZeroShotKit][INFO] %s\n", QString(msg).toUtf8().constData())
#endif

#ifndef ZSU_LOG_WARN
#define ZSU_LOG_WARN(msg)  std::printf("[ZeroShotKit][WARN] %s\n", QString(msg).toUtf8().constData())
#endif

#ifndef ZSU_LOG_ERROR
#define ZSU_LOG_ERROR(msg) std::printf("[ZeroShotKit][ERROR] %s\n", QString(msg).toUtf8().constData())
#endif

#ifndef ZSU_LOG_DEBUG
#define ZSU_LOG_DEBUG(msg) std::printf("[ZeroShotKit][DEBUG] %s\n", QString(msg).toUtf8().constData())
#endif

#endif // ZEROSHOTKIT_LOGGER_H
