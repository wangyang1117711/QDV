# 编译器设置

# 设置警告级别
if(MSVC)
    set(WARNING_FLAGS /W4 /WX)
    set(OPTIMIZATION_FLAGS /O2)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(OPTIMIZATION_FLAGS /Od /Zi)
    endif()
else()
    set(WARNING_FLAGS -Wall -Wextra -Wpedantic -Werror)
    set(OPTIMIZATION_FLAGS -O2)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(OPTIMIZATION_FLAGS -O0 -g)
    endif()
endif()

# 应用编译器标志
add_compile_options(${WARNING_FLAGS})
add_compile_options(${OPTIMIZATION_FLAGS})

# 调试/发布模式定义
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(QDV_DEBUG)
endif()

# Windows特定设置
if(WIN32)
    # Windows控制台输出编码
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    
    # 静态链接运行时
    if(QT_STATIC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# 标准安装目录
include(GNUInstallDirs)
