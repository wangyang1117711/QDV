set(MODULE_NAME TrainingInference)

file(GLOB_RECURSE MODULE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE MODULE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/TrainingInference/*.h
)

add_library(${MODULE_NAME} STATIC ${MODULE_SOURCES} ${MODULE_HEADERS})

target_include_directories(${MODULE_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/TrainingInference
)

target_link_libraries(${MODULE_NAME} PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    AI
    QDV::OperatorLibrary  # spec D Task 11：训练产物自动注册为算子（OperatorLibraryController）
)

target_compile_features(${MODULE_NAME} PUBLIC cxx_std_20)