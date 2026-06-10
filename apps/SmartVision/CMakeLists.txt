# SmartVision主应用

set(SMARTVISION_SOURCES
    main.cpp
    resources/styles.qrc
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
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Sql
    ${OpenCV_LIBS}
)

target_compile_features(QDetectVision PRIVATE cxx_std_17)

add_custom_command(TARGET QDetectVision POST_BUILD
    # v2.1.0 M4 修复：windeployqt 默认只复制 C++ DLL，不扫描 QML 源码；
    # 加 --qmldir 让其扫描 qml/ 目录下的 .qml 文件，解析 import 语句，
    # 自动部署 QtQuick/Controls/Window/Layouts/Dialogs 等模块到 bin/qml/，
    # 否则 QML 引擎报 "module 'QtQuick.X' is not installed"。
    COMMAND Qt6::windeployqt
        --qmldir "${CMAKE_SOURCE_DIR}/qml"
        $<TARGET_FILE:QDetectVision>
    COMMENT "Running windeployqt (with QML modules scan)"
)

add_custom_command(TARGET QDetectVision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OpenCV_BIN_DIR}/libopencv_world4130.dll"
        "$<TARGET_FILE_DIR:QDetectVision>"
    COMMENT "Copying OpenCV DLL to output directory"
)

# v2.1.0 M5：部署算子元数据 config/operators.json 到 exe 同级 config/
# （若文件已存在则保留本地修改，不覆盖）
add_custom_command(TARGET QDetectVision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:QDetectVision>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/operators.json"
        "$<TARGET_FILE_DIR:QDetectVision>/config/operators.json"
    COMMENT "Deploying config/operators.json (operator metadata)"
)