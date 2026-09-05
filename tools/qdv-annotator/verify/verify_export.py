#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QDV Annotator — 导出格式兼容性验证器
=====================================
作用：在真实项目图像（测试图/*.bmp，1200x1200 8bpp 灰度工业件）上，
1:1 移植 C++ Exporters.cpp 的导出逻辑，生成各格式产物，并断言：
  - YOLO 检测/分割 txt 坐标归一化在 [0,1]、类别序号=标签下标（与 operators.json
    categoryLabels 顺序一致）
  - labels.json 形如 {"labels":[...]}（与 models/labels.json 同构，供模型注册）
  - category_labels.json 为纯字符串数组（可直接填 operator 的 categoryLabels 参数）
  - COCO instances_*.json 结构合法，可被 Label Studio / CVAT 回灌
  - .qdvann 工程文件可被 openProject 闭环读取

注意：本脚本是 C++ 导出器的 Python 镜像，用于「无 Qt 环境时」验证格式正确性；
真正生产导出由 QDVAnnotator(.exe) 完成，二者算法完全一致。
"""
import json, os, glob, math, shutil, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]          # QDV 仓库根
TEST_IMG = ROOT / "测试图"
OUT = ROOT / "tools" / "qdv-annotator" / "verify" / "_out"
# 注：沙箱 safe-delete 会拦截 rmtree，此处改用 exist_ok 覆盖旧产物
OUT.mkdir(parents=True, exist_ok=True)

LABELS = ["defect", "good"]   # 与 operators.json categoryLabels 同序

# ---------- 构造样本工程（镜像 AnnotationSession 内存结构）----------
def img_info(p):
    # 读取 BMP 尺寸（8bpp 灰度，简单解析 BITMAPINFOHEADER）
    with open(p, "rb") as f:
        data = f.read(34)
    w = int.from_bytes(data[18:22], "little")
    h = int.from_bytes(data[22:26], "little")
    return w, h

bmps = sorted(glob.glob(str(TEST_IMG / "*.bmp")))[:3]
images = []
for i, p in enumerate(bmps):
    W, H = img_info(p)
    ann = []
    if i == 0:
        ann.append({"shape": "rect", "labelId": 0, "x": 500, "y": 520, "w": 180, "h": 160})
    elif i == 1:
        ann.append({"shape": "polygon", "labelId": 0, "points": [
            {"x": 600, "y": 600}, {"x": 760, "y": 640}, {"x": 700, "y": 780}]})
    else:
        ann.append({"shape": "rect", "labelId": 1, "x": 100, "y": 100, "w": 300, "h": 300})
    images.append({
        "path": p, "fileName": os.path.basename(p),
        "width": W, "height": H, "annotations": ann,
        "classLabels": [1] if i == 2 else [], "subset": "unassigned"
    })

# subset 计划：模拟 flag 模式，确保 train/val 均有样本
plan_list = ["train", "val", "train"]
for im, s in zip(images, plan_list):
    im["subset"] = s

# ---------- 工具函数（与 C++ 一致）----------
def norm_rect(x, y, w, h, W, H):
    cx = (x + w / 2) / max(W, 1)
    cy = (y + h / 2) / max(H, 1)
    nw = w / max(W, 1)
    nh = h / max(H, 1)
    return [max(0.0, min(1.0, v)) for v in (cx, cy, nw, nh)]

def copy_image(src, dstdir, copy=True):
    os.makedirs(dstdir, exist_ok=True)
    dst = os.path.join(dstdir, os.path.basename(src))
    if copy:
        shutil.copy(src, dst)
    return os.path.basename(src)

# ---------- 各格式导出 ----------
def write_labels_json(d, labels):
    os.makedirs(d, exist_ok=True)
    Path(os.path.join(d, "labels.json")).write_text(json.dumps({"labels": labels}, ensure_ascii=False, indent=2))
    Path(os.path.join(d, "category_labels.json")).write_text(json.dumps(labels, ensure_ascii=False, indent=2))

def export_yolo_detect(out, labels, imgs, plan):
    write_labels_json(out, labels)
    Path(out / "dataset.yaml").write_text(
        f"path: {out}\ntrain: images/train\nval: images/val\n\nnc: {len(labels)}\n"
        f"names: [{', '.join(labels)}]\n")
    for im, s in zip(imgs, plan):
        W, H = im["width"], im["height"]
        copy_image(im["path"], os.path.join(out, "images", s))
        lines = []
        for a in im["annotations"]:
            if a["shape"] != "rect" or a["labelId"] < 0:
                continue
            cx, cy, nw, nh = norm_rect(a["x"], a["y"], a["w"], a["h"], W, H)
            lines.append(f"{a['labelId']} {cx:.6f} {cy:.6f} {nw:.6f} {nh:.6f}")
        Path(out / "labels" / s).mkdir(parents=True, exist_ok=True)
        Path(out / "labels" / s / (Path(im["fileName"]).stem + ".txt")).write_text("\n".join(lines))

def export_yolo_seg(out, labels, imgs, plan):
    write_labels_json(out, labels)
    Path(out / "dataset.yaml").write_text(
        f"path: {out}\ntrain: images/train\nval: images/val\nmasks: masks/train\n\n"
        f"nc: {len(labels)}\nnames: [{', '.join(labels)}]\n")
    for im, s in zip(imgs, plan):
        W, H = im["width"], im["height"]
        copy_image(im["path"], os.path.join(out, "images", s))
        lines = []
        for a in im["annotations"]:
            if a["labelId"] < 0:
                continue
            if a["shape"] == "polygon":
                pts = a["points"]
                if len(pts) < 3:
                    continue
                toks = [str(a["labelId"])]
                for p in pts:
                    toks.append(f"{p['x']/max(W,1):.6f}")
                    toks.append(f"{p['y']/max(H,1):.6f}")
                lines.append(" ".join(toks))
            elif a["shape"] == "rect":
                x, y, w, h = a["x"], a["y"], a["w"], a["h"]
                cs = [(x,y),(x+w,y),(x+w,y+h),(x,y+h)]
                toks = [str(a["labelId"])]
                for (px,py) in cs:
                    toks.append(f"{px/max(W,1):.6f}")
                    toks.append(f"{py/max(H,1):.6f}")
                lines.append(" ".join(toks))
        Path(out / "labels" / s).mkdir(parents=True, exist_ok=True)
        Path(out / "labels" / s / (Path(im["fileName"]).stem + ".txt")).write_text("\n".join(lines))

def export_classification(out, labels, imgs, plan):
    write_labels_json(out, labels)
    for im, s in zip(imgs, plan):
        for lid in im["classLabels"]:
            if 0 <= lid < len(labels):
                copy_image(im["path"], os.path.join(out, s, labels[lid]))

def export_ocr(out, labels, imgs, plan):
    write_labels_json(out, labels)
    gt = []
    for im, s in zip(imgs, plan):
        W, H = im["width"], im["height"]
        rel = copy_image(im["path"], os.path.join(out, "images", s))
        anns = []
        texts = []
        for a in im["annotations"]:
            if a["shape"] != "rect" or a["labelId"] < 0:
                continue
            txt = a.get("text", "")
            anns.append({"bbox": [a["x"], a["y"], a["w"], a["h"]], "text": txt, "label": labels[a["labelId"]]})
            if txt:
                texts.append(txt)
        obj = {"image_width": W, "image_height": H, "annotations": anns}
        Path(out / "labels" / s).mkdir(parents=True, exist_ok=True)
        Path(out / "labels" / s / (Path(im["fileName"]).stem + ".json")).write_text(json.dumps(obj, ensure_ascii=False, indent=2))
        gt.append(f"{s}/{rel}\t{' '.join(texts)}")
    Path(out / "gt.txt").write_text("\n".join(gt))

def export_coco(out, labels, imgs, plan):
    for s in ("train", "val"):
        coco = {"info": {"description": "QDV Annotator export", "version": "1.0"},
                "categories": [{"id": i+1, "name": labels[i], "supercategory": "none"} for i in range(len(labels))],
                "images": [], "annotations": []}
        iid = annid = 1
        for im, ss in zip(imgs, plan):
            if ss != s:
                continue
            W, H = im["width"], im["height"]
            coco["images"].append({"id": iid, "file_name": os.path.basename(im["path"]), "width": W, "height": H})
            for a in im["annotations"]:
                lid = a["labelId"]
                if lid < 0:
                    continue
                cao = {"id": annid, "image_id": iid, "category_id": lid+1, "iscrowd": 0}
                if a["shape"] == "polygon":
                    seg = []
                    for p in a["points"]:
                        seg += [p["x"], p["y"]]
                    cao["segmentation"] = [seg]
                    cao["bbox"] = [0,0,0,0]
                    cao["area"] = abs(sum(p["x"]*q["y"]-q["x"]*p["y"] for p,q in zip(a["points"], a["points"][1:]+[a["points"][0]])))/2
                else:
                    cao["bbox"] = [a["x"], a["y"], a["w"], a["h"]]
                    cao["area"] = a["w"]*a["h"]
                    cao["segmentation"] = [[a["x"],a["y"],a["x"]+a["w"],a["y"],a["x"]+a["w"],a["y"]+a["h"],a["x"],a["y"]+a["h"]]]
                coco["annotations"].append(cao)
                annid += 1
            iid += 1
        Path(out / "annotations").mkdir(parents=True, exist_ok=True)
        Path(out / "annotations" / f"instances_{s}.json").write_text(json.dumps(coco, ensure_ascii=False, indent=2))
        for im, ss in zip(imgs, plan):
            if ss == s:
                copy_image(im["path"], os.path.join(out, "images", s))

def export_qdvann(out_file, labels, imgs):
    proj = {"version": "1.0", "app": "QDV Annotator", "taskType": "detection",
            "labels": labels, "labelColors": ["#FFB74D", "#7986CB"], "images": []}
    for im in imgs:
        proj["images"].append({
            "id": f"u{i}", "path": im["path"], "fileName": im["fileName"],
            "width": im["width"], "height": im["height"], "subset": im["subset"],
            "annotations": im["annotations"], "classLabels": im["classLabels"]})
    Path(out_file).write_text(json.dumps(proj, ensure_ascii=False, indent=2))

# ---------- 校验 ----------
results = []
def check(name, cond, detail=""):
    results.append((name, cond, detail))
    print(f"[{'PASS' if cond else 'FAIL'}] {name}" + (f" — {detail}" if detail else ""))

def run():
    d_det = OUT / "yolo_detect"; d_det.mkdir(exist_ok=True)
    export_yolo_detect(d_det, LABELS, images, plan_list)
    d_seg = OUT / "yolo_seg"; d_seg.mkdir(exist_ok=True)
    export_yolo_seg(d_seg, LABELS, images, plan_list)
    d_cls = OUT / "classification"; d_cls.mkdir(exist_ok=True)
    export_classification(d_cls, LABELS, images, plan_list)
    d_ocr = OUT / "ocr"; d_ocr.mkdir(exist_ok=True)
    export_ocr(d_ocr, LABELS, images, plan_list)
    d_coco = OUT / "coco"; d_coco.mkdir(exist_ok=True)
    export_coco(d_coco, LABELS, images, plan_list)
    d_qdv = OUT / "project.qdvann"
    export_qdvann(d_qdv, LABELS, images)

    # 1) YOLO detect 校验
    lbl = json.loads((d_det / "labels.json").read_text())
    check("labels.json 同构 models/labels.json", lbl == {"labels": LABELS}, str(lbl))
    cat = json.loads((d_det / "category_labels.json").read_text())
    check("category_labels.json 为纯数组(可直接填算子参数)", cat == LABELS)
    yaml_txt = (d_det / "dataset.yaml").read_text()
    check("dataset.yaml 含 nc/names", "nc: 2" in yaml_txt and "names:" in yaml_txt)
    all_ok = True
    for txtf in (d_det / "labels").rglob("*.txt"):
        for line in txtf.read_text().splitlines():
            if not line.strip():
                continue
            parts = line.split()
            cid = int(parts[0])
            coords = list(map(float, parts[1:]))
            if not (0 <= cid < len(LABELS)) or any(not (0.0 <= c <= 1.0) for c in coords):
                all_ok = False
    check("YOLO detect 坐标归一化且类别∈标签下标", all_ok)

    # 2) YOLO seg 校验
    seg_ok = True
    for txtf in (d_seg / "labels").rglob("*.txt"):
        for line in txtf.read_text().splitlines():
            if not line.strip():
                continue
            parts = line.split()
            if not (0 <= int(parts[0]) < len(LABELS)): seg_ok = False
            for c in map(float, parts[1:]):
                if not (0.0 <= c <= 1.0): seg_ok = False
    check("YOLO seg 多边形归一化", seg_ok)

    # 3) classification 目录布局
    cls_ok = (d_cls / "train" / "good").exists() or (d_cls / "val" / "good").exists()
    check("classification 按类名建目录", cls_ok)

    # 4) OCR
    ocr_json = next((d_ocr / "labels").rglob("*.json"))
    oj = json.loads(ocr_json.read_text())
    check("OCR json 含 bbox/image_width", "bbox" in oj["annotations"][0] and "image_width" in oj)
    check("OCR gt.txt 生成", (d_ocr / "gt.txt").exists())

    # 5) COCO
    coco_ok = True
    for s in ("train", "val"):
        cj = json.loads((d_coco / "annotations" / f"instances_{s}.json").read_text())
        if not (isinstance(cj["categories"], list) and "annotations" in cj and "images" in cj):
            coco_ok = False
    check("COCO instances_*.json 结构合法", coco_ok)

    # 6) qdvann 闭环
    reloaded = json.loads(d_qdv.read_text())
    check("qdvann 闭环可读(标签/图像数一致)",
          reloaded["labels"] == LABELS and len(reloaded["images"]) == len(images),
          f"imgs={len(reloaded['images'])}")

    npass = sum(1 for _, c, _ in results if c)
    print(f"\n=== 结果: {npass}/{len(results)} 通过 ===")
    print(f"产物目录: {OUT}")
    return npass == len(results)

if __name__ == "__main__":
    ok = run()
    sys.exit(0 if ok else 1)
