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
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/Monitoring
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
    OperatorSDK
    Monitoring
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Sql
    ${OpenCV_LIBS}
    dbghelp
    shlwapi
    psapi
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

# P1-4b ScriptTool: 部署 python314.dll 到 exe 目录（嵌入 CPython 运行时必需）
if(QDV_WITH_PYTHON AND Python3_DLL AND EXISTS "${Python3_DLL}")
    add_custom_command(TARGET QDetectVision POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${Python3_DLL}"
            "$<TARGET_FILE_DIR:QDetectVision>"
        COMMENT "Copying python314.dll to output directory for ScriptTool"
    )
endif()

# [零样本检测模块] 部署 ONNX Runtime DLL 到 exe 目录
# ZeroShotKit 通过 ZSU_HAS_ORT 宏条件编译 ORT 后端，
# 运行时若缺少 onnxruntime.dll，推理会在加载模型时失败。
# 优先从 D:/onnxruntime（用户偏好：非必要不放 C 盘）查找。
if(EXISTS "${ONNXRUNTIME_ROOT_DIR}/lib/onnxruntime.dll")
    add_custom_command(TARGET QDetectVision POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_ROOT_DIR}/lib/onnxruntime.dll"
            "$<TARGET_FILE_DIR:QDetectVision>"
        COMMENT "Copying ONNX Runtime DLL to output directory"
    )
else()
    message(WARNING "ONNX Runtime DLL not found at ${ONNXRUNTIME_ROOT_DIR}/lib/onnxruntime.dll - "
                    "ZeroShotKit ORT inference will fail at runtime if D:/onnxruntime not in PATH")
endif()

# v2.1.0 M5：部署算子元数据 config/operators.json 到 exe 同级 config/
# v5.3.5：改为 copy（强制覆盖），避免 copy_if_different 在某些情况下不触发
add_custom_command(TARGET QDetectVision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:QDetectVision>/config"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/config/operators.json"
        "$<TARGET_FILE_DIR:QDetectVision>/config/operators.json"
    COMMENT "Deploying config/operators.json (operator metadata)"
)

# v2.0 阶段二 Task 5：部署共享提示词库 config/prompt_library.json
add_custom_command(TARGET QDetectVision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/prompt_library.json"
        "$<TARGET_FILE_DIR:QDetectVision>/config/prompt_library.json"
    COMMENT "Deploying config/prompt_library.json (shared prompt library)"
)

# v2.0 阶段七 Task 16：部署模型源路径配置 config/model_sources.json
add_custom_command(TARGET QDetectVision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/model_sources.json"
        "$<TARGET_FILE_DIR:QDetectVision>/config/model_sources.json"
    COMMENT "Deploying config/model_sources.json (model source paths)"
)

# v2.7.3：部署默认模型文件到 exe 同级 models/ 目录。
# 注意：只同步模型本体 (.onnx) 和标签文件 (*_labels.json / labels.json)；
# 不同步 manifest.json，避免覆盖运行时用户注册/导入产生的模型库记录。
# 新增模型文件到源码 models/ 后，需重新运行 cmake 配置以更新此列表。
file(GLOB DEFAULT_MODEL_FILES
    "${CMAKE_SOURCE_DIR}/models/*.onnx"
    "${CMAKE_SOURCE_DIR}/models/*_labels.json"
    "${CMAKE_SOURCE_DIR}/models/labels.json"
)
foreach(MODEL_FILE ${DEFAULT_MODEL_FILES})
    get_filename_component(FILE_NAME ${MODEL_FILE} NAME)
    add_custom_command(TARGET QDetectVision POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MODEL_FILE}"
            "$<TARGET_FILE_DIR:QDetectVision>/models/${FILE_NAME}"
        COMMENT "Deploying default model file: ${FILE_NAME}"
    )
endforeach()

# BUG修复：部署模型注意事项默认配置 resources/models/model_notes.json 到 exe 同级。
# ZeroShotKit 从文件系统读取（未编译进 qrc），缺失时右栏"模型注意事项"为空。
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/ZeroShotKit/resources/models/model_notes.json")
    add_custom_command(TARGET QDetectVision POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:QDetectVision>/resources/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/ZeroShotKit/resources/models/model_notes.json"
            "$<TARGET_FILE_DIR:QDetectVision>/resources/models/model_notes.json"
        COMMENT "Deploying resources/models/model_notes.json (zero-shot model notes)"
    )
else()
    message(WARNING "model_notes.json not found - zero-shot model notes will be empty")
endif()