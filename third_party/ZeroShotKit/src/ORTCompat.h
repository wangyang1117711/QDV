#ifndef ORTCOMPAT_H
#define ORTCOMPAT_H

// ============================================================================
// ORT 兼容性头文件
// MinGW GCC 的 specstrings.h 不完整，缺少部分 MSVC SAL 注解
// 在包含 onnxruntime_cxx_api.h 前引入本文件，补全缺失的注解宏
// ============================================================================

// MinGW 下 _stdcall 不是关键字，需映射到 GCC 的 __stdcall
// ORT 头文件中 ORT_API_CALL 定义为 _stdcall，若未定义会导致编译错误
#ifndef _stdcall
#define _stdcall __stdcall
#endif

// 补全 MinGW specstrings.h 中缺失的 SAL2 注解（定义为空宏）
#ifndef _Frees_ptr_opt_
#define _Frees_ptr_opt_
#endif

#ifndef _Ret_maybenull_
#define _Ret_maybenull_
#endif

#ifndef _Ret_notnull_
#define _Ret_notnull_
#endif

#ifndef _Check_return_
#define _Check_return_
#endif

#ifndef _Outptr_result_maybenull_
#define _Outptr_result_maybenull_
#endif

#ifndef _Outptr_result_buffer_maybenull_
#define _Outptr_result_buffer_maybenull_(X)
#endif

#ifndef _In_reads_
#define _In_reads_(X)
#endif

#ifndef _Inout_updates_
#define _Inout_updates_(X)
#endif

#ifndef _Out_writes_
#define _Out_writes_(X)
#endif

#ifndef _Inout_updates_all_
#define _Inout_updates_all_(X)
#endif

#ifndef _Out_writes_bytes_all_
#define _Out_writes_bytes_all_(X)
#endif

#ifndef _Out_writes_all_
#define _Out_writes_all_(X)
#endif

#ifndef _Success_
#define _Success_(X)
#endif

#endif // ORTCOMPAT_H
