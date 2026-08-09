# templates/ - 算子流程导出模板预编译
#
# 构建时一次性编译产出 wrapper DLL / EXE / 运行时静态库，
# 供 SchemeExporter 在导出时复制到产物包。
# 遵守 MinGW -j 1 历史教训（在顶层构建脚本中控制）。

# 模板二进制输出目录（SchemeExporter 通过编译定义 QDV_TEMPLATE_BIN_DIR 定位）
set(QDV_TEMPLATE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/templates/bin")
set(QDV_TEMPLATE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include" CACHE INTERNAL "模板头文件目录")

# QDVRuntime 静态库：聚合算子执行能力，供 wrapper DLL 与 EXE 共享
add_library(QDVRuntime STATIC
    src/QDVRuntime.cpp
    src/QDVRuntime.h
)
target_include_directories(QDVRuntime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(QDVRuntime PUBLIC
    Core
    Vision
    Qt6::Core
    Qt6::Gui
    ${OpenCV_LIBS}
)
target_compile_features(QDVRuntime PUBLIC cxx_std_17)
# 运行时不依赖 QML（精简运行时包，符合 spec 约束）
# 不链接 Qt6::Quick / Qt6::QuickWidgets / Qt6::Qml

# QDVPipeline.dll：纯 C 接口 wrapper，供 C++/Python/其他语言调用
add_library(QDVPipeline SHARED
    src/QDVPipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/QDVPipeline.h
)
target_link_libraries(QDVPipeline PRIVATE
    QDVRuntime
)
target_include_directories(QDVPipeline PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# 导出符号定义
target_compile_definitions(QDVPipeline PRIVATE QDVPIPELINE_EXPORTS)
set_target_properties(QDVPipeline PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${QDV_TEMPLATE_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${QDV_TEMPLATE_OUTPUT_DIR}"
    PREFIX ""  # MinGW 默认 lib 前缀，导出场景需去掉
    OUTPUT_NAME "QDVPipeline"
)

# QDVPipelineRun.exe：命令行可执行，通过参数接收输入图返回结果
add_executable(QDVPipelineRun
    src/QDVPipelineRun.cpp
)
target_link_libraries(QDVPipelineRun PRIVATE
    QDVRuntime
    Qt6::Core
)
target_include_directories(QDVPipelineRun PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(QDVPipelineRun PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${QDV_TEMPLATE_OUTPUT_DIR}"
    OUTPUT_NAME "QDVPipelineRun"
)

message(STATUS "[Export] 模板产物输出目录: ${QDV_TEMPLATE_OUTPUT_DIR}")
