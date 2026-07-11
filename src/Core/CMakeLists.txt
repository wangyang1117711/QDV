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
)

target_compile_features(Core PUBLIC cxx_std_17)