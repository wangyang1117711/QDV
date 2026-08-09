# src/Export/ - 算子流程导出编排器（静态库 QDVExport）

set(EXPORT_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Export/ExportConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Export/ExportPackage.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Export/TemplateLocator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Export/RuntimePackager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Export/InterfaceDocGenerator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Export/ExampleCodeGenerator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Export/SchemeExporter.h
)

set(EXPORT_SOURCES
    TemplateLocator.cpp
    RuntimePackager.cpp
    InterfaceDocGenerator.cpp
    ExampleCodeGenerator.cpp
    SchemeExporter.cpp
)

add_library(QDVExport STATIC
    ${EXPORT_SOURCES}
    ${EXPORT_HEADERS}
)

target_include_directories(QDVExport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    ${OpenCV_INCLUDE_DIRS}
)

# 注入模板二进制目录（供 TemplateLocator 定位模板）
target_compile_definitions(QDVExport PRIVATE
    QDV_TEMPLATE_BIN_DIR="${CMAKE_BINARY_DIR}/templates/bin"
    OPENCV_BIN_DIR="D:/opencv/build_mingw/bin"
)

target_link_libraries(QDVExport PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent
    ${OpenCV_LIBS}
)

target_compile_features(QDVExport PUBLIC cxx_std_17)

install(TARGETS QDVExport
    EXPORT QDVTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${EXPORT_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Export
)
