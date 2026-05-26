# SmartVision主应用

set(SMARTVISION_SOURCES
    main.cpp
)

add_executable(QDetectVision
    ${SMARTVISION_SOURCES}
)

target_include_directories(QDetectVision PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/AI
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/TrainingInference
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(QDetectVision PRIVATE
    Core
    AI
    UI
    TrainingInference
    Vision
    Communication
    Database
    Plugins
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::SerialPort Qt6::Sql
    ${OpenCV_LIBS}
)

target_compile_features(QDetectVision PRIVATE cxx_std_20)

add_custom_command(TARGET QDetectVision POST_BUILD
    COMMAND Qt6::windeployqt $<TARGET_FILE:QDetectVision>
    COMMENT "Running windeployqt"
)

add_custom_command(TARGET QDetectVision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OpenCV_BIN_DIR}/libopencv_world4130.dll"
        "$<TARGET_FILE_DIR:QDetectVision>"
    COMMENT "Copying OpenCV DLL to output directory"
)