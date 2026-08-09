# Core模块 - 核心数据结构和服务

set(CORE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/Scheme.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/SchemeManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/Logger.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/AuthService.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/VisionTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/CameraConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/TriggerConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/OutputConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/ModelBinding.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/BranchNode.h
    # v2.6.0 变量管理
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/VariableManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/ImageVariableManager.h
    # S6 修复：路径校验工具类（header-only）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/PathValidator.h
    # AGENTS.md v2.0：VERDICT 裁决协议数据结构
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/Verdict.h
    # AGENTS.md v2.0：SpecGuardian 最高审计者
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/SpecGuardian.h
    # v2.0 阶段二：共享提示词库（跨 AI 算子复用）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/PromptLibrary.h
    # v2.0 阶段六 Task 15.2：推理结果缓存（图像哈希+提示词+ROI+模型路径）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core/InferenceCache.h
)

set(CORE_SOURCES
    Scheme.cpp
    SchemeManager.cpp
    Logger.cpp
    AuthService.cpp
    BranchNode.cpp
    # v2.6.0 变量管理
    VariableManager.cpp
    ImageVariableManager.cpp
    # P1-3 typed ports：端口描述符实现
    PortDescriptor.cpp
    # AGENTS.md v2.0：SpecGuardian 最高审计者实现（含 Verdict 序列化）
    SpecGuardian.cpp
    # v2.0 阶段二：共享提示词库实现
    PromptLibrary.cpp
    # v2.0 阶段六 Task 15.2：推理结果缓存实现
    InferenceCache.cpp
)

add_library(Core STATIC
    ${CORE_SOURCES}
    ${CORE_HEADERS}
)

target_include_directories(Core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core
)

target_link_libraries(Core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Sql
    ${OpenCV_LIBS}
)

# InferenceCache 使用 cv::Mat / cv::imencode，需要 OpenCV 头文件
if(OpenCV_INCLUDE_DIRS)
    target_include_directories(Core PUBLIC ${OpenCV_INCLUDE_DIRS})
endif()

target_compile_features(Core PUBLIC cxx_std_17)