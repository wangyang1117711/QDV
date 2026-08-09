#ifndef QDVPIPELINE_H
#define QDVPIPELINE_H

/**
 * @file QDVPipeline.h
 * @brief QDV 算子流程导出 - C 接口（extern "C" __cdecl）
 *
 * 纯 C 接口、opaque handle 风格，C/C++/C#/Python ctypes 均可调用。
 * 符合海康 MVS 模式：方案文件 + 运行时包 + 多语言 wrapper。
 *
 * 调用流程：
 *   1. QDVP_Pipeline_Create()  创建 handle
 *   2. QDVP_Pipeline_Load()    加载方案文件
 *   3. QDVP_Pipeline_SetInputImage()  设置输入图
 *   4. QDVP_Pipeline_Run()     执行流程
 *   5. QDVP_Pipeline_GetResultJson()  获取结果
 *   6. QDVP_Pipeline_Destroy() 释放 handle
 *
 * 注意：字符串返回值（GetResultJson/GetResult/GetLastError/GetVersion）
 *       为内部线程局部缓冲，下次同线程调用失效；调用方需立即拷贝。
 */

#ifdef QDVPIPELINE_EXPORTS
  #define QDVP_EXPORT __declspec(dllexport)
#else
  #define QDVP_EXPORT __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QDVP_OK = 0,
    QDVP_ERR_INVALID_ARG = 1,
    QDVP_ERR_LOAD_SCHEME = 2,
    QDVP_ERR_RUN = 3,
    QDVP_ERR_NO_RESULT = 4,
    QDVP_ERR_INTERNAL = 99,
} QDVPResult;

typedef struct QDVPipelineHandle QDVPipelineHandle;

QDVP_EXPORT QDVPipelineHandle* QDVP_Pipeline_Create(void);
QDVP_EXPORT QDVPResult         QDVP_Pipeline_Load(QDVPipelineHandle* h, const char* schemePath);
QDVP_EXPORT QDVPResult         QDVP_Pipeline_SetInputImage(QDVPipelineHandle* h, const char* imagePath);
QDVP_EXPORT QDVPResult         QDVP_Pipeline_Run(QDVPipelineHandle* h);
QDVP_EXPORT const char*        QDVP_Pipeline_GetResultJson(QDVPipelineHandle* h);
QDVP_EXPORT const char*        QDVP_Pipeline_GetResult(QDVPipelineHandle* h, const char* resultName);
QDVP_EXPORT QDVPResult         QDVP_Pipeline_GetOutputImage(QDVPipelineHandle* h, const char* resultName, const char* savePath);
QDVP_EXPORT void               QDVP_Pipeline_Destroy(QDVPipelineHandle* h);
QDVP_EXPORT const char*        QDVP_GetVersion(void);
QDVP_EXPORT const char*        QDVP_GetLastError(QDVPipelineHandle* h);

#ifdef __cplusplus
}
#endif

#endif /* QDVPIPELINE_H */
