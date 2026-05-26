# UI模块 - 用户界面

set(UI_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/MainWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/LoginView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/CentralWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/EditView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/RenderWidget.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/CameraView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/SchemeView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/IOView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/CommView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/MonitorView.h
)

set(UI_SOURCES
    MainWindow.cpp
    LoginView.cpp
    CentralWindow.cpp
    EditView.cpp
    RenderWidget.cpp
    CameraView.cpp
    SchemeView.cpp
    IOView.cpp
    CommView.cpp
    MonitorView.cpp
)

add_library(UI STATIC
    ${UI_SOURCES}
    ${UI_HEADERS}
)

target_include_directories(UI PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI>
    $<INSTALL_INTERFACE:include>
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(UI PUBLIC
    Core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${OpenCV_LIBS}
)

target_compile_features(UI PUBLIC cxx_std_20)

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