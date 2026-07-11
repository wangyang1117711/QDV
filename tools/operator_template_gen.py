#!/usr/bin/env python3
"""算子模板生成脚本（通用工具）

根据算子类型名+参数列表+模式（mock/real），生成完整算子目录骨架。
- mock 模式: 继承 MockOperatorBase，仅做占位输出
- real 模式: 继承 IOperator，提供真实算法骨架（execute 需手写）

用法示例:
    # 生成 mock 算子骨架
    python tools/operator_template_gen.py --type MockNewOp --cnName "新mock算子" \
        --category "预处理" --mock

    # 生成真实算子骨架
    python tools/operator_template_gen.py --type NewFilter --cnName "新滤波器" \
        --category "预处理" --real \
        --params '[{"name":"ksize","type":0,"defaultValue":5,"minValue":1,"maxValue":31}]'

    # 生成带 Agent 角色的 mock 算子
    python tools/operator_template_gen.py --type MockNewAgent --cnName "新Agent算子" \
        --category "Agent协作" --mock --agent-role Reviewer \
        --description "模拟评审判定"
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXT_DIR = ROOT / "src" / "operators" / "extensions"


def is_valid_type_name(type_name: str) -> bool:
    """校验算子 type 名合法性：字母开头，仅含字母数字"""
    return bool(re.match(r"^[A-Za-z][A-Za-z0-9]*$", type_name))


def gen_cmake_lists(dir_name: str, type_name: str) -> str:
    """生成 CMakeLists.txt"""
    return f"""# {dir_name} 算子库
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


def gen_manifest(type_name: str, cn_name: str, category: str,
                 description: str, params: list, is_mock: bool,
                 agent_role: str = None) -> str:
    """生成 manifest.json"""
    manifest = {
        "type": type_name,
        "version": "1.0.0",
        "cnName": cn_name,
        "category": category,
        "iconPath": "",
        "description": description or f"[{'MOCK' if is_mock else 'REAL'}] {type_name}",
        "library": f"{type_name if type_name.startswith('Mock') else ('Mock' + type_name if is_mock else type_name)}.dll",
        "isMock": is_mock,
    }
    if agent_role:
        manifest["agentRole"] = agent_role
    if params:
        manifest["params"] = params
    return json.dumps(manifest, ensure_ascii=False, indent=4)


def gen_mock_header(dir_name: str, type_name: str, cn_name: str,
                    agent_role: str = None, description: str = None) -> str:
    """生成 mock 算子头文件（继承 MockOperatorBase）"""
    agent_methods = ""
    build_mock_override = ""
    if agent_role:
        agent_methods = f"""
    /// Agent 角色标识
    QString agentRole() const {{ return "{agent_role}"; }}"""
        build_mock_override = f"""

    /// 重写 buildMockResult 以携带 Agent 角色信息
    QJsonObject buildMockResult(const QString& outPath, const cv::Mat& input) const override {{
        QJsonObject data = MockOperatorBase::buildMockResult(outPath, input);
        data["agentRole"] = "{agent_role}";
        data["description"] = "{description or ''}";
        return data;
    }}"""

    doc_comment = f"/// {cn_name} mock 算子"
    if agent_role:
        doc_comment += f"（对应 Agent 角色: {agent_role}）\n/// 用途: {description or 'Agent 协作 mock'}"
    else:
        doc_comment += f"\n/// 占位输出，未做真实计算"

    return f"""#ifndef QDV_{dir_name.upper()}_OPERATOR_H
#define QDV_{dir_name.upper()}_OPERATOR_H

#include "OperatorSDK/MockOperatorBase.h"

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#   define QDV_EXPORT Q_DECL_EXPORT
#else
#   define QDV_EXPORT __attribute__((visibility("default")))
#endif

namespace QDV {{

{doc_comment}
class {dir_name}Operator : public MockOperatorBase {{
public:
    {dir_name}Operator() {{ m_name = "{cn_name}"; }}
    ~{dir_name}Operator() override = default;

    QString type() const override {{ return "{type_name}"; }}
    IOperator* clone() const override {{ return new {dir_name}Operator(*this); }}
{agent_methods}

protected:
    QString mockTag() const override {{ return "{type_name}"; }}{build_mock_override}
}};

}} // namespace QDV

extern "C" {{
    QDV_EXPORT const char* operator_type();
    QDV_EXPORT const char* operator_version();
    QDV_EXPORT QDV::IOperator* create_operator();
}}

#endif // QDV_{dir_name.upper()}_OPERATOR_H
"""


def gen_real_header(dir_name: str, type_name: str, cn_name: str,
                    description: str = None) -> str:
    """生成真实算子头文件（继承 IOperator）"""
    doc_comment = f"/// {cn_name} 算子（真实实现）"
    if description:
        doc_comment += f"\n/// {description}"

    return f"""#ifndef QDV_{dir_name.upper()}_OPERATOR_H
#define QDV_{dir_name.upper()}_OPERATOR_H

#include "OperatorSDK/IOperator.h"

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#   define QDV_EXPORT Q_DECL_EXPORT
#else
#   define QDV_EXPORT __attribute__((visibility("default")))
#endif

namespace QDV {{

{doc_comment}
class {dir_name}Operator : public IOperator {{
public:
    {dir_name}Operator() {{ m_name = "{cn_name}"; }}
    ~{dir_name}Operator() override = default;

    QString type() const override {{ return "{type_name}"; }}
    QString version() const override {{ return "1.0.0"; }}

    /// 真实执行接口（需手写实现）
    bool execute(const cv::Mat& input, ToolResult& result) override;
    bool configure(const QJsonObject& params) override;
    IOperator* clone() const override {{ return new {dir_name}Operator(*this); }}
}};

}} // namespace QDV

extern "C" {{
    QDV_EXPORT const char* operator_type();
    QDV_EXPORT const char* operator_version();
    QDV_EXPORT QDV::IOperator* create_operator();
}}

#endif // QDV_{dir_name.upper()}_OPERATOR_H
"""


def gen_mock_cpp(dir_name: str, type_name: str) -> str:
    """生成 mock 算子实现文件"""
    return f"""#include "{dir_name}Operator.h"

extern "C" {{
    QDV_EXPORT const char* operator_type() {{ return "{type_name}"; }}
    QDV_EXPORT const char* operator_version() {{ return "1.0.0"; }}
    QDV_EXPORT QDV::IOperator* create_operator() {{ return new QDV::{dir_name}Operator(); }}
}}
"""


def gen_real_cpp(dir_name: str, type_name: str, cn_name: str) -> str:
    """生成真实算子实现文件（含 TODO 标记）"""
    return f"""#include "{dir_name}Operator.h"

#include <QJsonObject>
#include <opencv2/imgproc.hpp>

namespace QDV {{

bool {dir_name}Operator::execute(const cv::Mat& input, ToolResult& result) {{
    // TODO: 实现真实的 {cn_name} 算法
    // 当前为骨架占位，按接口契约构造最小合法 ToolResult

    if (input.empty()) {{
        result.ok = false;
        result.data = QJsonObject{{{{"reason", "input image is empty"}}}};
        return false;
    }}

    // TODO: 替换为真实算法输出
    cv::Mat output = input.clone();

    result.ok = true;
    result.data = QJsonObject{{
        {{"reason", "[REAL] {type_name} executed (skeleton placeholder)"}},
        {{"outputSize", QString("%1x%2").arg(output.cols).arg(output.rows)}}
    }};
    result.score = 1.0;
    result.overlayImage = output;
    return true;
}}

bool {dir_name}Operator::configure(const QJsonObject& params) {{
    // TODO: 校验并存储参数
    m_params = params;
    return true;
}}

}} // namespace QDV

extern "C" {{
    QDV_EXPORT const char* operator_type() {{ return "{type_name}"; }}
    QDV_EXPORT const char* operator_version() {{ return "1.0.0"; }}
    QDV_EXPORT QDV::IOperator* create_operator() {{ return new QDV::{dir_name}Operator(); }}
}}
"""


def generate_operator(type_name: str, cn_name: str, category: str,
                      params: list, mode: str, description: str = None,
                      agent_role: str = None, force: bool = False,
                      ext_dir: Path = None) -> dict:
    """生成算子目录骨架

    参数:
        ext_dir: 算子扩展目录路径，默认为项目 src/operators/extensions/

    返回 dict: {"dir": str, "files": [str], "skipped": bool, "reason": str}
    """
    if not is_valid_type_name(type_name):
        return {
            "dir": None,
            "files": [],
            "skipped": True,
            "reason": f"invalid type name: {type_name} (must be alphanumeric, start with letter)"
        }

    is_mock = (mode == "mock")
    # 目录命名规则：mock 模式若 type 未以 Mock 开头则加 Mock 前缀
    if is_mock and not type_name.startswith("Mock"):
        dir_name = f"Mock{type_name}"
    else:
        dir_name = type_name

    base = ext_dir if ext_dir is not None else EXT_DIR
    op_dir = base / dir_name
    if op_dir.exists() and not force:
        return {
            "dir": str(op_dir),
            "files": [],
            "skipped": True,
            "reason": f"directory already exists: {op_dir} (use --force to overwrite)"
        }

    op_dir.mkdir(parents=True, exist_ok=True)
    files_written = []

    # 1) CMakeLists.txt
    cmake_path = op_dir / "CMakeLists.txt"
    cmake_path.write_text(gen_cmake_lists(dir_name, type_name), encoding="utf-8")
    files_written.append(str(cmake_path))

    # 2) manifest.json
    manifest_path = op_dir / "manifest.json"
    manifest_path.write_text(
        gen_manifest(type_name, cn_name, category, description, params,
                     is_mock, agent_role),
        encoding="utf-8"
    )
    files_written.append(str(manifest_path))

    # 3) Operator.h
    header_path = op_dir / f"{dir_name}Operator.h"
    if is_mock:
        header_path.write_text(
            gen_mock_header(dir_name, type_name, cn_name, agent_role, description),
            encoding="utf-8"
        )
    else:
        header_path.write_text(
            gen_real_header(dir_name, type_name, cn_name, description),
            encoding="utf-8"
        )
    files_written.append(str(header_path))

    # 4) Operator.cpp
    cpp_path = op_dir / f"{dir_name}Operator.cpp"
    if is_mock:
        cpp_path.write_text(gen_mock_cpp(dir_name, type_name), encoding="utf-8")
    else:
        cpp_path.write_text(gen_real_cpp(dir_name, type_name, cn_name), encoding="utf-8")
    files_written.append(str(cpp_path))

    return {
        "dir": str(op_dir),
        "files": files_written,
        "skipped": False,
        "reason": ""
    }


def main():
    parser = argparse.ArgumentParser(
        description="算子模板生成脚本：生成 mock/real 算子目录骨架",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument("--type", required=True, help="算子类型名（如 GaussFilter / MockNewOp）")
    parser.add_argument("--cn-name", required=True, help="中文显示名")
    parser.add_argument("--category", required=True, help="算子分类（如 预处理）")
    parser.add_argument("--description", default=None, help="算子描述")

    mode_group = parser.add_mutually_exclusive_group(required=True)
    mode_group.add_argument("--mock", action="store_true", help="生成 mock 算子（继承 MockOperatorBase）")
    mode_group.add_argument("--real", action="store_true", help="生成真实算子（继承 IOperator）")

    parser.add_argument("--params", default="[]", help="参数列表 JSON 字符串")
    parser.add_argument("--agent-role", default=None,
                        help="Agent 角色标识（仅 mock 模式，如 Reviewer/Gatekeeper）")
    parser.add_argument("--force", action="store_true", help="覆盖已存在目录")

    args = parser.parse_args()

    # 模式互斥已经由 argparse 保证
    mode = "mock" if args.mock else "real"

    # 解析参数 JSON
    try:
        params = json.loads(args.params)
        if not isinstance(params, list):
            raise ValueError("params must be a JSON array")
    except (json.JSONDecodeError, ValueError) as e:
        print(f"ERROR: invalid --params JSON: {e}", file=sys.stderr)
        sys.exit(2)

    # agent-role 仅 mock 模式允许
    if args.agent_role and mode != "mock":
        print("ERROR: --agent-role can only be used with --mock", file=sys.stderr)
        sys.exit(2)

    result = generate_operator(
        type_name=args.type,
        cn_name=args.cn_name,
        category=args.category,
        params=params,
        mode=mode,
        description=args.description,
        agent_role=args.agent_role,
        force=args.force,
    )

    if result["skipped"]:
        print(f"SKIPPED: {result['reason']}", file=sys.stderr)
        sys.exit(1)

    print(f"Generated {mode} operator: {args.type}")
    print(f"  Directory: {result['dir']}")
    print(f"  Files ({len(result['files'])}):")
    for f in result["files"]:
        print(f"    - {f}")
    print(f"\nNext steps:")
    print(f"  1. Add to root CMakeLists.txt: add_subdirectory(src/operators/extensions/{Path(result['dir']).name})")
    if mode == "real":
        print(f"  2. Implement execute() in {Path(result['files'][3]).name}")
    else:
        print(f"  2. (mock) Execute base class already provides placeholder behavior")


if __name__ == "__main__":
    main()
