# Database模块 - 数据持久化

set(DATABASE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Database/ResultDatabase.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Database/DatabaseIntegrator.h
)

set(DATABASE_SOURCES
    ResultDatabase.cpp
    DatabaseIntegrator.cpp
)

add_library(Database STATIC
    ${DATABASE_SOURCES}
    ${DATABASE_HEADERS}
)

target_include_directories(Database PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include/Database>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(Database PUBLIC
    Core
    Qt6::Core
    Qt6::Sql
)

target_compile_features(Database PUBLIC cxx_std_17)

install(TARGETS Database
    EXPORT QDVTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${DATABASE_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Database
)
