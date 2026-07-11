#!/usr/bin/env python3
"""gen_operator_docs.py 单元测试

2 个测试用例:
1. Markdown 文档生成完整性（功能清单 + 技术文档均生成）
2. 文档内容包含算子元数据（数量匹配 + mock 标注）
"""
import json
import sys
from pathlib import Path

import pytest

TOOLS_DIR = Path(__file__).resolve().parent.parent.parent / "tools"
sys.path.insert(0, str(TOOLS_DIR))

from gen_operator_docs import generate_docs  # noqa: E402


@pytest.fixture
def temp_project(tmp_path):
    """构造最小化项目结构（含 extensions + operators.json）"""
    # extensions 目录 + 2 个 manifest.json
    ext = tmp_path / "src" / "operators" / "extensions"
    ext.mkdir(parents=True)

    # mock 算子
    mock_dir = ext / "MockFoo"
    mock_dir.mkdir()
    (mock_dir / "manifest.json").write_text(json.dumps({
        "type": "MockFoo",
        "version": "1.0.0",
        "cnName": "mock算子",
        "category": "测试",
        "description": "[MOCK] 占位",
        "library": "MockFoo.dll",
        "isMock": True,
        "params": [{"name": "k", "type": 0, "defaultValue": 1}],
    }, ensure_ascii=False), encoding="utf-8")

    # 真实算子
    real_dir = ext / "Bar"
    real_dir.mkdir()
    (real_dir / "manifest.json").write_text(json.dumps({
        "type": "Bar",
        "version": "1.0.0",
        "cnName": "真实算子",
        "category": "测试",
        "description": "真实实现",
        "library": "Bar.dll",
        "isMock": False,
    }, ensure_ascii=False), encoding="utf-8")

    # operators.json（含一个仅内置、无 manifest 的算子）
    ops_json = tmp_path / "config" / "operators.json"
    ops_json.parent.mkdir(parents=True)
    ops_json.write_text(json.dumps({
        "operators": [
            {"type": "Bar", "cnName": "真实算子", "category": "测试",
             "description": "真实实现", "params": []},
            {"type": "BuiltinOnly", "cnName": "仅内置", "category": "内置",
             "description": "无 manifest", "params": []},
        ]
    }, ensure_ascii=False), encoding="utf-8")

    return tmp_path


def test_generate_md_docs(temp_project, tmp_path):
    """用例1: Markdown 文档生成完整性"""
    output_dir = tmp_path / "docs_out"
    files = generate_docs(output_dir, fmt="md", root_dir=temp_project)

    # 生成 2 个 md 文件
    assert len(files) == 2
    for f in files:
        assert Path(f).exists()
        assert f.endswith(".md")

    # 功能清单存在
    list_file = output_dir / "算子功能清单.md"
    assert list_file.exists()
    content = list_file.read_text(encoding="utf-8")
    assert "# 算子功能清单" in content
    assert "| Type |" in content  # 表头

    # 技术文档存在
    tech_file = output_dir / "算子技术文档.md"
    assert tech_file.exists()
    tech_content = tech_file.read_text(encoding="utf-8")
    assert "# 算子技术文档" in tech_content


def test_doc_content_includes_operators(temp_project, tmp_path):
    """用例2: 文档内容包含所有算子（含 mock 标注）"""
    output_dir = tmp_path / "docs_out2"
    generate_docs(output_dir, fmt="md", root_dir=temp_project)

    list_content = (output_dir / "算子功能清单.md").read_text(encoding="utf-8")

    # 3 个算子全部出现：MockFoo / Bar / BuiltinOnly
    assert "MockFoo" in list_content
    assert "Bar" in list_content
    assert "BuiltinOnly" in list_content

    # mock 标注正确
    # MockFoo 行应含"是"（isMock=true），Bar 行应含"否"（isMock=false）
    lines = list_content.splitlines()
    mock_foo_line = next(l for l in lines if "MockFoo" in l)
    bar_line = next(l for l in lines if "`Bar`" in l)
    assert "是" in mock_foo_line  # MockFoo 是 mock
    assert "否" in bar_line       # Bar 不是 mock

    # 技术文档也包含算子
    tech_content = (output_dir / "算子技术文档.md").read_text(encoding="utf-8")
    assert "MockFoo" in tech_content
    assert "Bar" in tech_content
    # 分类标题出现
    assert "## 测试" in tech_content
    assert "## 内置" in tech_content


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
