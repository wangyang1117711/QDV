#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Q-DetectVision Qt6 开发环境验证脚本
检查所有必要的开发组件是否已安装
"""

import os
import sys
import subprocess

def check_command(cmd, description):
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        if result.returncode == 0:
            print(f"  ✓ {description}")
            return True, result.stdout.strip()
        else:
            print(f"  ✗ {description}")
            return False, None
    except (subprocess.TimeoutExpired, FileNotFoundError):
        print(f"  ✗ {description}")
        return False, None

def check_path(path, description):
    if os.path.exists(path):
        print(f"  ✓ {description}")
        return True
    else:
        print(f"  ✗ {description}")
        return False

def main():
    print("=" * 60)
    print("  Q-DetectVision Qt6 开发环境验证")
    print("=" * 60)

    # 检查编译器
    print("\n--- 编译器检查 ---")
    msvc_ok, _ = check_command(["cl.exe"], "MSVC 编译器")
    gcc_ok, _ = check_command(["gcc", "--version"], "GCC 编译器")

    # 检查构建工具
    print("\n--- 构建工具检查 ---")
    cmake_ok, cmake_ver = check_command(["cmake", "--version"], "CMake")
    if cmake_ver:
        print(f"    版本: {cmake_ver.split()[2]}")

    # 检查 Qt6
    print("\n--- Qt6 检查 ---")
    qmake_ok, qmake_ver = check_command(["qmake", "--version"], "qmake")
    if qmake_ver:
        print(f"    版本: {qmake_ver.split()[3]}")

    # 检查 Qt 安装路径
    qt_paths = [
        "C:/Qt/6.5.0/msvc2022_64",
        "C:/Qt/6.5.0/mingw_64",
        "C:/Qt/6.4.3/msvc2022_64"
    ]
    qt_installed = False
    for qt_path in qt_paths:
        if check_path(qt_path, f"Qt6 安装路径: {qt_path}"):
            qt_installed = True
            break

    # 检查 OpenCV
    print("\n--- OpenCV 检查 ---")
    opencv_paths = [
        "C:/opencv/build",
        "C:/vcpkg/installed/x64-windows"
    ]
    opencv_installed = False
    for opencv_path in opencv_paths:
        if check_path(opencv_path, f"OpenCV 路径: {opencv_path}"):
            opencv_installed = True
            break

    # 检查 vcpkg
    print("\n--- vcpkg 检查 ---")
    vcpkg_ok = check_path("C:/vcpkg", "vcpkg 安装路径")

    # 检查项目文件
    print("\n--- 项目文件检查 ---")
    project_root = "E:/anchor/Trae/QDV"
    project_ok = check_path(project_root, "项目根目录")
    
    if project_ok:
        check_path(os.path.join(project_root, "CMakeLists.txt"), "主 CMakeLists.txt")
        check_path(os.path.join(project_root, "apps/SmartVision/main.cpp"), "主程序入口")

    # 汇总
    print("\n" + "=" * 60)
    print("  环境验证汇总")
    print("=" * 60)

    total_checks = 7
    passed = sum([msvc_ok or gcc_ok, cmake_ok, qmake_ok or qt_installed, 
                  opencv_installed, vcpkg_ok, project_ok])

    print(f"\n通过检查: {passed}/{total_checks}")

    if passed == total_checks:
        print("\n✓ 所有环境检查通过，可以编译项目！")
    else:
        print("\n✗ 部分组件缺失，请按照安装指南进行安装")
        print("\n需要安装的组件:")
        
        if not (msvc_ok or gcc_ok):
            print("  - Visual Studio Build Tools 2022")
        
        if not qmake_ok and not qt_installed:
            print("  - Qt6.5+ (MSVC 2022 64-bit)")
        
        if not opencv_installed:
            print("  - OpenCV 4.x (通过 vcpkg)")
        
        if not vcpkg_ok:
            print("  - vcpkg")

    print("\n安装指南: docs/QtEnvironmentSetup.md")
    
    return 0 if passed == total_checks else 1

if __name__ == "__main__":
    sys.exit(main())