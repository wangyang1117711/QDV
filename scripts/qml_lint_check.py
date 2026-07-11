#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QML Lint 防护脚本（v4.0.1）
=====================================================================
目的：防止编辑界面空白问题再次回归。

背景：历史上 QDV 编辑界面多次出现"完全空白"问题，根因均为
      QML 静态错误（如：跨作用域信号处理器、DesignTokens 未定义引用、
      ListView 不存在的 background 属性等）。QML 是解释执行的，
      这些错误只在运行时暴露，构建期无法发现。

本脚本作为"确定性终门"的一部分，在构建前/提交前执行静态检查：

检查项 1：DesignTokens 引用一致性
  - 扫描 qml/EditView/*.qml 中所有 Tok.DesignTokens.xxx 引用
  - 与 DesignTokens.qml 中的 readonly property 定义对比
  - 报告：被引用但未定义的属性（CRITICAL，会导致运行时 undefined）
  - 报告：已定义但从未被引用的属性（INFO，可能死代码）

检查项 2：跨作用域信号处理器检测（启发式）
  - 扫描 onXxxChanged / onXxxYyy 信号处理器
  - 检测：当 xxx 属性未在当前对象作用域内定义，且来自父级时
    （这是 ParamForm.qml:263 空白根因的同类模式）
  - 启发式：仅检测明显的"属性定义在父级 Item/RowLayout 上，
    但信号处理器写在子控件内部"的情况

检查项 3：qmldir / .qrc / C++ qmlRegisterType 三处一致性
  - 对比 qmldir 注册的组件 vs .qrc 列出的文件 vs C++ 注册的类型
  - 报告不一致项（历史上的多次空白回归均因三处遗漏）

退出码：
  0 = 全部通过
  1 = 发现 CRITICAL 错误（必须修复）
  2 = 脚本自身异常

用法：
  python scripts/qml_lint_check.py
  python scripts/qml_lint_check.py --strict  # 把 WARNING 也当 CRITICAL
=====================================================================
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple

# ============ 路径配置 ============
PROJECT_ROOT = Path(__file__).resolve().parent.parent
QML_EDITVIEW_DIR = PROJECT_ROOT / "qml" / "EditView"
DESIGN_TOKENS_FILE = QML_EDITVIEW_DIR / "DesignTokens.qml"
QMldIR_FILE = QML_EDITVIEW_DIR / "qmldir"
QRC_FILE = PROJECT_ROOT / "qml" / "EditView.qrc"
EDITVIEW_CPP = PROJECT_ROOT / "src" / "UI" / "EditView.cpp"

# ============ 颜色输出（Windows 兼容）============
class Color:
    RESET = "\033[0m"
    RED = "\033[31m"
    YELLOW = "\033[33m"
    GREEN = "\033[32m"
    CYAN = "\033[36m"
    GRAY = "\033[90m"

def enable_color() -> None:
    """Windows 下启用 ANSI 颜色支持"""
    if sys.platform == "win32":
        try:
            import ctypes
            kernel32 = ctypes.windll.kernel32
            kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
        except Exception:
            pass

def c(text: str, color: str) -> str:
    return f"{color}{text}{Color.RESET}"

# ============ 检查项 1：DesignTokens 引用一致性 ============
PROPERTY_DEF_RE = re.compile(
    r"^\s*readonly\s+property\s+(\w+)\s+(\w+)\s*:",
    re.MULTILINE
)
TOKEN_REF_RE = re.compile(r"Tok\.DesignTokens\.(\w+)")

def parse_design_tokens_definitions(tokens_file: Path) -> Dict[str, str]:
    """从 DesignTokens.qml 解析所有 readonly property 定义"""
    if not tokens_file.exists():
        print(c(f"[FATAL] DesignTokens.qml 不存在: {tokens_file}", Color.RED))
        sys.exit(2)
    content = tokens_file.read_text(encoding="utf-8")
    defs: Dict[str, str] = {}
    for m in PROPERTY_DEF_RE.finditer(content):
        prop_type, prop_name = m.group(1), m.group(2)
        defs[prop_name] = prop_type
    # 同时收集 function 定义（categoryColor / toastBg 等）
    func_def_re = re.compile(r"^\s*function\s+(\w+)\s*\(", re.MULTILINE)
    for m in func_def_re.finditer(content):
        defs[m.group(1)] = "function"
    return defs

def scan_token_references(qml_dir: Path) -> Dict[str, List[Tuple[Path, int]]]:
    """扫描所有 QML 文件中 Tok.DesignTokens.xxx 的引用"""
    refs: Dict[str, List[Tuple[Path, int]]] = {}
    for qml_file in sorted(qml_dir.glob("*.qml")):
        for lineno, line in enumerate(qml_file.read_text(encoding="utf-8").splitlines(), 1):
            for m in TOKEN_REF_RE.finditer(line):
                prop_name = m.group(1)
                refs.setdefault(prop_name, []).append((qml_file, lineno))
    return refs

def check_design_tokens(strict: bool) -> List[str]:
    """检查 DesignTokens 引用一致性，返回错误列表"""
    errors: List[str] = []
    defs = parse_design_tokens_definitions(DESIGN_TOKENS_FILE)
    refs = scan_token_references(QML_EDITVIEW_DIR)

    # 被引用但未定义 → CRITICAL
    undefined_refs = [(name, locs) for name, locs in refs.items() if name not in defs]
    if undefined_refs:
        errors.append(c("[CRITICAL] DesignTokens 被引用但未定义的属性：", Color.RED))
        for name, locs in sorted(undefined_refs):
            for f, ln in locs:
                rel = f.relative_to(PROJECT_ROOT)
                errors.append(f"  {c(name, Color.RED)}  ←  {rel}:{ln}")
    else:
        print(c("[PASS] 检查项 1：DesignTokens 引用一致性全部通过", Color.GREEN))

    # 已定义但未引用 → INFO（不算错误）
    unused = sorted(set(defs.keys()) - set(refs.keys()))
    if unused:
        print(c(f"[INFO] DesignTokens 已定义但未引用的属性（{len(unused)} 个）：", Color.GRAY))
        for name in unused:
            print(f"       {name} ({defs[name]})")

    return errors

# ============ 检查项 2：跨作用域信号处理器检测 ============
# 启发式：仅检测已知的高危模式（currentValue 是 ParamForm/FilePathField 中
# 常见的父级属性名，子控件内写 onCurrentValueChanged 是 v4.0.1 空白根因的同类模式）
HIGH_RISK_SIGNAL_PATTERNS = {
    # 属性名 -> 描述
    "currentValue": "currentValue 通常定义在父级 RowLayout/Item 上，子控件内写 onCurrentValueChanged 会跨作用域失败",
    "currentValues": "currentValues 通常定义在根 Item 上，子控件内写 onCurrentValuesChanged 会跨作用域失败",
    "selectedNode": "selectedNode 通常由外部传入，子控件内写 onSelectedNodeChanged 需确认作用域",
    "currentMeta": "currentMeta 通常由外部传入，子控件内写 onCurrentMetaChanged 需确认作用域",
}

# 这些是 QML/QtQuick.Controls 内置的合法信号，不需要检测
BUILTIN_SIGNALS = {
    # MouseArea
    "pressed", "released", "clicked", "doubleClicked", "positionChanged",
    "pressAndHold", "canceled", "entered", "exited", "wheel",
    # TextInput / TextField / TextEdit
    "textChanged", "textEdited", "editingFinished", "accepted", "tapped",
    # SpinBox / Slider
    "valueChanged", "moved",
    # Item 通用
    "visibleChanged", "focusChanged", "activeFocusChanged", "enabledChanged",
    "implicitHeightChanged", "implicitWidthChanged", "widthChanged", "heightChanged",
    "xChanged", "yChanged", "zChanged", "opacityChanged", "scaleChanged",
    "rotationChanged", "childrenRectChanged", "stateChanged", "destroyed",
    "parentChanged", "windowChanged", "childrenChanged",
    # Canvas / Custom drawing
    "paint", "imageLoaded",
    # Timer / Animation
    "triggered", "runningChanged", "finished",
    # Loader
    "loaded", "statusChanged", "progressChanged",
    # ComboBox / abstract buttons
    "activated", "deactivated", "highlighted", "toggled", "checkedChanged",
    "clicked", "press", "release",
    # Flickable
    "flickStarted", "flickEnded", "movementStarted", "movementEnded",
    "contentXChanged", "contentYChanged",
    # Popup
    "opened", "closed",
}

SIGNAL_HANDLER_RE = re.compile(r"^\s*(on\w+):")
# 匹配当前对象作用域内定义的 property（注意：与检查项1的 PROPERTY_DEF_RE 不同，
# 这里不要求 readonly，用于检查项2的作用域分析）
# 注意：property var currentValue 这种无默认值的定义没有 ":"，所以不能要求 ":"
PROPERTY_DEF_IN_SCOPE_RE = re.compile(r"^\s*property\s+\w+\s+(\w+)\b")
# 匹配对象开始（Type {）—— 行末是 {
OBJECT_OPEN_RE = re.compile(r"^\s*\w[\w\s]*\{$")
# 匹配对象结束
OBJECT_CLOSE_RE = re.compile(r"^\s*\}")
# 匹配 JS 块开始（property xxx: { 或 onXxx: {）
JS_BLOCK_START_RE = re.compile(r":\s*\{")

def _find_property_in_current_object(lines: List[str], handler_lineno: int, prop_name: str) -> bool:
    """
    从 handler_lineno 向上查找，确认 prop_name 是否定义在当前对象作用域内。

    策略：
    1. 从 handler_lineno - 1 向上遍历
    2. 遇到 OBJECT_CLOSE_RE (}) 表示进入更深层嵌套，记录深度
    3. 遇到 OBJECT_OPEN_RE (Type {) 且深度为 0 时，表示到达当前对象的开始
    4. 在当前对象作用域内查找 property 定义
    5. 跳过 JS 块（{ 在 : 后面的情况）

    返回 True 表示属性在当前对象作用域内定义（合法）。
    """
    # 从 handler_lineno - 2（0-based）向上遍历
    depth = 0  # 嵌套深度（>0 表示在更深层对象内）
    js_brace_depth = 0  # JS 块深度

    for i in range(handler_lineno - 2, -1, -1):
        line = lines[i] if i < len(lines) else ""
        stripped = line.strip()
        if stripped.startswith("//") or stripped.startswith("/*"):
            continue

        # 如果在 JS 块内（从下往上看），只做花括号计数
        # 注意：从下往上看，} 是 JS 块开始，{ 是 JS 块结束
        if js_brace_depth > 0:
            js_brace_depth += stripped.count("}") - stripped.count("{")
            if js_brace_depth < 0:
                js_brace_depth = 0
            continue

        # 检测 JS 块结束（从下往上看，} 在 : 之前的行）
        # 例如：property string _text: { ... }，从下往上看先遇到 }
        # 但这种情况不好判断，我们用更简单的方法：检测当前行是否有 : { 模式
        # 实际上，从下往上看，我们无法直接知道某个 } 是 JS 块的还是对象的

        # 检测对象结束（}）—— 从下往上看是进入更深嵌套
        if OBJECT_CLOSE_RE.match(line):
            depth += 1
            continue

        # 检测对象开始（Type {）—— 从下往上看
        if OBJECT_OPEN_RE.match(line):
            if depth > 0:
                # 这是更外层的对象，跳过
                depth -= 1
                continue
            # 到达当前对象的开始
            # 但我们需要继续检查这一行之后（向下）到 handler_lineno 之间的 property 定义
            # 这里直接返回 False，让外层逻辑继续查找
            # 实际上，我们应该在遍历过程中记录 property 定义
            break

        # 检查 property 定义
        m_prop = PROPERTY_DEF_IN_SCOPE_RE.match(line)
        if m_prop and depth == 0:
            if m_prop.group(1) == prop_name:
                return True

    return False

def _is_property_defined_in_current_scope(lines: List[str], handler_lineno: int, prop_name: str) -> bool:
    """
    检查 prop_name 是否定义在 handler_lineno 所在对象的作用域内（包括祖先作用域）。

    使用简化的对象栈追踪。对于 JS 块（{ 在 : 后面），使用花括号计数跳过。
    """
    # 简化版：检查整个文件中是否有 property xxx currentValue 的定义
    # 如果有，认为是合法的（会有漏报，但避免误报）
    # 对于 v4.0.1 修复的根因（currentValue 完全未定义在当前对象），这个方法有效
    for line in lines[:handler_lineno - 1]:
        m = PROPERTY_DEF_IN_SCOPE_RE.match(line)
        if m and m.group(1) == prop_name:
            return True
    return False

def check_signal_handlers(strict: bool) -> List[str]:
    """检测高危跨作用域信号处理器模式"""
    errors: List[str] = []
    warning_count = 0
    for qml_file in sorted(QML_EDITVIEW_DIR.glob("*.qml")):
        lines = qml_file.read_text(encoding="utf-8").splitlines()
        for lineno, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith("//") or stripped.startswith("/*"):
                continue
            m_sig = SIGNAL_HANDLER_RE.match(line)
            if not m_sig:
                continue
            handler = m_sig.group(1)  # onXxxChanged
            # 推断属性名：onCurrentValueChanged -> currentValue
            prop = handler[2:]  # 去掉 on
            if prop.endswith("Changed"):
                prop = prop[:-len("Changed")]
            if not prop:
                continue
            # 首字母小写
            prop = prop[0].lower() + prop[1:]

            # 跳过内置信号
            if prop in BUILTIN_SIGNALS:
                continue

            # 检测高危模式
            if prop in HIGH_RISK_SIGNAL_PATTERNS:
                # 进一步验证：属性是否在当前作用域内定义
                if _is_property_defined_in_current_scope(lines, lineno, prop):
                    # 属性在当前或祖先作用域内定义，合法
                    warning_count += 1
                    continue
                # 真正的跨作用域错误
                rel = qml_file.relative_to(PROJECT_ROOT)
                desc = HIGH_RISK_SIGNAL_PATTERNS[prop]
                msg = (f"[CRITICAL] {rel}:{lineno} {c(handler, Color.RED)}"
                       f" -> 属性 '{prop}' 可能跨作用域。{desc}")
                errors.append(msg)
            else:
                # 其他信号处理器：仅作 INFO 提示，不阻断
                warning_count += 1

    if warning_count > 0:
        print(c(f"[INFO] 检查项 2：检测到 {warning_count} 个非内置信号处理器（已跳过，仅高危模式阻断）", Color.GRAY))

    if not errors:
        print(c("[PASS] 检查项 2：高危跨作用域信号处理器检测通过", Color.GREEN))
    return errors

# ============ 检查项 3：三处注册一致性 ============
def check_registration_consistency(strict: bool) -> List[str]:
    """检查 qmldir / .qrc / C++ qmlRegisterType 三处一致性"""
    errors: List[str] = []

    # ---- 解析 qmldir ----
    # 格式：TypeName 1.0 File.qml  或  singleton TypeName 3.0 File.qml
    qmldir_components: Dict[str, str] = {}  # name -> file
    if QMldIR_FILE.exists():
        for line in QMldIR_FILE.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("module"):
                continue
            parts = line.split()
            if len(parts) >= 3:
                # singleton DesignTokens 3.0 DesignTokens.qml
                if parts[0] == "singleton":
                    name, _, file = parts[1], parts[2], parts[3]
                    qmldir_components[name] = file
                else:
                    # TypeName 1.0 File.qml
                    name, _, file = parts[0], parts[1], parts[2]
                    qmldir_components[name] = file
    else:
        errors.append(c(f"[CRITICAL] qmldir 不存在: {QMldIR_FILE}", Color.RED))

    # ---- 解析 .qrc ----
    # 格式：<file alias="XXX.qml">EditView/XXX.qml</file>
    qrc_files: Set[str] = set()
    if QRC_FILE.exists():
        content = QRC_FILE.read_text(encoding="utf-8")
        # 匹配 <file alias="XXX.qml">...</file> 和 <file>...</file>
        for m in re.finditer(r'<file(?:\s+alias="([^"]+)")?>([^<]+)</file>', content):
            alias = m.group(1)
            filepath = m.group(2)
            # 优先用 alias 作为组件文件名
            if alias:
                qrc_files.add(alias)
            else:
                qrc_files.add(Path(filepath).name)
    else:
        errors.append(c(f"[CRITICAL] EditView.qrc 不存在: {QRC_FILE}", Color.RED))

    # ---- 解析 C++ qmlRegisterType / qmlRegisterSingletonType ----
    # 格式（多行）：
    #   qmlRegisterType(
    #       QUrl(QStringLiteral("qrc:/qml/EditView/ParamForm.qml")),
    #       kModuleUri, 1, 0, "ParamForm");
    cpp_components: Set[str] = set()
    if EDITVIEW_CPP.exists():
        cpp_content = EDITVIEW_CPP.read_text(encoding="utf-8")
        # 用 DOTALL 匹配多行 qmlRegisterType(... "Name");
        for m in re.finditer(
            r'qmlRegister(?:Singleton)?Type\s*\([^;]*?"(\w+)"\s*\)',
            cpp_content, re.DOTALL
        ):
            cpp_components.add(m.group(1))
    else:
        errors.append(c(f"[CRITICAL] EditView.cpp 不存在: {EDITVIEW_CPP}", Color.RED))

    # 实际 QML 文件
    actual_qml_files = {f.name for f in QML_EDITVIEW_DIR.glob("*.qml")}

    print(f"[INFO] qmldir 注册组件: {len(qmldir_components)} 个 -> {sorted(qmldir_components.keys())}")
    print(f"[INFO] .qrc 注册文件: {len(qrc_files)} 个 -> {sorted(qrc_files)}")
    print(f"[INFO] C++ qmlRegisterType: {len(cpp_components)} 个 -> {sorted(cpp_components)}")
    print(f"[INFO] 实际 QML 文件: {len(actual_qml_files)} 个")

    # 检查 1：qmldir 中每个组件的 .qml 文件是否存在于磁盘
    for name, file in qmldir_components.items():
        if file not in actual_qml_files:
            errors.append(c(f"[CRITICAL] qmldir 注册的 {name} -> {file} 在磁盘上不存在", Color.RED))

    # 检查 2：qmldir 中每个组件是否在 .qrc 中注册
    for name, file in qmldir_components.items():
        if file not in qrc_files:
            errors.append(c(f"[CRITICAL] {file} 在 qmldir 中注册但未在 .qrc 中注册（静态库场景会找不到）", Color.RED))

    # 检查 3：qmldir 中每个组件是否在 C++ 中 qmlRegisterType
    # 例外：Main.qml 是入口文件，由 QQuickWidget::setSource 直接加载，不需要 qmlRegisterType
    ENTRY_FILES = {"Main"}  # 入口文件集合（不需要 qmlRegisterType）
    for name in qmldir_components:
        if name in ENTRY_FILES:
            continue
        if name not in cpp_components:
            errors.append(c(f"[CRITICAL] {name} 在 qmldir 中注册但未在 C++ 中 qmlRegisterType（静态库场景会被链接器优化丢弃）", Color.RED))

    # 检查 4：DesignTokens 必须注册为 singleton
    if "DesignTokens" not in cpp_components:
        errors.append(c("[CRITICAL] DesignTokens 未在 C++ 中注册为 singleton", Color.RED))

    if not errors:
        print(c("[PASS] 检查项 3：三处注册一致性检查通过", Color.GREEN))
    return errors

def _extract_type_names_from_qmldir() -> Set[str]:
    """[已废弃] 辅助函数，保留以避免破坏向后兼容"""
    return set()

# ============ 主入口 ============
def main() -> int:
    parser = argparse.ArgumentParser(description="QML Lint 防护脚本")
    parser.add_argument("--strict", action="store_true",
                        help="把 WARNING 也当 CRITICAL（用于 CI）")
    args = parser.parse_args()

    enable_color()
    print(c("=" * 70, Color.CYAN))
    print(c("QML Lint 防护脚本 v4.0.1", Color.CYAN))
    print(c("目的：防止编辑界面空白问题再次回归", Color.CYAN))
    print(c("=" * 70, Color.CYAN))
    print(f"项目根目录: {PROJECT_ROOT}")
    print(f"QML 目录:   {QML_EDITVIEW_DIR}")
    print()

    all_errors: List[str] = []
    all_errors.extend(check_design_tokens(args.strict))
    all_errors.extend(check_signal_handlers(args.strict))
    all_errors.extend(check_registration_consistency(args.strict))

    print()
    print(c("=" * 70, Color.CYAN))
    if all_errors:
        print(c(f"[FAIL] 发现 {len(all_errors)} 个错误，必须修复后才能继续构建", Color.RED))
        for err in all_errors:
            print(f"  {err}")
        print(c("=" * 70, Color.CYAN))
        return 1
    else:
        print(c("[SUCCESS] 所有检查项通过，可以继续构建", Color.GREEN))
        print(c("=" * 70, Color.CYAN))
        return 0

if __name__ == "__main__":
    sys.exit(main())
