# =====================================================================
# QDV Annotator — CMake 构建（Qt6 only，无需 OpenCV）
#
# 集成：本工具位于 QDV 仓库内 tools/qdv-annotator/，
#       通过 QML_IMPORT_PATH 复用项目 qml/EditView 模块，无需改动主工程。
#       若希望并入主工程一并构建，可在主 CMakeLists.txt 添加：
#           add_subdirectory(tools/qdv-annotator)
#
# 构建（独立）：
#   cmake -S . -B build -DCMAKE_PREFIX_PATH="D:/Qt_new/6.11.1/mingw_64"
#   cmake --build build -j 8
#   ./build/QDVAnnotator.exe
# =====================================================================
cmake_minimum_required(VERSION 3.27)
project(QDVAnnotator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2)

qt_standard_project_setup(REQUIRES Qt6)

# ---- 可执行文件 ----
qt_add_executable(QDVAnnotator
    main.cpp
    src/AnnotationSession.h
    src/AnnotationSession.cpp
    src/Exporters.h
    src/Exporters.cpp
    src/ImagePreprocess.h
    src/ImagePreprocess.cpp
    src/TrainingBridge.h
    src/TrainingBridge.cpp
    src/ShapeRegistry.h
    src/ShapeRegistry.cpp
    capabilities.qrc
)

target_link_libraries(QDVAnnotator PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

target_include_directories(QDVAnnotator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---- 打包 QML 资源（qrc:/qml/Main.qml + 本地 QDVAnnotator 模块）----
qt_add_resources(QDVAnnotator qml_qrc
    PREFIX /
    FILES
        qml/Main.qml
        qml/AnnotationCanvas.qml
        qml/ImageListPanel.qml
        qml/LabelPanel.qml
        qml/AnnotationListPanel.qml
        qml/PreprocessPanel.qml
        qml/ExportDialog.qml
        qml/TrainingDialog.qml
        qml/ImportWizard.qml
        qml/QDVAnnotator/qmldir
        qml/QDVAnnotator/DesignTokens.qml
        qml/QDVAnnotator/AnnotateOverlay.qml
        qml/QDVAnnotator/WideComboBox.qml
)

target_compile_definitions(QDVAnnotator PRIVATE
    QT_MESSAGELOGCONTEXT
)

# 本工具使用字符串字面量（QString 隐式转换），不启用 QT_NO_CAST_FROM_ASCII，
# 以保证无论是独立构建还是作为主工程 add_subdirectory 都能编译通过。
remove_definitions(-DQT_NO_CAST_FROM_ASCII)

# ---- 单元测试：撤销/重做 + 脏状态（tests/test_annotation_session_undo.cpp）----
qt_add_executable(test_annotation_session_undo
    tests/test_annotation_session_undo.cpp
    src/AnnotationSession.h
    src/AnnotationSession.cpp
    src/Exporters.h
    src/Exporters.cpp
    src/ImagePreprocess.h
    src/ImagePreprocess.cpp
    src/TrainingBridge.h
    src/TrainingBridge.cpp
    src/ShapeRegistry.h
    src/ShapeRegistry.cpp
)
target_link_libraries(test_annotation_session_undo PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)
target_include_directories(test_annotation_session_undo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

install(TARGETS QDVAnnotator RUNTIME DESTINATION bin)
