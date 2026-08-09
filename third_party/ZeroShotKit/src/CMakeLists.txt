# ============================================================================
# ZeroShotKit Core 层 - 纯推理库，无 UI 依赖
# ============================================================================

set(ZSU_CORE_SOURCES
    ZeroShotEngine.cpp
    ORTInferenceEngine.cpp
    PipelineEngine.cpp
    EvaluationEngine.cpp
    BadCaseRecorder.cpp
    ModelNotesManager.cpp
    ZeroShotKit.cpp          # Kit 门面类实现
)

set(ZSU_CORE_HEADERS
    ZeroShotTypes.h
    ZeroShotEngine.h
    ORTInferenceEngine.h
    ORTCompat.h
    PipelineEngine.h
    EvaluationEngine.h
    BadCaseRecorder.h
    ModelNotesManager.h
)

# 公共头文件（通过 include/ZeroShotKit/ 暴露）
set(ZSU_PUBLIC_HEADERS
    ../include/ZeroShotKit/Logger.h
    ../include/ZeroShotKit/ZeroShotKit.h
    ../include/ZeroShotKit/ZeroShotTypes.h
    ../include/ZeroShotKit/ZeroShotEngine.h
    ../include/ZeroShotKit/ORTInferenceEngine.h
    ../include/ZeroShotKit/PipelineEngine.h
    ../include/ZeroShotKit/EvaluationEngine.h
    ../include/ZeroShotKit/BadCaseRecorder.h
    ../include/ZeroShotKit/ModelNotesManager.h
    ../include/ZeroShotKit/ORTCompat.h
)

add_library(ZeroShotKitCore STATIC ${ZSU_CORE_SOURCES} ${ZSU_CORE_HEADERS} ${ZSU_PUBLIC_HEADERS})

target_include_directories(ZeroShotKitCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}              # src/ 目录（内部头文件）
    ${CMAKE_CURRENT_SOURCE_DIR}/../include   # include/ 目录（公共头文件）
)

target_link_libraries(ZeroShotKitCore PUBLIC
    Qt6::Core
    Qt6::Concurrent
    ${OpenCV_LIBS}
)

# ORT 链接
if(ZSU_HAS_ORT)
    target_link_libraries(ZeroShotKitCore PUBLIC ${ONNXRUNTIME_LIBS})
    target_include_directories(ZeroShotKitCore PUBLIC ${ONNXRUNTIME_INCLUDE_DIRS})
    target_compile_definitions(ZeroShotKitCore PUBLIC ZSU_HAS_ORT)
    # ORT C++ 头文件在 MinGW GCC 下需要 -fpermissive（类型隐式转换）
    target_compile_options(ZeroShotKitCore PRIVATE -fpermissive)
endif()

# 别名目标
add_library(ZeroShotKit::Core ALIAS ZeroShotKitCore)
