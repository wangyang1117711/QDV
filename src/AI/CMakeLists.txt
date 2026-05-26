# AI模块 - 深度学习推理

set(AI_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/AI/InferenceEngine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/AI/ModelManager.h
)

set(AI_SOURCES
    InferenceEngine.cpp
    ModelManager.cpp
)

add_library(AI STATIC
    ${AI_SOURCES}
    ${AI_HEADERS}
)

target_include_directories(AI PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(AI PUBLIC
    Core
)

if(ENABLE_GPU)
    target_link_libraries(AI PRIVATE ${CUDA_LIBRARIES})
    target_include_directories(AI PRIVATE ${CUDA_INCLUDE_DIRS})
endif()

target_compile_features(AI PUBLIC cxx_std_20)

install(TARGETS AI
    EXPORT QDVTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${AI_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/AI
)
