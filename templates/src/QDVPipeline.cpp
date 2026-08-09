// QDVPipeline.cpp - wrapper DLL 实现（纯 C 接口）
// 内部用 QDVRuntime 加载方案并执行算子流程

#include "../include/QDVPipeline.h"
#include "QDVRuntime.h"

#include <string>
#include <new>

// 线程局部字符串缓冲（下次同线程调用失效，调用方需立即拷贝）
static thread_local std::string g_stringBuf;
static thread_local std::string g_lastError;

struct QDVPipelineHandle {
    QDVRuntime runtime;
};

QDVP_EXPORT QDVPipelineHandle* QDVP_Pipeline_Create(void)
{
    try {
        return new (std::nothrow) QDVPipelineHandle();
    } catch (...) {
        return nullptr;
    }
}

QDVP_EXPORT QDVPResult QDVP_Pipeline_Load(QDVPipelineHandle* h, const char* schemePath)
{
    if (!h || !schemePath) return QDVP_ERR_INVALID_ARG;
    if (h->runtime.loadScheme(QString::fromUtf8(schemePath))) {
        return QDVP_OK;
    }
    g_lastError = h->runtime.lastError().toStdString();
    return QDVP_ERR_LOAD_SCHEME;
}

QDVP_EXPORT QDVPResult QDVP_Pipeline_SetInputImage(QDVPipelineHandle* h, const char* imagePath)
{
    if (!h || !imagePath) return QDVP_ERR_INVALID_ARG;
    if (h->runtime.setInputImage(QString::fromUtf8(imagePath))) {
        return QDVP_OK;
    }
    g_lastError = h->runtime.lastError().toStdString();
    return QDVP_ERR_INVALID_ARG;
}

QDVP_EXPORT QDVPResult QDVP_Pipeline_Run(QDVPipelineHandle* h)
{
    if (!h) return QDVP_ERR_INVALID_ARG;
    if (h->runtime.run()) {
        return QDVP_OK;
    }
    g_lastError = h->runtime.lastError().toStdString();
    return QDVP_ERR_RUN;
}

QDVP_EXPORT const char* QDVP_Pipeline_GetResultJson(QDVPipelineHandle* h)
{
    if (!h) return "{}";
    g_stringBuf = h->runtime.getResultJson().toStdString();
    return g_stringBuf.c_str();
}

QDVP_EXPORT const char* QDVP_Pipeline_GetResult(QDVPipelineHandle* h, const char* resultName)
{
    if (!h || !resultName) return "{}";
    g_stringBuf = h->runtime.getResult(QString::fromUtf8(resultName)).toStdString();
    return g_stringBuf.c_str();
}

QDVP_EXPORT QDVPResult QDVP_Pipeline_GetOutputImage(QDVPipelineHandle* h, const char* resultName, const char* savePath)
{
    if (!h || !resultName || !savePath) return QDVP_ERR_INVALID_ARG;
    if (h->runtime.saveOutputImage(QString::fromUtf8(resultName), QString::fromUtf8(savePath))) {
        return QDVP_OK;
    }
    return QDVP_ERR_NO_RESULT;
}

QDVP_EXPORT void QDVP_Pipeline_Destroy(QDVPipelineHandle* h)
{
    delete h;
}

QDVP_EXPORT const char* QDVP_GetVersion(void)
{
    g_stringBuf = QDVRuntime::version().toStdString();
    return g_stringBuf.c_str();
}

QDVP_EXPORT const char* QDVP_GetLastError(QDVPipelineHandle* h)
{
    if (h) {
        g_lastError = h->runtime.lastError().toStdString();
    }
    return g_lastError.c_str();
}
