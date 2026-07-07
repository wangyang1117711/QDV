# OperatorSDK - 独立算子库 SDK 骨架（Phase 0）
#
# 设计原则：
# - 零侵入：不依赖 AI / Vision 模块，保持独立
# - 接口先行：仅定义 IOperator/IInferenceEngine/IOperatorRegistry 等抽象
# - 与现有 ToolFactory 并存（不替代）

set(OPSDK_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/OperatorSDK/IOperator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/OperatorSDK/IOperatorRegistry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/OperatorSDK/IInferenceEngine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/OperatorSDK/OperatorContext.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/OperatorSDK/OperatorManifest.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/OperatorSDK/RegisterMacros.h
)

set(OPSDK_SOURCES
    src/OperatorRegistry.cpp
    src/ManifestLoader.cpp
    src/OperatorPluginLoader.cpp
)

add_library(OperatorSDK STATIC
    ${OPSDK_SOURCES}
    ${OPSDK_HEADERS}
)

target_include_directories(OperatorSDK PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

# 仅链接 Qt6::Core + OpenCV；不链接 AI / Vision，保持独立
target_link_libraries(OperatorSDK PUBLIC
    Qt6::Core
    ${OpenCV_LIBS}
)

target_compile_features(OperatorSDK PUBLIC cxx_std_17)

install(TARGETS OperatorSDK
    EXPORT QDVTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${OPSDK_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/OperatorSDK
)
