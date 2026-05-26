# Plugins模块 - 插件系统

set(PLUGINS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Plugins/IPlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Plugins/PluginManager.h
)

set(PLUGINS_SOURCES
    PluginManager.cpp
)

add_library(Plugins STATIC
    ${PLUGINS_SOURCES}
    ${PLUGINS_HEADERS}
)

target_include_directories(Plugins PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(Plugins PUBLIC
    Core
    Qt6::Core
)

target_compile_features(Plugins PUBLIC cxx_std_20)

install(TARGETS Plugins
    EXPORT QDVTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${PLUGINS_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Plugins
)
