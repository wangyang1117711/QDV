# -*- coding: utf-8 -*-
"""
T6 落地补丁：单丝计数 auto_label_server.py（回灌热切换）+ 生成 sync_obb_weight.bat
================================================================================
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
# 1) OBB 常量区 -> 自动探测 + 自检计数
# =====================================================================
add(
'''# ---- 有向矩形(OBB) 自动标注：与现有端点自动标注并存，互不干扰 ----
# 训练产物由 训练有向矩形模型.bat 生成；若未训练，接口会返回明确错误。
OBB_MODEL_PATH = os.path.join(_PROJECT_ROOT, "runs", "obb", "obb_it1", "weights", "best.pt")
MODEL_OBB = None
MODEL_OBB_LOCK = threading.Lock()''',
'''# ---- 有向矩形(OBB) 自动标注：与现有端点自动标注并存，互不干扰 ----
# 权重来源优先级（T6 回灌闭环，详见 QDVAnnotator_闭环整合设计方案_2026-08-19.md）：
#   1) 环境变量 QDV_OBB_MODEL（手动指定）
#   2) runs/obb/**/weights/obb_current.pt（sync_obb_weight.bat 复制的最新重训权重，优先 .pt）
#   3) runs/obb/**/weights/obb_current.onnx（同上，.onnx 形态）
#   4) 回退默认 runs/obb/obb_it1/weights/best.pt（首轮训练产物）
# 每次 /api/load_obb_weight 与启动时都会重新探测；因此 QDVAnnotator 重训产物
# 只需复制为 obb_current.* 即可热切换，无需改任何代码。
_OBB_DEFAULT = os.path.join(_PROJECT_ROOT, "runs", "obb", "obb_it1", "weights", "best.pt")
OBB_SELF_TEST_DETECTIONS = -1   # -1=尚未自检；0=自检 0 检出(告警)；>0=正常


def _has_onnxruntime():
    """探测 onnxruntime 是否可用（.onnx 权重推理依赖）。"""
    try:
        import onnxruntime  # noqa: F401
        return True
    except Exception:
        return False


def _resolve_obb_model_path():
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
    return cands[0] if cands else _OBB_DEFAULT


OBB_MODEL_PATH = _resolve_obb_model_path()   # 启动时探测一次；运行中由 load_obb_weight 刷新
MODEL_OBB = None
MODEL_OBB_LOCK = threading.Lock()'''
)

# =====================================================================
# 2) load_obb_model -> 支持 onnx + 样例自检告警
# =====================================================================
add(
'''def load_obb_model(path):
    """懒加载有向矩形(OBB)模型（首次调用时）。"""
    global MODEL_OBB
    from ultralytics import YOLO
    if not os.path.exists(path):
        raise FileNotFoundError(
            "OBB 模型权重不存在: %s\\n请先运行「训练有向矩形模型.bat」生成 runs/obb/obb_it1/weights/best.pt" % path)
    MODEL_OBB = YOLO(path)
    MODEL_OBB.predict(np.zeros((320, 320, 3), dtype=np.uint8), conf=0.1, imgsz=320, verbose=False)
    return MODEL_OBB''',
'''def load_obb_model(path):
    """懒加载有向矩形(OBB)模型（首次调用或 load_obb_weight 强制重载）。

    支持 .pt 与 .onnx 两种形态；加载后用 1 张空白样例自检并统计检出数，
    0 检出会打印醒目告警（防"空标签训练废权重"上线）。
    """
    global MODEL_OBB, OBB_SELF_TEST_DETECTIONS
    if not os.path.exists(path):
        raise FileNotFoundError(
            "OBB 模型权重不存在: %s\\n"
            "请先运行「训练有向矩形模型.bat」产出 best.pt，"
            "或运行 sync_obb_weight.bat 导入 QDVAnnotator 重训产物。" % path)
    if path.lower().endswith(".onnx") and not _has_onnxruntime():
        raise RuntimeError(
            "检测到 .onnx 权重但缺少 onnxruntime。\\n"
            "请安装 onnxruntime（pip install onnxruntime-gpu），再重新运行 sync_obb_weight.bat。")
    from ultralytics import YOLO
    with MODEL_OBB_LOCK:
        MODEL_OBB = YOLO(path)
        # 自检：1 张空白样例预测，统计检出数（0 检出 = 大概率无效权重）
        res = MODEL_OBB.predict(np.zeros((320, 320, 3), dtype=np.uint8),
                                conf=0.1, imgsz=320, verbose=False)[0]
    obb = getattr(res, "obb", None)
    n_det = 0 if obb is None else int(len(getattr(obb, "xywhr", []) or []))
    OBB_SELF_TEST_DETECTIONS = n_det
    if n_det == 0:
        print(f"[auto_label_server] [WARN] OBB 权重自检 0 检出: {path}\\n"
              f"            该权重可能为无效(空标签训练)产物，请核查数据集后重训。", flush=True)
    else:
        print(f"[auto_label_server] OBB 权重已加载: {path}（样例自检检出 {n_det} 个）", flush=True)
    return MODEL_OBB'''
)

# =====================================================================
# 3) health 增强：暴露权重是否存在 + 自检结果
# =====================================================================
add(
'''            ok = MODEL is not None
            obb_ok = MODEL_OBB is not None
            data = json.dumps({
                "ok": ok,
                "model_loaded": ok,            # 端点模型
                "obb_loaded": obb_ok,          # 有向矩形模型
                "obb_model_path": OBB_MODEL_PATH,
            }).encode("utf-8")''',
'''            ok = MODEL is not None
            obb_ok = MODEL_OBB is not None
            data = json.dumps({
                "ok": ok,
                "model_loaded": ok,            # 端点模型
                "obb_loaded": obb_ok,          # 有向矩形模型
                "obb_model_path": OBB_MODEL_PATH,
                "obb_weights_found": os.path.isfile(OBB_MODEL_PATH),
                "obb_self_test_detections": OBB_SELF_TEST_DETECTIONS,
            }).encode("utf-8")'''
)

# =====================================================================
# 4) do_POST：白名单加入 /api/load_obb_weight 并分发
# =====================================================================
add(
'''        if path not in ("/api/auto_label", "/api/auto_label_rect2",
                        "/api/pseudo_label", "/api/merge_labels"):
            self.send_response(404)
            self.end_headers()
            return
        try:
            length = int(self.headers.get("Content-Length", 0))
            raw = self.rfile.read(length) if length else b"{}"
            body = json.loads(raw.decode("utf-8"))

            if path == "/api/pseudo_label":
                self._handle_pseudo_label(body)
                return
            if path == "/api/merge_labels":
                self._handle_merge_labels(body)
                return''',
'''        if path not in ("/api/auto_label", "/api/auto_label_rect2",
                        "/api/pseudo_label", "/api/merge_labels",
                        "/api/load_obb_weight"):
            self.send_response(404)
            self.end_headers()
            return
        try:
            length = int(self.headers.get("Content-Length", 0))
            raw = self.rfile.read(length) if length else b"{}"
            body = json.loads(raw.decode("utf-8"))

            if path == "/api/load_obb_weight":
                self._handle_load_obb_weight(body)
                return
            if path == "/api/pseudo_label":
                self._handle_pseudo_label(body)
                return
            if path == "/api/merge_labels":
                self._handle_merge_labels(body)
                return'''
)

# =====================================================================
# 5) 新增 _handle_load_obb_weight 方法（插在 _handle_merge_labels 之前）
# =====================================================================
add(
'''    def _handle_merge_labels(self, body):''',
'''    def _handle_load_obb_weight(self, body):
        """T6 回灌热切换：加载 QDVAnnotator 重训产物（或指定路径的 OBB 权重）。

        body: 可选 {"path": "绝对路径"} —— 不传则按优先级自动探测 obb_current.*；
        resp: {"ok", "path", "self_test_detections", "error"?}
        说明：由 sync_obb_weight.bat 调用；也可供标注页「导入 QDV 重训权重」按钮使用。
        """
        global OBB_MODEL_PATH
        cand = (body.get("path") or "").strip()
        if cand and not os.path.isfile(cand):
            raise ValueError("指定的权重文件不存在: %s" % cand)
        if not cand:
            cand = _resolve_obb_model_path()   # env / obb_current.* / 默认
        model = load_obb_model(cand)           # 内部持锁原子替换 + 样例自检
        OBB_MODEL_PATH = cand                  # 刷新 health 展示的路径
        out = {"ok": model is not None, "path": OBB_MODEL_PATH,
               "self_test_detections": OBB_SELF_TEST_DETECTIONS}
        data = json.dumps(out, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self._cors()
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _handle_merge_labels(self, body):'''
)

# =====================================================================
# sync_obb_weight.bat（全 ASCII，规避 GBK 乱码；%~dp0 推导项目根，规避中文硬编码）
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
REM  It copies the newest weight as obb_current.<ext> under
REM  runs\\obb\\obb_it1\\weights\\, then asks the running service to reload.
REM  If the service is not running, the weight file stays in place and
REM  will be auto-detected on next service start.
REM =====================================================================
chcp 65001 >nul
setlocal enabledelayedexpansion

set "PROJ=%~dp0.."
set "DST_DIR=%PROJ%\\runs\\obb\\obb_it1\\weights"
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

REM --- 2) copy to the agreed location obb_current.<ext> ---
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

REM --- 3) hot reload via service API (fallback: prompt restart) ---
set "TMPF=%TEMP%\\qdv_obb_reload.json"
curl -s -X POST http://127.0.0.1:8765/api/load_obb_weight -H "Content-Type: application/json" -d "{}" > "%TMPF%" 2>nul
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
    print("[OK] T6 落地完成（服务端探测+热切换+自检告警 / 一键回灌脚本）")


if __name__ == "__main__":
    main()
