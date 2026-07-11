#!/usr/bin/env python3
"""批量生成 mock 算子目录骨架
生成 32 个 mock 补全算子 + 5 个 Agent 角色 mock 算子
每个算子一个独立目录，包含: CMakeLists.txt / manifest.json / <Type>Operator.h / <Type>Operator.cpp
"""
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXT_DIR = ROOT / "src" / "operators" / "extensions"
OPS_JSON = ROOT / "config" / "operators.json"

# 32 个需 mock 补全的算子 type（来自 spec）
MOCK_TYPES = [
    # 图像采集 (2)
    "GrabImage", "OpenFramegrabber",
    # 预处理 (6)
    "FftGeneric", "GaussFilter", "MeanImage", "Emphasize", "ScaleImage", "MedianImage",
    # 几何变换 (2)
    "AffineTransImage", "PolarTransImage",
    # 形态学 (6)
    "Closing", "BottomHat", "Erosion", "Opening", "TopHat", "Dilation",
    # 图像分割 (3)
    "DynThreshold", "Watershed", "RegionGrowing",
    # Blob 分析 (2)
    "Connection", "SelectShape",
    # 特征提取 (2)
    "PointsHarris", "EdgesSubPix",
    # 匹配定位 (2)
    "FindNccModel", "FindShapeModel",
    # 几何测量 (2)
    "DistancePp", "AngleLl",
    # 3D 视觉 (3)
    "Reconstruct3D", "BinocularDisparity", "SurfaceMatching",
    # 深度学习 (2)
    "SegmentDl", "DetectObjectsDl",
]

# 5 个新增 Agent 角色 mock 算子
AGENT_MOCKS = [
    ("MockClassify", "模拟分类", "Agent协作", "Reviewer", "模拟评审分类判定"),
    ("MockValidate", "模拟验证", "Agent协作", "Gatekeeper", "模拟终门验证"),
    ("MockPlan", "模拟规划", "Agent协作", "Planner", "模拟生成执行计划"),
    ("MockCoordinate", "模拟协调", "Agent协作", "Coordinator", "模拟任务调度"),
    ("MockAudit", "模拟审计", "Agent协作", "SpecGuardian", "模拟审计检查"),
]


def load_operators_metadata():
    """从 operators.json 加载算子元数据"""
    with open(OPS_JSON, encoding="utf-8") as f:
        data = json.load(f)
    meta = {}
    for op in data["operators"]:
        meta[op["type"]] = op
    return meta


def gen_cmake_lists(dir_name, type_name):
    """生成 CMakeLists.txt"""
    return f"""# {dir_name} mock 算子库
# 编译为 {dir_name}.dll，通过 manifest.json 提供元数据
# 由 OperatorPluginLoader 在主程序启动时加载

add_library({dir_name} SHARED
    {dir_name}Operator.cpp
    {dir_name}Operator.h
)

target_include_directories({dir_name} PRIVATE
    $<BUILD_INTERFACE:${{CMAKE_CURRENT_SOURCE_DIR}}/../../../OperatorSDK/include>
    $<BUILD_INTERFACE:${{CMAKE_CURRENT_SOURCE_DIR}}/../../../../include>
)

target_link_libraries({dir_name} PRIVATE
    OperatorSDK
    Qt6::Core
    ${{OpenCV_LIBS}}
)

target_compile_features({dir_name} PRIVATE cxx_std_17)

set_target_properties({dir_name} PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${{CMAKE_BINARY_DIR}}/bin/operators
    LIBRARY_OUTPUT_DIRECTORY ${{CMAKE_BINARY_DIR}}/bin/operators
)

add_custom_command(TARGET {dir_name} POST_BUILD
    COMMAND ${{CMAKE_COMMAND}} -E copy_if_different
        "${{CMAKE_CURRENT_SOURCE_DIR}}/manifest.json"
        "$<TARGET_FILE_DIR:{dir_name}>/{dir_name}.json"
    COMMENT "Deploying {dir_name} manifest.json"
)
"""


def gen_manifest(type_name, cn_name, category, description, params, is_mock=True, agent_role=None):
    """生成 manifest.json"""
    manifest = {
        "type": type_name,
        "version": "1.0.0",
        "cnName": cn_name,
        "category": category,
        "iconPath": "",
        "description": description,
        "library": f"{'Mock' if is_mock and not type_name.startswith('Mock') else type_name}.dll",
        "isMock": is_mock,
    }
    if agent_role:
        manifest["agentRole"] = agent_role
    if params:
        manifest["params"] = params
    return json.dumps(manifest, ensure_ascii=False, indent=4)


def gen_operator_header(dir_name, type_name, base_class="MockOperatorBase"):
    """生成算子头文件"""
    return f"""#ifndef QDV_{dir_name.upper()}_OPERATOR_H
#define QDV_{dir_name.upper()}_OPERATOR_H

#include "OperatorSDK/{base_class}.h"

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#   define QDV_EXPORT Q_DECL_EXPORT
#else
#   define QDV_EXPORT __attribute__((visibility("default")))
#endif

namespace QDV {{

/// {type_name} mock 算子（继承 {base_class}）
class {dir_name}Operator : public {base_class} {{
public:
    {dir_name}Operator() {{ m_name = "{type_name} mock"; }}
    ~{dir_name}Operator() override = default;

    QString type() const override {{ return "{type_name}"; }}
    IOperator* clone() const override {{ return new {dir_name}Operator(*this); }}

protected:
    QString mockTag() const override {{ return "{type_name}"; }}
}};

}} // namespace QDV

extern "C" {{
    QDV_EXPORT const char* operator_type();
    QDV_EXPORT const char* operator_version();
    QDV_EXPORT QDV::IOperator* create_operator();
}}

#endif // QDV_{dir_name.upper()}_OPERATOR_H
"""


def gen_operator_cpp(dir_name, type_name):
    """生成算子实现文件"""
    return f"""#include "{dir_name}Operator.h"

extern "C" {{
    QDV_EXPORT const char* operator_type() {{ return "{type_name}"; }}
    QDV_EXPORT const char* operator_version() {{ return "1.0.0"; }}
    QDV_EXPORT QDV::IOperator* create_operator() {{ return new QDV::{dir_name}Operator(); }}
}}
"""


def gen_agent_operator_header(dir_name, type_name, cn_name, agent_role, description):
    """生成 Agent mock 算子头文件（带 agentRole 字段）"""
    return f"""#ifndef QDV_{dir_name.upper()}_OPERATOR_H
#define QDV_{dir_name.upper()}_OPERATOR_H

#include "OperatorSDK/MockOperatorBase.h"

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#   define QDV_EXPORT Q_DECL_EXPORT
#else
#   define QDV_EXPORT __attribute__((visibility("default")))
#endif

namespace QDV {{

/// {cn_name} mock 算子（对应 Agent 角色: {agent_role}）
/// 用途: {description}
class {dir_name}Operator : public MockOperatorBase {{
public:
    {dir_name}Operator() {{ m_name = "{cn_name}"; }}
    ~{dir_name}Operator() override = default;

    QString type() const override {{ return "{type_name}"; }}
    IOperator* clone() const override {{ return new {dir_name}Operator(*this); }}

    /// Agent 角色标识
    QString agentRole() const {{ return "{agent_role}"; }}

protected:
    QString mockTag() const override {{ return "{type_name}"; }}

    /// 重写 buildMockResult 以携带 Agent 角色信息
    QJsonObject buildMockResult(const QString& outPath, const cv::Mat& input) const override {{
        QJsonObject data = MockOperatorBase::buildMockResult(outPath, input);
        data["agentRole"] = "{agent_role}";
        data["description"] = "{description}";
        return data;
    }}
}};

}} // namespace QDV

extern "C" {{
    QDV_EXPORT const char* operator_type();
    QDV_EXPORT const char* operator_version();
    QDV_EXPORT QDV::IOperator* create_operator();
}}

#endif // QDV_{dir_name.upper()}_OPERATOR_H
"""


def gen_agent_operator_cpp(dir_name, type_name):
    """生成 Agent mock 算子实现文件"""
    return f"""#include "{dir_name}Operator.h"

extern "C" {{
    QDV_EXPORT const char* operator_type() {{ return "{type_name}"; }}
    QDV_EXPORT const char* operator_version() {{ return "1.0.0"; }}
    QDV_EXPORT QDV::IOperator* create_operator() {{ return new QDV::{dir_name}Operator(); }}
}}
"""


def main():
    meta = load_operators_metadata()
    print(f"Loaded {len(meta)} operators from operators.json")

    generated = []
    skipped = []

    # 1. 生成 32 个 mock 补全算子
    for type_name in MOCK_TYPES:
        if type_name not in meta:
            skipped.append(f"NOT_IN_JSON: {type_name}")
            continue

        op_meta = meta[type_name]
        dir_name = f"Mock{type_name}"
        op_dir = EXT_DIR / dir_name

        if op_dir.exists():
            skipped.append(f"EXISTS: {dir_name}")
            continue

        op_dir.mkdir(parents=True)

        # CMakeLists.txt
        (op_dir / "CMakeLists.txt").write_text(
            gen_cmake_lists(dir_name, type_name), encoding="utf-8")

        # manifest.json
        manifest = gen_manifest(
            type_name=type_name,
            cn_name=op_meta.get("cnName", type_name),
            category=op_meta.get("category", "未分类"),
            description=f"[MOCK] {op_meta.get('description', type_name)}",
            params=op_meta.get("params", []),
            is_mock=True,
        )
        (op_dir / "manifest.json").write_text(manifest, encoding="utf-8")

        # Operator.h
        (op_dir / f"{dir_name}Operator.h").write_text(
            gen_operator_header(dir_name, type_name), encoding="utf-8")

        # Operator.cpp
        (op_dir / f"{dir_name}Operator.cpp").write_text(
            gen_operator_cpp(dir_name, type_name), encoding="utf-8")

        generated.append(f"MOCK: {dir_name} -> {type_name}")

    # 2. 生成 5 个 Agent 角色 mock 算子
    for type_name, cn_name, category, agent_role, description in AGENT_MOCKS:
        dir_name = type_name  # MockClassify, MockValidate 等
        op_dir = EXT_DIR / dir_name

        if op_dir.exists():
            skipped.append(f"EXISTS: {dir_name}")
            continue

        op_dir.mkdir(parents=True)

        # CMakeLists.txt
        (op_dir / "CMakeLists.txt").write_text(
            gen_cmake_lists(dir_name, type_name), encoding="utf-8")

        # manifest.json
        manifest = gen_manifest(
            type_name=type_name,
            cn_name=cn_name,
            category=category,
            description=description,
            params=[],
            is_mock=True,
            agent_role=agent_role,
        )
        (op_dir / "manifest.json").write_text(manifest, encoding="utf-8")

        # Agent Operator.h（带 agentRole）
        (op_dir / f"{dir_name}Operator.h").write_text(
            gen_agent_operator_header(dir_name, type_name, cn_name, agent_role, description),
            encoding="utf-8")

        # Operator.cpp
        (op_dir / f"{dir_name}Operator.cpp").write_text(
            gen_agent_operator_cpp(dir_name, type_name), encoding="utf-8")

        generated.append(f"AGENT: {dir_name} -> {agent_role}")

    print(f"\n=== Generated {len(generated)} mock operators ===")
    for g in generated:
        print(f"  {g}")

    if skipped:
        print(f"\n=== Skipped {len(skipped)} ===")
        for s in skipped:
            print(f"  {s}")

    # 输出 CMakeLists.txt 需要添加的 add_subdirectory 行
    print(f"\n=== 需要添加到主 CMakeLists.txt 的行 ===")
    for type_name in MOCK_TYPES:
        dir_name = f"Mock{type_name}"
        if (EXT_DIR / dir_name).exists():
            print(f"add_subdirectory(src/operators/extensions/{dir_name})")
    for type_name, _, _, _, _ in AGENT_MOCKS:
        if (EXT_DIR / type_name).exists():
            print(f"add_subdirectory(src/operators/extensions/{type_name})")


if __name__ == "__main__":
    main()
