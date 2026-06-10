cmake_minimum_required(VERSION 3.20)
project(QDV_Tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

enable_testing()

add_executable(QDV_tests
    test_main.cpp
    Core/test_scheme.cpp
    Core/test_auth.cpp
    Core/test_branch_node.cpp
    Core/test_result.cpp
    Core/test_logger.cpp
    Vision/test_tools.cpp
    Vision/test_tool_chain.cpp
    Communication/test_tcp.cpp
    Communication/test_serial.cpp
    Communication/test_io.cpp
    Database/test_database.cpp
    Database/test_integrator.cpp
    AI/test_inference.cpp
    AI/test_model_manager.cpp
    AI/test_classification_metrics.cpp
    UI/test_ui_smoke.cpp
    UI/test_mainwindow_upgrade.cpp
    Plugins/test_plugins.cpp
    TrainingInference/test_preprocessor.cpp
    TrainingInference/test_image_manager.cpp
    TrainingInference/test_training_inference_view.cpp
    TrainingInference/visual_test_utils.cpp
    TrainingInference/test_visual_verification.cpp
    Integration/test_integration.cpp
)

target_include_directories(QDV_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(QDV_tests PRIVATE
    UI
    TrainingInference
    Plugins
    Core
    AI
    Vision
    Communication
    Database
    Qt6::Core Qt6::Sql Qt6::Network Qt6::Widgets Qt6::Concurrent Qt6::Test
    ${OpenCV_LIBS}
)

add_test(NAME QDV_Tests_All COMMAND QDV_tests)