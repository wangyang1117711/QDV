# Vision模块 - 视觉算法和工具链

set(VISION_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/TemplateMatchTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/EdgeDetectTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/BlobDetectTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ColorDetectTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ThresholdTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ImagePreprocessTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ContourAnalyzeTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ToolFactory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ToolChainExecutor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/BranchControlTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/GeometryMeasureTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/LineCircleDetectTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ImageArithmeticTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ImageTransformTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ImageMergeTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/AiClassifyTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ReadImageTool.h
    # 新增：滤波类
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/GaussFilterTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/MeanImageTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/MedianImageTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/EmphasizeTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ScaleImageTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/FftGenericTool.h
    # 新增：形态学类
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ErosionTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/DilationTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/OpeningTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ClosingTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/TopHatTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/BottomHatTool.h
    # 新增：几何变换+分割类
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/AffineTransImageTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/PolarTransImageTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/DynThresholdTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/WatershedTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/RegionGrowingTool.h
    # 新增：Blob+特征提取类
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ConnectionTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/SelectShapeTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/PointsHarrisTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/EdgesSubPixTool.h
    # 新增：匹配+测量类
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/FindNccModelTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/FindShapeModelTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/DistancePpTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/AngleLlTool.h
    # 新增：分析与识别类（2026-07-09）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/HistogramTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/RgbExtractTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ChannelSplitTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/AreaCenterTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/QrCodeDetectTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/Barcode1dTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/OcrTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/HandEyeCalibTool.h
    # 新增：图像采集类
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/OpenFramegrabberTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/GrabImageTool.h
    # 新增：深度学习算子（2026-07-10）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/DetectObjectsDlTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/SegmentDlTool.h
    # 新增：YOLO检测算子（2026-07-11）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/YoloDetectTool.h
    # 新增：均一化算子（2026-07-13）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/NormalizationTool.h
    # 新增：机器人位姿算子（2026-07-10）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/RobotPoseTool.h
    # P0/P1 升级新增算子（2026-07-15）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/PositionCorrectTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/UnitConvertTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/CaliperTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/CameraCalibTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ContourMatchTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ColorMatchTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/DLOCRTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/LightControlTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/LoopTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ScriptTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/VariableTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/SurfaceDefectTool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ContourCompareTool.h
    # v2.7.0 流程合并算子
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/FlowJoinTool.h
    # spec v2 阶段三 Task 8：零样本检测算子
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ZeroShotDetectTool.h
    # 工具链验证器（EditViewBridge 依赖）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision/ToolChainVerifier.h
)

set(VISION_SOURCES
    TemplateMatchTool.cpp
    EdgeDetectTool.cpp
    BlobDetectTool.cpp
    ColorDetectTool.cpp
    ThresholdTool.cpp
    ImagePreprocessTool.cpp
    ContourAnalyzeTool.cpp
    ToolFactory.cpp
    ToolChainExecutor.cpp
    BranchControlTool.cpp
    GeometryMeasureTool.cpp
    LineCircleDetectTool.cpp
    ImageArithmeticTool.cpp
    ImageTransformTool.cpp
    ImageMergeTool.cpp
    AiClassifyTool.cpp
    ReadImageTool.cpp
    # 新增：滤波类
    GaussFilterTool.cpp
    MeanImageTool.cpp
    MedianImageTool.cpp
    EmphasizeTool.cpp
    ScaleImageTool.cpp
    FftGenericTool.cpp
    # 新增：形态学类
    ErosionTool.cpp
    DilationTool.cpp
    OpeningTool.cpp
    ClosingTool.cpp
    TopHatTool.cpp
    BottomHatTool.cpp
    # 新增：几何变换+分割类
    AffineTransImageTool.cpp
    PolarTransImageTool.cpp
    DynThresholdTool.cpp
    WatershedTool.cpp
    RegionGrowingTool.cpp
    # 新增：Blob+特征提取类
    ConnectionTool.cpp
    SelectShapeTool.cpp
    PointsHarrisTool.cpp
    EdgesSubPixTool.cpp
    # 新增：匹配+测量类
    FindNccModelTool.cpp
    FindShapeModelTool.cpp
    DistancePpTool.cpp
    AngleLlTool.cpp
    # 新增：分析与识别类（2026-07-09）
    HistogramTool.cpp
    RgbExtractTool.cpp
    ChannelSplitTool.cpp
    AreaCenterTool.cpp
    QrCodeDetectTool.cpp
    Barcode1dTool.cpp
    OcrTool.cpp
    HandEyeCalibTool.cpp
    # 新增：图像采集类
    OpenFramegrabberTool.cpp
    GrabImageTool.cpp
    # 新增：深度学习算子（2026-07-10）
    DetectObjectsDlTool.cpp
    SegmentDlTool.cpp
    # 新增：YOLO检测算子（2026-07-11）
    YoloDetectTool.cpp
    # 新增：均一化算子（2026-07-13）
    NormalizationTool.cpp
    # 新增：机器人位姿算子（2026-07-10）
    RobotPoseTool.cpp
    # P0/P1 升级新增算子（2026-07-15）
    PositionCorrectTool.cpp
    UnitConvertTool.cpp
    CaliperTool.cpp
    CameraCalibTool.cpp
    ContourMatchTool.cpp
    ColorMatchTool.cpp
    DLOCRTool.cpp
    LightControlTool.cpp
    LoopTool.cpp
    ScriptTool.cpp
    VariableTool.cpp
    SurfaceDefectTool.cpp
    ContourCompareTool.cpp
    # v2.7.0 流程合并算子
    FlowJoinTool.cpp
    # spec v2 阶段三 Task 8：零样本检测算子
    ZeroShotDetectTool.cpp
    # 工具链验证器（EditViewBridge 依赖）
    ToolChainVerifier.cpp
)

add_library(Vision STATIC
    ${VISION_SOURCES}
    ${VISION_HEADERS}
)

target_include_directories(Vision PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include/Vision>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(Vision PUBLIC
    Core
    OperatorSDK
    Communication    # P0-6e LightControlTool 依赖 IOController
    Qt6::Network
    Qt6::Concurrent   # v2.7.0: ToolChainExecutor 并行分支（QtConcurrent::blockingMap）
    ${OpenCV_LIBS}
    ZeroShotKit::Core  # spec v2 阶段三 Task 8：ZeroShotDetectTool 依赖 zsu::Kit
)

# P1-4b ScriptTool: 嵌入式 CPython 链接（QDV_WITH_PYTHON 宏在根 CMakeLists 定义）
if(QDV_WITH_PYTHON AND Python3_INCLUDE_DIRS)
    target_include_directories(Vision PRIVATE ${Python3_INCLUDE_DIRS})
    target_link_libraries(Vision PRIVATE ${Python3_LIBRARIES})
    message(STATUS "Vision: linked against CPython for ScriptTool")
endif()

target_compile_features(Vision PUBLIC cxx_std_17)

install(TARGETS Vision
    EXPORT QDVTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${VISION_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Vision
)