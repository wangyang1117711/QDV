# Histogram 动态算子库（Phase 2 示例新算子）
#
# 编译为 Histogram.dll，通过 manifest.json 提供元数据
# 由 OperatorPluginLoader 在主程序启动时加载

add_library(Histogram SHARED
    HistogramOperator.cpp
    HistogramOperator.h
)

target_include_directories(Histogram PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../OperatorSDK/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../../include>
)

# 动态库不链接 Vision/AI，仅依赖 OperatorSDK 接口 + OpenCV
target_link_libraries(Histogram PRIVATE
    OperatorSDK
    Qt6::Core
    ${OpenCV_LIBS}
)

target_compile_features(Histogram PRIVATE cxx_std_17)

# 编译产物放到 bin/operators 目录（与 manifest.json 同级，便于 main.cpp 扫描加载）
# Windows 下 MinGW 默认加 lib 前缀，需去除以匹配 manifest.json 中的 "Histogram.dll"
set_target_properties(Histogram PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/operators
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/operators
)

# 部署 manifest.json 到 bin/operators 目录（与 .dll 同级，便于 OperatorPluginLoader 读取）
add_custom_command(TARGET Histogram POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json"
        "$<TARGET_FILE_DIR:Histogram>/Histogram.json"
    COMMENT "Deploying Histogram manifest.json"
)
