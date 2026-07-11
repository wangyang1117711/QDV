# UI模块 - 用户界面

set(UI_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/MainWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/LoginView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/CentralWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/EditView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/EditViewBridge.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/OperatorDescriptors.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/UndoCommands.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/SchemeSerializer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/RenderWidget.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/CameraView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/SchemeView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/IOView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/CommView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/MonitorView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/AutoTestRunner.h
    # v2.6.0 预览管理
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/PreviewManager.h
)

set(UI_SOURCES
    MainWindow.cpp
    LoginView.cpp
    CentralWindow.cpp
    EditView.cpp
    EditViewBridge.cpp
    OperatorDescriptors.cpp
    UndoCommands.cpp
    SchemeSerializer.cpp
    RenderWidget.cpp
    CameraView.cpp
    SchemeView.cpp
    IOView.cpp
    CommView.cpp
    MonitorView.cpp
    AutoTestRunner.cpp
    # v2.6.0 预览管理
    PreviewManager.cpp
)

# M2 v2.1.0：注册 qml/ 资源（QML 端 EditView 改造用）
set(UI_QML_RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../qml/EditView.qrc
)

add_library(UI STATIC
    ${UI_SOURCES}
    ${UI_HEADERS}
    ${UI_QML_RESOURCES}
)

target_include_directories(UI PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI>
    $<INSTALL_INTERFACE:include>
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(UI PUBLIC
    Core
    Vision
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::Qml
    Qt6::Concurrent
    ${OpenCV_LIBS}
)

target_compile_features(UI PUBLIC cxx_std_17)

install(TARGETS UI
    EXPORT QDVTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${UI_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/UI
)