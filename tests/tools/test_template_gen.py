#!/usr/bin/env python3
"""operator_template_gen.py 单元测试

3 个测试用例:
1. mock 算子骨架完整性（4 文件 + manifest isMock:true）
2. real 算子骨架完整性（4 文件 + cpp 含 TODO + manifest isMock:false）
3. Agent mock 算子（含 agentRole 字段 + header 含 agentRole() 方法）
"""
import json
import shutil
import sys
from pathlib import Path

import pytest

# 将 tools 目录加入 sys.path 以导入模块
TOOLS_DIR = Path(__file__).resolve().parent.parent.parent / "tools"
sys.path.insert(0, str(TOOLS_DIR))

from operator_template_gen import generate_operator  # noqa: E402


@pytest.fixture
def temp_ext_dir(tmp_path):
    """临时算子扩展目录（测试后自动清理）"""
    ext = tmp_path / "extensions"
    ext.mkdir()
    yield ext
    # tmp_path 由 pytest 自动清理


def test_generate_mock_operator_skeleton(temp_ext_dir):
    """用例1: 生成 mock 算子骨架完整性"""
    result = generate_operator(
        type_name="MockTestOp",
        cn_name="测试mock算子",
        category="测试分类",
        params=[{"name": "ksize", "type": 0, "defaultValue": 5}],
        mode="mock",
        description="测试用 mock 算子",
        ext_dir=temp_ext_dir,
    )

    # 未跳过
    assert not result["skipped"], f"unexpected skip: {result['reason']}"

    # 4 个文件全部生成
    assert len(result["files"]) == 4
    op_dir = Path(result["dir"])
    assert (op_dir / "CMakeLists.txt").exists()
    assert (op_dir / "manifest.json").exists()
    assert (op_dir / "MockTestOpOperator.h").exists()
    assert (op_dir / "MockTestOpOperator.cpp").exists()

    # manifest 含 isMock:true
    manifest = json.loads((op_dir / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["type"] == "MockTestOp"
    assert manifest["isMock"] is True
    assert manifest["cnName"] == "测试mock算子"
    assert len(manifest["params"]) == 1

    # header 继承 MockOperatorBase
    header = (op_dir / "MockTestOpOperator.h").read_text(encoding="utf-8")
    assert "MockOperatorBase" in header
    assert "mockTag" in header

    # cpp 含 C API 导出
    cpp = (op_dir / "MockTestOpOperator.cpp").read_text(encoding="utf-8")
    assert "operator_type" in cpp
    assert "create_operator" in cpp
    assert "MockTestOp" in cpp


def test_generate_real_operator_skeleton(temp_ext_dir):
    """用例2: 生成 real 算子骨架完整性"""
    result = generate_operator(
        type_name="NewRealFilter",
        cn_name="新真实滤波器",
        category="预处理",
        params=[
            {"name": "ksize", "type": 0, "defaultValue": 5,
             "minValue": 1, "maxValue": 31}
        ],
        mode="real",
        description="真实滤波器骨架",
        ext_dir=temp_ext_dir,
    )

    assert not result["skipped"], f"unexpected skip: {result['reason']}"
    assert len(result["files"]) == 4

    op_dir = Path(result["dir"])
    # real 模式目录名不加 Mock 前缀
    assert op_dir.name == "NewRealFilter"

    # manifest isMock:false
    manifest = json.loads((op_dir / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["isMock"] is False
    assert manifest["type"] == "NewRealFilter"

    # header 继承 IOperator
    header = (op_dir / "NewRealFilterOperator.h").read_text(encoding="utf-8")
    assert "IOperator" in header
    assert "MockOperatorBase" not in header
    assert "execute" in header  # 声明 execute 方法

    # cpp 含 TODO 标记（真实实现待手写）
    cpp = (op_dir / "NewRealFilterOperator.cpp").read_text(encoding="utf-8")
    assert "TODO" in cpp
    assert "bool NewRealFilterOperator::execute" in cpp
    assert "configure" in cpp


def test_generate_agent_mock_operator(temp_ext_dir):
    """用例3: Agent mock 算子（含 agentRole 字段）"""
    result = generate_operator(
        type_name="MockTestAgent",
        cn_name="测试Agent算子",
        category="Agent协作",
        params=[],
        mode="mock",
        description="模拟测试Agent角色",
        agent_role="Reviewer",
        ext_dir=temp_ext_dir,
    )

    assert not result["skipped"], f"unexpected skip: {result['reason']}"

    op_dir = Path(result["dir"])
    # manifest 含 agentRole 字段
    manifest = json.loads((op_dir / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["isMock"] is True
    assert manifest["agentRole"] == "Reviewer"
    assert manifest["type"] == "MockTestAgent"

    # header 含 agentRole() 方法和 buildMockResult 重写
    header = (op_dir / "MockTestAgentOperator.h").read_text(encoding="utf-8")
    assert "agentRole" in header
    assert "Reviewer" in header
    assert "buildMockResult" in header  # 重写以携带 Agent 信息


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
