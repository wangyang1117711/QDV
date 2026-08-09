# ============================================================================
# ZeroShotKit UI 层 - Qt Widgets 组件
# ============================================================================

set(ZSU_UI_SOURCES
    ZeroShotPanel.cpp
    ZeroShotResultPanel.cpp
    ModelNotesDialog.cpp
)

set(ZSU_UI_HEADERS
    ZeroShotPanel.h
    ZeroShotResultPanel.h
    ModelNotesDialog.h
)

add_library(ZeroShotKitUI STATIC ${ZSU_UI_SOURCES} ${ZSU_UI_HEADERS})

target_include_directories(ZeroShotKitUI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ZeroShotKitUI PUBLIC
    ZeroShotKit::Core
    Qt6::Widgets
    Qt6::Concurrent
)

# Qt 资源文件（model_notes.json）
# 注意：使用文件系统路径直接读取，不编译为 qrc
# 如果需要 qrc，取消注释以下代码：
# set(ZSU_UI_QRC ${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc)
# target_sources(ZeroShotKitUI PRIVATE ${ZSU_UI_QRC})

# 别名目标
add_library(ZeroShotKit::UI ALIAS ZeroShotKitUI)
