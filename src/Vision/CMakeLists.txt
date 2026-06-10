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
    AI
    ${OpenCV_LIBS}
)

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