#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Q-DetectVision 开发任务完成验证脚本
验证所有新开发的模块和测试文件的完整性
"""

import os
import sys

PROJECT_ROOT = "E:/anchor/Trae/QDV"

def check_file(path, description):
    full_path = os.path.join(PROJECT_ROOT, path)
    exists = os.path.exists(full_path)
    size = os.path.getsize(full_path) if exists else 0
    status = "PASS" if exists and size > 0 else "FAIL"
    print(f"  [{status}] {description}")
    print(f"         {path} ({size} bytes)")
    return exists and size > 0

def check_category(title, files):
    print(f"\n{'='*60}")
    print(f"  {title}")
    print(f"{'='*60}")
    passed = 0
    failed = 0
    for path, desc in files:
        if check_file(path, desc):
            passed += 1
        else:
            failed += 1
    return passed, failed

def main():
    print("=" * 60)
    print("  Q-DetectVision 开发任务验证工具")
    print("=" * 60)
    
    total_pass = 0
    total_fail = 0
    
    # 新视觉工具头文件
    category = "新增视觉工具头文件"
    files = [
        ("include/Vision/BranchControlTool.h", "BranchControlTool 分支控制工具头文件"),
        ("include/Vision/GeometryMeasureTool.h", "GeometryMeasureTool 几何测量工具头文件"),
        ("include/Vision/LineCircleDetectTool.h", "LineCircleDetectTool 直线圆检测头文件"),
        ("include/Vision/ImageArithmeticTool.h", "ImageArithmeticTool 图像运算头文件"),
        ("include/Vision/ImageTransformTool.h", "ImageTransformTool 图像变换头文件"),
        ("include/Vision/ImageMergeTool.h", "ImageMergeTool 图像合并工具头文件"),
    ]
    p, f = check_category(category, files)
    total_pass += p
    total_fail += f
    
    # 新视觉工具实现文件
    category = "新增视觉工具实现文件"
    files = [
        ("src/Vision/BranchControlTool.cpp", "BranchControlTool 实现"),
        ("src/Vision/GeometryMeasureTool.cpp", "GeometryMeasureTool 实现"),
        ("src/Vision/LineCircleDetectTool.cpp", "LineCircleDetectTool 实现"),
        ("src/Vision/ImageArithmeticTool.cpp", "ImageArithmeticTool 实现"),
        ("src/Vision/ImageTransformTool.cpp", "ImageTransformTool 实现"),
        ("src/Vision/ImageMergeTool.cpp", "ImageMergeTool 实现"),
    ]
    p, f = check_category(category, files)
    total_pass += p
    total_fail += f
    
    # 新增核心模块
    category = "新增核心模块文件"
    files = [
        ("src/Core/BranchNode.cpp", "BranchNode 分支节点实现"),
        ("include/Core/BranchNode.h", "BranchNode 分支节点头文件(已更新)"),
    ]
    p, f = check_category(category, files)
    total_pass += p
    total_fail += f
    
    # 新增UI模块
    category = "新增UI模块文件"
    files = [
        ("include/UI/EditView.h", "EditView 方案编辑器头文件"),
        ("src/UI/EditView.cpp", "EditView 方案编辑器实现"),
        ("include/UI/RenderWidget.h", "RenderWidget 渲染控件头文件"),
        ("src/UI/RenderWidget.cpp", "RenderWidget 渲染控件实现"),
    ]
    p, f = check_category(category, files)
    total_pass += p
    total_fail += f
    
    # 新增通信模块
    category = "新增通信模块文件"
    files = [
        ("include/Communication/SerialCommunicator.h", "SerialCommunicator 串口通信头文件"),
        ("src/Communication/SerialCommunicator.cpp", "SerialCommunicator 串口通信实现"),
        ("include/Communication/IOController.h", "IOController IO控制器头文件"),
        ("src/Communication/IOController.cpp", "IOController IO控制器实现"),
    ]
    p, f = check_category(category, files)
    total_pass += p
    total_fail += f
    
    # AI模块实现
    category = "AI模块实现文件"
    files = [
        ("src/AI/InferenceEngine.cpp", "InferenceEngine AI推理引擎实现"),
    ]
    p, f = check_category(category, files)
    total_pass += p
    total_fail += f
    
    # 新增测试文件
    category = "新增测试文件"
    files = [
        ("tests/NewVisionToolsTest.cpp", "新视觉工具测试"),
        ("tests/CommunicationTest.cpp", "通信模块测试"),
    ]
    p, f = check_category(category, files)
    total_pass += p
    total_fail += f
    
    # CMake更新
    category = "已更新的CMake配置"
    files = [
        ("src/Core/CMakeLists.txt", "Core CMake (含BranchNode)"),
        ("src/Vision/CMakeLists.txt", "Vision CMake (含所有新工具)"),
        ("src/UI/CMakeLists.txt", "UI CMake (含EditView+RenderWidget)"),
        ("src/Communication/CMakeLists.txt", "Communication CMake (含Serial+IO)"),
        ("tests/CMakeLists.txt", "Tests CMake (含新测试)"),
    ]
    p, f = check_category(category, files)
    total_pass += p
    total_fail += f
    
    # 汇总
    print(f"\n{'='*60}")
    print(f"  验证结果汇总")
    print(f"{'='*60}")
    total = total_pass + total_fail
    print(f"  总计: {total} 项检查")
    print(f"  通过: {total_pass} 项")
    print(f"  失败: {total_fail} 项")
    print(f"  通过率: {total_pass/total*100:.1f}%")
    
    if total_fail == 0:
        print(f"\n  *** 所有文件检查通过! ***")
    else:
        print(f"\n  *** 存在 {total_fail} 个失败项 ***")
    
    print(f"{'='*60}")
    
    return 0 if total_fail == 0 else 1

if __name__ == "__main__":
    sys.exit(main())