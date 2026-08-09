# Monitoring 模块 - 训练推理监控

set(MONITORING_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Monitoring/Anomaly.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Monitoring/AnomalyDetector.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Monitoring/GpuMetricsCollector.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Monitoring/MonitoringStorage.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Monitoring/ProcessSnapshot.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Monitoring/SystemMetricsCollector.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Monitoring/TrainingInferenceMonitor.h
)

set(MONITORING_SOURCES
    AnomalyDetector.cpp
    GpuMetricsCollector.cpp
    MonitoringStorage.cpp
    SystemMetricsCollector.cpp
    TrainingInferenceMonitor.cpp
)

add_library(Monitoring STATIC
    ${MONITORING_SOURCES}
    ${MONITORING_HEADERS}
)

target_include_directories(Monitoring PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Monitoring
)

target_link_libraries(Monitoring PUBLIC
    Core
    Qt6::Core
    Qt6::Network
)

# Windows 平台需要链接 iphlpapi 与 psapi（SystemMetricsCollector 使用）
if(WIN32)
    target_link_libraries(Monitoring PUBLIC
        iphlpapi
        psapi
    )
endif()

target_compile_features(Monitoring PUBLIC cxx_std_17)
