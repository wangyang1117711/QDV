#!/usr/bin/env python3
"""算子文档自动生成脚本

扫描 src/operators/extensions/*/manifest.json + config/operators.json
合并元数据（mock 算子标注 [MOCK]），按类别分组输出 Markdown 文档。

支持生成两种文档:
- 算子功能清单: 表格形式（type/cnName/category/参数/版本/是否mock）
- 算子技术文档: 每算子详细参数说明 + 输入输出格式 + 适用场景

用法:
    python tools/gen_operator_docs.py                          # 生成所有文档
    python tools/gen_operator_docs.py --format md              # 仅 Markdown
    python tools/gen_operator_docs.py --format html            # 仅 HTML（简化版）
    python tools/gen_operator_docs.py --output docs/algorithms  # 自定义输出目录
"""
import argparse
import json
import sys
from pathlib import Path
from collections import defaultdict
from html import escape

ROOT = Path(__file__).resolve().parent.parent
EXT_DIR = ROOT / "src" / "operators" / "extensions"
OPS_JSON = ROOT / "config" / "operators.json"
DEFAULT_OUTPUT = ROOT / "docs" / "algorithms"


def load_operators_json(ops_json: Path = None) -> dict:
    """加载 config/operators.json，返回 {type: meta} 字典"""
    path = ops_json if ops_json is not None else OPS_JSON
    if not path.exists():
        return {}
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    return {op["type"]: op for op in data.get("operators", [])}


def scan_extension_manifests(ext_dir: Path = None) -> list:
    """扫描 src/operators/extensions/*/manifest.json，返回 manifest 列表"""
    path = ext_dir if ext_dir is not None else EXT_DIR
    manifests = []
    if not path.exists():
        return manifests

    for manifest_path in sorted(path.glob("*/manifest.json")):
        try:
            with open(manifest_path, encoding="utf-8") as f:
                data = json.load(f)
            data["_source_dir"] = manifest_path.parent.name
            data["_manifest_path"] = str(manifest_path)
            manifests.append(data)
        except (json.JSONDecodeError, OSError) as e:
            # 跳过损坏的 manifest，不中断
            print(f"WARN: skip bad manifest {manifest_path}: {e}", file=sys.stderr)
    return manifests


def merge_metadata(operators_json: dict, manifests: list) -> list:
    """合并 operators.json 与 manifest.json 元数据

    优先使用 manifest.json（更具体），缺失字段从 operators.json 补充
    """
    merged = []
    seen_types = set()

    # 1) 先处理 manifest.json（独立 dll 算子，包含 mock）
    for m in manifests:
        op_type = m.get("type", "")
        if not op_type:
            continue
        json_meta = operators_json.get(op_type, {})
        record = {
            "type": op_type,
            "version": m.get("version", "1.0.0"),
            "cnName": m.get("cnName", json_meta.get("cnName", op_type)),
            "category": m.get("category", json_meta.get("category", "未分类")),
            "iconPath": m.get("iconPath", json_meta.get("iconPath", "")),
            "description": m.get("description", json_meta.get("description", "")),
            "library": m.get("library", f"{op_type}.dll"),
            "params": m.get("params", json_meta.get("params", [])),
            "outputs": json_meta.get("outputs", []),
            "isMock": m.get("isMock", False),
            "agentRole": m.get("agentRole"),
            "source": "extension",
            "source_dir": m.get("_source_dir", ""),
        }
        merged.append(record)
        seen_types.add(op_type)

    # 2) 补充 operators.json 中存在但 manifest.json 中不存在的算子（旧算子）
    for op_type, meta in operators_json.items():
        if op_type in seen_types:
            continue
        record = {
            "type": op_type,
            "version": "1.0.0",
            "cnName": meta.get("cnName", op_type),
            "category": meta.get("category", "未分类"),
            "iconPath": meta.get("iconPath", ""),
            "description": meta.get("description", ""),
            "library": f"{op_type}.dll",
            "params": meta.get("params", []),
            "outputs": meta.get("outputs", []),
            "isMock": False,
            "agentRole": None,
            "source": "operators_json",
            "source_dir": "",
        }
        merged.append(record)

    return merged


def group_by_category(operators: list) -> dict:
    """按 category 分组"""
    groups = defaultdict(list)
    for op in operators:
        groups[op["category"]].append(op)
    # 每组内按 type 排序
    for cat in groups:
        groups[cat].sort(key=lambda x: x["type"])
    return dict(groups)


def gen_function_list_md(operators: list) -> str:
    """生成算子功能清单 Markdown"""
    lines = [
        "# 算子功能清单",
        "",
        f"**自动生成** · 共 {len(operators)} 个算子",
        "",
        "| Type | 中文名 | 分类 | 参数数 | 版本 | 是否Mock | Agent角色 | 来源 |",
        "|------|--------|------|--------|------|----------|-----------|------|",
    ]
    for op in operators:
        params_count = len(op.get("params", []))
        is_mock = "是" if op.get("isMock") else "否"
        agent_role = op.get("agentRole") or "-"
        source = "扩展dll" if op["source"] == "extension" else "内置"
        lines.append(
            f"| `{op['type']}` | {op['cnName']} | {op['category']} | "
            f"{params_count} | {op['version']} | {is_mock} | {agent_role} | {source} |"
        )
    lines.append("")
    return "\n".join(lines)


def gen_tech_doc_md(operators: list) -> str:
    """生成算子技术文档 Markdown"""
    groups = group_by_category(operators)
    lines = [
        "# 算子技术文档",
        "",
        f"**自动生成** · 共 {len(operators)} 个算子，{len(groups)} 个分类",
        "",
        "---",
        "",
    ]

    for category in sorted(groups.keys()):
        ops_in_cat = groups[category]
        lines.append(f"## {category}（{len(ops_in_cat)} 个）")
        lines.append("")
        for op in ops_in_cat:
            mock_tag = " **[MOCK]**" if op.get("isMock") else ""
            agent_tag = f" · Agent: `{op['agentRole']}`" if op.get("agentRole") else ""
            lines.append(f"### {op['cnName']} (`{op['type']}`){mock_tag}{agent_tag}")
            lines.append("")
            lines.append(f"- **版本**: {op['version']}")
            lines.append(f"- **动态库**: `{op['library']}`")
            if op["source_dir"]:
                lines.append(f"- **源码目录**: `src/operators/extensions/{op['source_dir']}/`")
            lines.append(f"- **描述**: {op['description'] or '(无)'}")
            lines.append("")

            # 参数说明
            params = op.get("params", [])
            if params:
                lines.append("**参数**:")
                lines.append("")
                lines.append("| 参数名 | 中文名 | 类型 | 默认值 | 范围 | 说明 |")
                lines.append("|--------|--------|------|--------|------|------|")
                for p in params:
                    p_name = p.get("name", "")
                    p_cn = p.get("cnName", "")
                    p_type = p.get("type", "")
                    p_default = p.get("defaultValue", "")
                    p_min = p.get("minValue", "")
                    p_max = p.get("maxValue", "")
                    p_help = p.get("help", "").replace("|", "\\|")
                    range_str = f"{p_min} ~ {p_max}" if p_min is not None or p_max is not None else "-"
                    lines.append(
                        f"| `{p_name}` | {p_cn} | {p_type} | `{p_default}` | {range_str} | {p_help} |"
                    )
                lines.append("")
            else:
                lines.append("**参数**: 无")
                lines.append("")

            # 输出
            outputs = op.get("outputs", [])
            if outputs:
                lines.append("**输出**:")
                lines.append("")
                for out in outputs:
                    lines.append(
                        f"- `{out.get('name', '')}` ({out.get('typeName', '')}): {out.get('desc', '')}"
                    )
                lines.append("")
            lines.append("---")
            lines.append("")

    return "\n".join(lines)


def gen_function_list_html(operators: list) -> str:
    """生成算子功能清单 HTML（简化版）"""
    rows = []
    for op in operators:
        params_count = len(op.get("params", []))
        is_mock = "是" if op.get("isMock") else "否"
        rows.append(
            f"<tr>"
            f"<td><code>{escape(op['type'])}</code></td>"
            f"<td>{escape(op['cnName'])}</td>"
            f"<td>{escape(op['category'])}</td>"
            f"<td>{params_count}</td>"
            f"<td>{escape(op['version'])}</td>"
            f"<td>{is_mock}</td>"
            f"</tr>"
        )
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>算子功能清单</title>
<style>
body {{ font-family: 'Segoe UI', sans-serif; margin: 2em; }}
table {{ border-collapse: collapse; width: 100%; }}
th, td {{ border: 1px solid #ddd; padding: 8px; text-align: left; }}
th {{ background-color: #f2f2f2; }}
tr:nth-child(even) {{ background-color: #f9f9f9; }}
code {{ background: #f4f4f4; padding: 2px 4px; border-radius: 3px; }}
</style>
</head>
<body>
<h1>算子功能清单</h1>
<p>共 {len(operators)} 个算子</p>
<table>
<tr><th>Type</th><th>中文名</th><th>分类</th><th>参数数</th><th>版本</th><th>是否Mock</th></tr>
{"".join(rows)}
</table>
</body>
</html>
"""


def generate_docs(output_dir: Path, fmt: str = "md",
                  root_dir: Path = None) -> list:
    """生成文档主入口

    参数:
        output_dir: 文档输出目录
        fmt: 输出格式 (md/html/both)
        root_dir: 项目根目录（测试时传入临时目录），默认为脚本所在上级目录

    返回生成的文件路径列表
    """
    if root_dir is None:
        root_dir = ROOT
    ext_dir = root_dir / "src" / "operators" / "extensions"
    ops_json = root_dir / "config" / "operators.json"

    operators_json = load_operators_json(ops_json)
    manifests = scan_extension_manifests(ext_dir)
    operators = merge_metadata(operators_json, manifests)

    output_dir.mkdir(parents=True, exist_ok=True)
    files_written = []

    if fmt in ("md", "both"):
        # 功能清单
        list_path = output_dir / "算子功能清单.md"
        list_path.write_text(gen_function_list_md(operators), encoding="utf-8")
        files_written.append(str(list_path))

        # 技术文档
        tech_path = output_dir / "算子技术文档.md"
        tech_path.write_text(gen_tech_doc_md(operators), encoding="utf-8")
        files_written.append(str(tech_path))

    if fmt in ("html", "both"):
        html_path = output_dir / "算子功能清单.html"
        html_path.write_text(gen_function_list_html(operators), encoding="utf-8")
        files_written.append(str(html_path))

    return files_written


def main():
    parser = argparse.ArgumentParser(
        description="算子文档自动生成脚本",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument("--format", choices=["md", "html", "both"], default="md",
                        help="输出格式（默认 md）")
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT),
                        help=f"输出目录（默认 {DEFAULT_OUTPUT}）")
    args = parser.parse_args()

    output_dir = Path(args.output)
    files = generate_docs(output_dir, args.format)

    print(f"Generated {len(files)} document(s) in {output_dir}:")
    for f in files:
        print(f"  - {f}")


if __name__ == "__main__":
    main()
