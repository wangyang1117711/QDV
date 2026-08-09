# UI模块 - 用户界面

set(UI_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/MainWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/LoginView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/CentralWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/EditView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/EditViewBridge.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/OperatorDescriptors.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/UndoCommands.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/SchemeSerializer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/RenderWidget.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/CameraView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/SchemeView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/IOView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/CommView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/MonitorView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/AutoTestRunner.h
    # v2.6.0 预览管理
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/PreviewManager.h
    # v2.6.0 图像金字塔缓存
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/ImagePyramidCache.h
    # 零样本检测模块
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/ZeroShotDetectView.h
    # v2.7.0 O1a：算子库桥接器
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/OperatorLibraryBridge.h
    # v2.8.0：算子帮助内容提供器（双源加载帮助）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/OperatorHelpProvider.h
    # v2.7.0 Phase 3.1 Task 6：从 EditViewBridge 拆分的专职协作者
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/ClipboardManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/SchemeRunController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/OutputConfigManager.h
    # spec：editor-output-connection-optimization 端口绑定数据模型
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/PortBindingManager.h
    # spec：editor-output-connection-optimization Task 6 输入/输出项冲突检测引擎
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/OutputConflictDetector.h
    # spec：editor-output-connection-optimization 智能算子推荐引擎（Task 9）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/OperatorRecommender.h
    # v2.0 阶段五 Task 13：第二层 训练兑底流程 数据收集向导
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/DataCollectionWizard.h
    # v2.0 阶段六 Task 14：结果导出对话框（JSON/CSV/图片叠加标注）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI/ResultExportDialog.h
)

set(UI_SOURCES
    MainWindow.cpp
    LoginView.cpp
    CentralWindow.cpp
    EditView.cpp
    EditViewBridge.cpp
    OperatorDescriptors.cpp
    UndoCommands.cpp
    SchemeSerializer.cpp
    RenderWidget.cpp
    CameraView.cpp
    SchemeView.cpp
    IOView.cpp
    CommView.cpp
    MonitorView.cpp
    AutoTestRunner.cpp
    # v2.6.0 预览管理
    PreviewManager.cpp
    # v2.6.0 图像金字塔缓存
    ImagePyramidCache.cpp
    # 零样本检测模块
    ZeroShotDetectView.cpp
    # v2.7.0 O1a：算子库桥接器
    OperatorLibraryBridge.cpp
    # v2.8.0：算子帮助内容提供器（双源加载帮助）
    OperatorHelpProvider.cpp
    # v2.7.0 Phase 3.1 Task 6：从 EditViewBridge 拆分的专职协作者
    ClipboardManager.cpp
    SchemeRunController.cpp
    OutputConfigManager.cpp
    PortBindingManager.cpp
    # spec：editor-output-connection-optimization Task 6 输入/输出项冲突检测引擎
    OutputConflictDetector.cpp
    # spec：editor-output-connection-optimization 智能算子推荐引擎（Task 9）
    OperatorRecommender.cpp
    # v2.0 阶段五 Task 13：第二层 训练兑底流程 数据收集向导
    DataCollectionWizard.cpp
    # v2.0 阶段六 Task 14：结果导出对话框实现
    ResultExportDialog.cpp
)

# M2 v2.1.0：注册 qml/ 资源（QML 端 EditView 改造用）
set(UI_QML_RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../qml/EditView.qrc
)

add_library(UI STATIC
    ${UI_SOURCES}
    ${UI_HEADERS}
    ${UI_QML_RESOURCES}
)

target_include_directories(UI PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include/UI>
    $<INSTALL_INTERFACE:include>
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(UI PUBLIC
    Core
    Vision
    QDVExport
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::Qml
    Qt6::Concurrent
    ${OpenCV_LIBS}
    ZeroShotKit::Core
    ZeroShotKit::UI
    QDV::OperatorLibrary
)

target_compile_features(UI PUBLIC cxx_std_17)

install(TARGETS UI
    EXPORT QDVTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${UI_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/UI
)