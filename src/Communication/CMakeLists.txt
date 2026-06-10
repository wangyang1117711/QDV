# Communication模块 - 网络和串口通信

set(COMMUNICATION_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Communication/TCPCommunicator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Communication/SerialCommunicator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Communication/IOController.h
)

set(COMMUNICATION_SOURCES
    TCPCommunicator.cpp
    SerialCommunicator.cpp
    IOController.cpp
)

add_library(Communication STATIC
    ${COMMUNICATION_SOURCES}
    ${COMMUNICATION_HEADERS}
)

target_include_directories(Communication PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include/Communication>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(Communication PUBLIC
    Core
    Qt6::Core
    Qt6::Network
)

target_compile_features(Communication PUBLIC cxx_std_17)

install(TARGETS Communication
    EXPORT QDVTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${COMMUNICATION_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Communication
)