# src/OperatorLibrary/ - 自定义算子库模块（静态库 QDVOperatorLibrary）
#
# 职责：
#   - 算子定义数据模型（OperatorDefinition.cpp）
#   - 后续切片将追加：RegistryStore / Validator / Importer / Controller / ListModel
#
# 设计原则（AGENTS.md 宪法 II.2：代码与 Spec 契约严格匹配）：
#   - 自包含：模块不依赖 UI/Vision，仅依赖 Qt6 Core/Gui
#   - 通过 QDV::OperatorLibrary 别名供其他模块链接
#
# 循环依赖处理（方案 D，spec §1.4 决策）：
#   - OperatorLibraryController.cpp 直接 #include "UI/OperatorDescriptors.h"
#   - 本 CMakeLists 不链接 UI 模块（避免 OperatorLibrary ↔ UI 循环依赖）
#   - 静态库允许未解析符号，由最终可执行文件（QDV_tests / SmartVision）解析
#   - 最终可执行文件必须同时链接 UI + QDVOperatorLibrary

set(OPERATORLIB_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/OperatorLibrary/OperatorDefinition.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/OperatorLibrary/OperatorRegistryStore.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/OperatorLibrary/OperatorValidator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/OperatorLibrary/OperatorImporter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/OperatorLibrary/OperatorLibraryController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/OperatorLibrary/OperatorListModel.h
    # 以下头文件待对应 .cpp 实现后逐步加入，避免 AUTOMOC 扫描未实现的 Q_OBJECT 类
    # OperatorDependencyGraph.h / OperatorDocGenerator.h /
    # OperatorMetadataInferrer.h / OperatorPermissionManager.h /
    # OperatorTester.h / OperatorVersionManager.h — 后续切片 O1b/O1c/O1d/O1e/O2
)

set(OPERATORLIB_SOURCES
    OperatorDefinition.cpp
    OperatorRegistryStore.cpp
    OperatorValidator.cpp
    OperatorImporter.cpp
    OperatorLibraryController.cpp
    OperatorListModel.cpp
)

add_library(QDVOperatorLibrary STATIC
    ${OPERATORLIB_SOURCES}
    ${OPERATORLIB_HEADERS}
)

# 别名供其他模块以 QDV::OperatorLibrary 形式引用（spec §1.2 契约要求）
add_library(QDV::OperatorLibrary ALIAS QDVOperatorLibrary)

target_include_directories(QDVOperatorLibrary PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/OperatorLibrary
)

target_link_libraries(QDVOperatorLibrary PUBLIC
    Qt6::Core
    Qt6::Gui
)

target_compile_features(QDVOperatorLibrary PUBLIC cxx_std_17)
