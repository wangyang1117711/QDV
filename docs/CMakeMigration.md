# Qt项目CMake完整迁移指南

## 概述

本文档说明如何将Q-DetectVision项目从Qt默认构建系统（qmake）完整迁移到CMake构建系统。

## 迁移要点

### 1. 项目结构

```
Q-DetectVision/
├── CMakeLists.txt          # 主CMake配置
├── cmake/                  # CMake模块目录
│   ├── CompilerSettings.cmake
│   ├── Dependencies.cmake
│   ├── QDVConfig.cmake.in
│   └── config.h.in
├── src/                    # 源代码目录
│   ├── Core/
│   ├── Vision/
│   ├── UI/
│   ├── Database/
│   ├── AI/
│   ├── Communication/
│   └── Plugins/
├── include/                # 头文件目录
├── apps/                   # 应用程序目录
│   └── SmartVision/
├── tests/                  # 测试目录
├── resources/              # 资源文件目录
└── scripts/                # 构建脚本
```

### 2. 关键CMake配置

#### 2.1 主CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.27)
project(QDetectVision VERSION 1.0.0 LANGUAGES CXX)

# C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Qt自动化
set(CMAKE_AUTOMOC ON)    # 自动MOC
set(CMAKE_AUTORCC ON)    # 自动RCC
set(CMAKE_AUTOUIC ON)    # 自动UIC
```

#### 2.2 依赖查找

```cmake
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Quick
    Qml
    Network
    Sql
)

find_package(OpenCV 4.8 REQUIRED)
find_package(spdlog 1.10 REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)
```

### 3. Qt资源和UI文件处理

#### 3.1 资源文件 (QRC)

```cmake
qt_add_resources(RESOURCES
    resources/resources.qrc
)

add_executable(MyApp
    main.cpp
    ${RESOURCES}
)
```

#### 3.2 UI文件

```cmake
# CMake会自动处理.ui文件，因为CMAKE_AUTOUIC已设置为ON
# UI文件应放在标准位置，或设置搜索路径
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)
```

### 4. 模块化架构

每个模块（Core、Vision、UI等）都有自己的CMakeLists.txt，遵循以下模式：

```cmake
# 模块源文件和头文件
set(MODULE_HEADERS
    include/Module/Header1.h
    include/Module/Header2.h
)

set(MODULE_SOURCES
    src/Module/Source1.cpp
    src/Module/Source2.cpp
)

# 创建静态库
add_library(Module STATIC
    ${MODULE_SOURCES}
    ${MODULE_HEADERS}
)

# 包含目录（构建时和安装时）
target_include_directories(Module PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 链接依赖
target_link_libraries(Module PUBLIC
    Dependency1
    Dependency2
    Qt6::Component
)

# C++标准
target_compile_features(Module PUBLIC cxx_std_20)

# 安装规则
install(TARGETS Module
    EXPORT QDVTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
```

### 5. 测试集成

```cmake
enable_testing()
find_package(Catch2 3.0 REQUIRED)

add_executable(QDVTests
    tests/Test1.cpp
    tests/Test2.cpp
)

target_link_libraries(QDVTests PRIVATE
    Catch2::Catch2WithMain
    Module1
    Module2
)

# 自动发现测试
include(Catch)
catch_discover_tests(QDVTests)
```

### 6. CMake包导出

```cmake
include(CMakePackageConfigHelpers)

# 配置文件
configure_package_config_file(
    cmake/QDVConfig.cmake.in
    QDVConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QDV
)

# 版本文件
write_basic_package_version_file(
    QDVConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# 导出目标
install(EXPORT QDVTargets
    FILE QDVTargets.cmake
    NAMESPACE QDV::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QDV
)
```

## 构建指南

### Windows构建

```cmd
cd Q-DetectVision
mkdir build
cd build

# 使用Visual Studio生成器
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release

# 运行测试
ctest -C Release
```

或使用提供的脚本：

```cmd
scripts\build.bat
```

### Linux/macOS构建

```bash
cd Q-DetectVision
mkdir build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel

# 运行测试
ctest
```

或使用提供的脚本：

```bash
chmod +x scripts/build.sh
scripts/build.sh
```

### 常见构建选项

```cmake
-DCMAKE_BUILD_TYPE=Release|Debug
-DBUILD_TESTS=ON|OFF
-DBUILD_PLUGINS=ON|OFF
-DENABLE_GPU=ON|OFF
-DCMAKE_INSTALL_PREFIX=/path/to/install
```

## CMake最佳实践

### 1. 使用现代CMake

- 使用`target_*`函数而非全局变量
- 使用`target_include_directories`管理包含目录
- 使用`target_link_libraries`管理依赖
- 使用`target_compile_features`设置C++标准

### 2. 导出目标

```cmake
# 构建时和安装时的包含目录分开
target_include_directories(Target PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
```

### 3. 使用生成器表达式

```cmake
# 只在Debug构建中添加定义
target_compile_definitions(Target PRIVATE
    $<$<CONFIG:Debug>:DEBUG_MODE>
)

# MSVC特定选项
target_compile_options(Target PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
```

### 4. 配置头文件

```cmake
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
)
```

### 5. 安装规则

使用`GNUInstallDirs`模块的标准目录：

```cmake
include(GNUInstallDirs)
install(TARGETS Target
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
```

## 从qmake迁移对照表

| qmake特性 | CMake等效 |
|----------|----------|
| `QT += core widgets` | `find_package(Qt6 REQUIRED COMPONENTS Core Widgets)` |
| `HEADERS += ...` | `set(HEADERS ...)`, 在target中引用 |
| `SOURCES += ...` | `set(SOURCES ...)`, 在target中引用 |
| `RESOURCES += ...` | `qt_add_resources()`或自动RCC |
| `FORMS += ...` | 自动UIC处理 |
| `TARGET = myapp` | `add_executable(myapp ...)` |
| `TEMPLATE = lib` | `add_library(...)` |
| `CONFIG += c++20` | `set(CMAKE_CXX_STANDARD 20)` |
| `DESTDIR = ...` | `set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ...)` |
| `INCLUDEPATH += ...` | `target_include_directories()` |
| `LIBS += ...` | `target_link_libraries()` |

## 验证检查清单

- [ ] 所有模块正确构建
- [ ] Qt自动化处理工作正常（MOC、RCC、UIC）
- [ ] 资源文件正确嵌入
- [ ] 测试编译和运行
- [ ] 无警告无错误
- [ ] 可执行文件正确运行
- [ ] 安装规则正确
- [ ] CMake包可以被find_package找到

## 故障排除

### Windows部署

使用`windeployqt`自动部署Qt运行时：

```cmake
if(WIN32 AND MSVC)
    add_custom_command(TARGET Target POST_BUILD
        COMMAND Qt6::windeployqt $<TARGET_FILE:Target>
    )
endif()
```

### 常见错误

**错误：找不到Qt6**
```
# 设置Qt6路径
-DCMAKE_PREFIX_PATH=/path/to/Qt6
```

**错误：UI文件找不到**
```
# 设置UIC搜索路径
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)
```

**错误：MOC未运行**
```
# 确保已设置AUTOMOC
set(CMAKE_AUTOMOC ON)
# 确保头文件在target源列表中
```

## 相关资源

- [CMake官方文档](https://cmake.org/documentation/)
- [Qt6 CMake指南](https://doc.qt.io/qt-6/cmake-manual.html)
- [Modern CMake](https://cliutils.gitlab.io/modern-cmake/)
