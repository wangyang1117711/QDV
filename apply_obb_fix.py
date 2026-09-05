# -*- coding: utf-8 -*-
"""
闭环核对修复补丁：单丝计数 auto_label_server.py（权重探测排序 Bug）+ sync_obb_weight.bat
=======================================================================================
问题（来自 WorkBuddy 故障修复核对）：
  _resolve_obb_model_path() 用 sorted(glob, reverse=True) 按【路径字符串】排序，
  而 'runs/obb/obb_it2/weights/obb_current.pt' 的字符串大于 'obb_it1'，
  导致新回灌部署到 obb_it1 的权重永远排在 obb_it2 残留权重之后 —— 回灌实际不生效。

修复：
  1) 专用回灌目录 runs/obb/current/weights/（最高优先级，独立于 obb_it1/obb_it2 历史目录）
  2) 历史目录 obb_current.* 改按【修改时间】倒序（取最新）
  3) sync_obb_weight.bat 部署到专用目录，热加载时【显式传 path】（彻底绕开探测歧义）

- 目标文件位于工作目录之外，故用本脚本（UTF-8、字面替换、唯一性校验）安全应用。
- 每处替换要求 old 在文件中恰好命中 1 次，否则中止且不改动文件。
- 完成后自动 py_compile 语法自检。
"""
import sys

TARGET = r"E:\anchor\WorkBuddy\moren\单丝计数V2.0\scripts\auto_label_server.py"
BAT_OUT = r"E:\anchor\WorkBuddy\moren\单丝计数V2.0\scripts\sync_obb_weight.bat"

REPL = []


def add(old, new):
    """登记一对字面替换（old 必须唯一命中）。"""
    assert old not in (r[0] for r in REPL), "old 重复"
    REPL.append((old, new))


# =====================================================================
# 1) _resolve_obb_model_path()：修复字符串排序 Bug → 专用目录 + 按时间倒序
# =====================================================================
add(
'''def _resolve_obb_model_path():
    """按优先级探测 OBB 权重路径，返回最先存在者；全不存在回退默认路径。"""
    import glob as _glob
    cands = []
    env_path = os.environ.get("QDV_OBB_MODEL", "").strip()
    if env_path:
        cands.append(env_path)
    # 遍历 runs/obb/**/weights/ 下约定文件名（最新训练回灌产物，.pt 优先）
    for pat in ("obb_current.pt", "obb_current.onnx"):
        for p in sorted(_glob.glob(os.path.join(_PROJECT_ROOT, "runs", "obb", "**", "weights", pat),
                                   recursive=True), reverse=True):
            cands.append(p)
    cands.append(_OBB_DEFAULT)   # 回退默认
    for p in cands:
        if os.path.isfile(p):
            return p
    return cands[0] if cands else _OBB_DEFAULT''',
'''def _resolve_obb_model_path():
    """按优先级探测 OBB 权重路径，返回最先存在者；全不存在回退默认路径。

    优先级（高→低）：
      1) 环境变量 QDV_OBB_MODEL（手动指定）
      2) 专用回灌目录 runs/obb/current/weights/obb_current.{pt,onnx}
         —— sync_obb_weight.bat 的部署点，独立于 obb_it1/obb_it2 历史目录，
         避免"字符串排序让 obb_it2 永远压过 obb_it1"导致回灌失效。
      3) 其余 runs/obb/**/weights/obb_current.* —— 按【修改时间】倒序（取最新）
      4) 回退默认 runs/obb/obb_it1/weights/best.pt
    """
    import glob as _glob
    cands = []
    env_path = os.environ.get("QDV_OBB_MODEL", "").strip()
    if env_path:
        cands.append(env_path)
    # 2) 专用回灌目录（sync_obb_weight.bat 部署点）——最高优先级
    for pat in ("obb_current.pt", "obb_current.onnx"):
        cands.append(os.path.join(_PROJECT_ROOT, "runs", "obb", "current", "weights", pat))
    # 3) 兼容历史目录：按修改时间倒序（最新优先），而非字符串排序
    for pat in ("obb_current.pt", "obb_current.onnx"):
        hits = _glob.glob(os.path.join(_PROJECT_ROOT, "runs", "obb", "**", "weights", pat),
                          recursive=True)
        hits.sort(key=lambda p: os.path.getmtime(p), reverse=True)
        cands.extend(hits)
    cands.append(_OBB_DEFAULT)   # 回退默认
    for p in cands:
        if os.path.isfile(p):
            return p
    return cands[0] if cands else _OBB_DEFAULT'''
)

# =====================================================================
# sync_obb_weight.bat（全 ASCII；部署到专用回灌目录；热加载显式传 path）
# =====================================================================
BAT_TEXT = '''@echo off
REM =====================================================================
REM  T6 one-click backfill: import QDVAnnotator retrained OBB weights
REM  into the 8765 auto-label service (hot reload).
REM
REM  USAGE:
REM     sync_obb_weight.bat  [QDVAnnotator training output dir]
REM     - no arg  -> prompts you to drag the output folder here
REM     - arg     -> folder containing best<ts>.onnx / best.pt
REM
REM  It copies the newest weight as obb_current.<ext> under the dedicated
REM  runs\\obb\\current\\weights\\ dir, then hot-reloads THAT exact file via
REM  the service API (no auto-detection ambiguity).
REM  If the service is not running, the weight file stays in place and
REM  will be auto-detected on next service start.
REM =====================================================================
chcp 65001 >nul
setlocal enabledelayedexpansion

set "PROJ=%~dp0.."
set "DST_DIR=%PROJ%\\runs\\obb\\current\\weights"
set "SRC_DIR=%~1"

if "%SRC_DIR%"=="" (
  echo Drag the QDVAnnotator training output folder here, then press Enter:
  set /p "SRC_DIR="
)
if defined SRC_DIR set "SRC_DIR=%SRC_DIR:"=%"
if "%SRC_DIR%"=="" (
  echo [ERROR] No source dir provided.
  pause
  exit /b 1
)
if not exist "%SRC_DIR%" (
  echo [ERROR] Source dir not found: "%SRC_DIR%"
  pause
  exit /b 1
)

REM --- 1) pick the newest best*.onnx (fallback best*.pt) ---
set "NEWEST="
set "EXT=onnx"
for /f "delims=" %%f in ('dir /b /o-d "%SRC_DIR%\\best*.onnx" 2^>nul') do (
  if not defined NEWEST set "NEWEST=%%f"
)
if not defined NEWEST (
  set "EXT=pt"
  for /f "delims=" %%f in ('dir /b /o-d "%SRC_DIR%\\best*.pt" 2^>nul') do (
    if not defined NEWEST set "NEWEST=%%f"
  )
)
if not defined NEWEST (
  echo [ERROR] No best*.onnx / best*.pt found in "%SRC_DIR%"
  pause
  exit /b 1
)

REM --- 2) copy to the dedicated backfill dir obb_current.<ext> ---
if not exist "%DST_DIR%" mkdir "%DST_DIR%"
set "DST=%DST_DIR%\\obb_current.%EXT%"
copy /y "%SRC_DIR%\\%NEWEST%" "%DST%" >nul
if errorlevel 1 (
  echo [ERROR] Copy failed to "%DST%"
  pause
  exit /b 1
)
echo [OK] Weight staged: %NEWEST%
echo      -^> %DST%

REM --- 3) hot reload the exact staged file via service API ---
set "TMPF=%TEMP%\\qdv_obb_reload.json"
set "DSTFWD=%DST:\\=/%"
curl -s -X POST http://127.0.0.1:8765/api/load_obb_weight -H "Content-Type: application/json" -d "{\\"path\\":\\"%DSTFWD%\\"}" > "%TMPF%" 2>nul
if errorlevel 1 (
  echo [WARN] Service not running at :8765.
  echo        The weight file is in place and will auto-load on next service start.
  echo        Start the auto-label service launcher, then re-run this script.
) else (
  echo [OK] Hot reload requested. Service response:
  type "%TMPF%"
)
echo.
pause
'''


def _load(path):
    with open(path, "r", encoding="utf-8", newline="") as f:
        return f.read()


def _save(path, text):
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def main():
    raw = _load(TARGET)
    crlf = "\r\n" in raw
    text = raw.replace("\r\n", "\n")

    for i, (old, new) in enumerate(REPL, 1):
        c = text.count(old)
        if c != 1:
            print(f"[FAIL] 替换 #{i} 命中 {c} 次（应为 1）→ 已中止，文件未改动")
            sys.exit(1)
        text = text.replace(old, new)
        print(f"[OK]   替换 #{i} 已应用")

    out = text.replace("\n", "\r\n") if crlf else text
    _save(TARGET, out)

    _save(BAT_OUT, BAT_TEXT)

    import py_compile
    py_compile.compile(TARGET, doraise=True)
    print("[OK] py_compile 语法自检通过:", TARGET)
    print("[OK] bat 已生成:", BAT_OUT)
    print("[OK] OBB 权重探测 Bug 修复完成（专用回灌目录 + 按时间排序 + 显式传 path）")


if __name__ == "__main__":
    main()
