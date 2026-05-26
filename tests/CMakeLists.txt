# 测试模块

enable_testing()

set(TEST_SOURCES
    SchemeTest.cpp
    VisionToolTest.cpp
    ToolChainExecutorTest.cpp
    AuthServiceTest.cpp
    DatabaseTest.cpp
    VisionToolExtendedTest.cpp
    SchemeValidationTest.cpp
    IntegrationTest.cpp
    SystemTest.cpp
    NewVisionToolsTest.cpp
    CommunicationTest.cpp
)

add_executable(QDVTests
    ${TEST_SOURCES}
)

target_link_libraries(QDVTests PRIVATE
    Core
    Vision
    Database
    Communication
    Catch2::Catch2WithMain
    ${OpenCV_LIBS}
)

target_compile_features(QDVTests PRIVATE cxx_std_20)

target_include_directories(QDVTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

include(Catch)
catch_discover_tests(QDVTests)

install(TARGETS QDVTests
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/tests
)