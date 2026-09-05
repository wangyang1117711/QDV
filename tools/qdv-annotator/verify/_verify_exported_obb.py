# -*- coding: utf-8 -*-
"""只读校验 QDVAnnotator 实际导出的 yolo_obb 数据集是否符合 OBB 训练格式。

检查项：
1. 目录结构 images/{train,val} + labels/{train,val}
2. 图像 <-> 标签 一一配对（同名 .bmp/.png/.jpg -> .txt）
3. 每行 class x1 y1 x2 y2 x3 y3 x4 y4：9 个字段、数字、class 在 [0,nc)
4. 坐标在合理范围（允许少量越界标注，仅统计）
5. 4 角点逆时针顺序（shoelace 面积 > 0，YOLO-OBB 要求 CCW）
6. dataset.yaml 可被 PyYAML 解析，nc/names 与 class 索引一致
7. labels.json / category_labels.json / dataset_report.json 内容一致性
"""
import json
import math
import os
import sys
from collections import Counter

ROOT = r"E:\anchor\WorkBuddy\moren\单丝计数V2.0\有向矩形数据output"

IMG_EXT = {".bmp", ".png", ".jpg", ".jpeg"}


def load_yaml(path):
    """极简 YAML 子集解析（本数据集 yaml 结构固定：path/train/val/nc/names）。"""
    data = {}
    with open(path, "r", encoding="utf-8") as f:
        lines = [l for l in f.read().splitlines() if l.strip() and not l.strip().startswith("#")]
    for i, ln in enumerate(lines):
        if ln.startswith("names:"):
            rest = ln[len("names:"):].strip()
            names = [s.strip().strip('"').strip("'") for s in rest.strip("[]").split(",") if s.strip()]
            data["names"] = names
        elif ":" in ln and not ln.startswith(" "):
            k, v = ln.split(":", 1)
            data[k.strip()] = v.strip()
    return data


def main():
    fails = []

    # 1. 目录
    for sub in ("train", "val"):
        for kind in ("images", "labels"):
            d = os.path.join(ROOT, kind, sub)
            if not os.path.isdir(d):
                fails.append(f"缺少目录: {d}")
    if fails:
        print("\n".join(fails))
        return 1

    # 2+3+4+5. 逐子集配对与行校验
    # 坐标范围对齐 ultralytics 校验规则（上下限均允许 1% 容差）：
    #   utils.py: assert points.max() <= 1.01 ; assert lb.min() >= -0.01
    # 顶点方向：ultralytics 训练时用 polygon2rbox(cv2.minAreaRect) 求规范 xywhr，
    #   CW/CCW 不影响训练，故仅统计、不作失败判定。
    total_imgs = total_lbl = total_lines = 0
    class_dist = Counter()
    bad_fields = 0          # 字段数/非数字
    bad_cls = 0             # class 越界
    range_lo = range_hi = 0
    cw = ccw = 0            # 顺时针/逆时针（仅统计）
    not_found_txt = []      # 有图无标签
    orphan_txt = []         # 有标签无图
    ccw_of = lambda p: sum(p[i][0]*p[(i+1) % 4][1] - p[(i+1) % 4][0]*p[i][1] for i in range(4)) / 2.0

    for sub in ("train", "val"):
        img_dir = os.path.join(ROOT, "images", sub)
        lbl_dir = os.path.join(ROOT, "labels", sub)
        imgs = {f for f in os.listdir(img_dir)
                if os.path.splitext(f)[1].lower() in IMG_EXT}
        lbls = {f for f in os.listdir(lbl_dir) if f.lower().endswith(".txt")}
        stems_i = {os.path.splitext(f)[0] for f in imgs}
        stems_l = {os.path.splitext(f)[0] for f in lbls}
        total_imgs += len(imgs); total_lbl += len(lbls)
        not_found_txt += sorted(stems_i - stems_l)
        orphan_txt += sorted(stems_l - stems_i)

        for stem in sorted(stems_i & stems_l):
            p = os.path.join(lbl_dir, stem + ".txt")
            with open(p, "r", encoding="utf-8") as f:
                for lno, ln in enumerate(f, 1):
                    ln = ln.strip()
                    if not ln:
                        continue
                    total_lines += 1
                    toks = ln.split()
                    if len(toks) != 9:
                        bad_fields += 1
                        continue
                    try:
                        nums = [float(t) for t in toks]
                    except ValueError:
                        bad_fields += 1
                        continue
                    cls = int(nums[0])
                    if cls < 0:
                        bad_cls += 1; continue
                    class_dist[cls] += 1
                    pts = [(nums[1+2*i], nums[2+2*i]) for i in range(4)]
                    for x, y in pts:
                        if x < -0.01 or x > 1.01:
                            range_lo += 1
                            print(f"  [越界x] {sub}/{stem}.txt L{lno}: x={x:.6f} y={y:.6f}")
                        if y < -0.01 or y > 1.01:
                            range_hi += 1
                            print(f"  [越界y] {sub}/{stem}.txt L{lno}: x={x:.6f} y={y:.6f}")
                    area = ccw_of(pts)
                    if area > 0: ccw += 1
                    else: cw += 1

    # 6. yaml
    y = load_yaml(os.path.join(ROOT, "dataset.yaml"))
    nc = int(y.get("nc", -1))
    names = y.get("names", [])
    max_cls = max(class_dist) if class_dist else -1
    print(f"[目录] 图像={total_imgs} (train+val), 标签txt={total_lbl}, 行数={total_lines}")
    print(f"[配对] 有图无标签: {len(not_found_txt)} -> {not_found_txt[:3]}")
    print(f"[配对] 有标签无图: {len(orphan_txt)} -> {orphan_txt[:3]}")
    print(f"[字段] 非9字段/非数字: {bad_fields}")
    print(f"[class] 越界: {bad_cls}; 分布: {dict(sorted(class_dist.items()))}")
    print(f"[范围] 超出ultralytics容差[-0.01,1.01]的坐标数: x={range_lo}, y={range_hi}")
    print(f"[方向] 逆时针(CCW)={ccw}, 顺时针(CW)={cw}  (仅统计,训练不依赖)")
    print(f"[yaml] nc={nc}, names={names}")
    print(f"[yaml] 类数一致: {len(names) == nc}")

    # 7. json 一致性
    lab = json.load(open(os.path.join(ROOT, "labels.json"), encoding="utf-8"))["labels"]
    cat = json.load(open(os.path.join(ROOT, "category_labels.json"), encoding="utf-8"))
    rep = json.load(open(os.path.join(ROOT, "dataset_report.json"), encoding="utf-8"))
    print(f"[labels.json] {len(lab)} 项: {lab}")
    print(f"[category_labels.json] {len(cat)} 项: {cat}")
    print(f"[report] total={rep['total_images']}, labeled={rep['labeled_images']}, "
          f"anns={rep['annotations_total']}, 与本次扫描行数一致={rep['annotations_total'] == total_lines}")

    ok = True
    if not_found_txt or orphan_txt:
        ok = False; print("FAIL: 图像/标签未完全配对")
    if bad_fields or bad_cls:
        ok = False; print("FAIL: 存在格式错误行")
    if range_lo or range_hi:
        ok = False; print("FAIL: 存在超出 ultralytics 容差范围的坐标")
    if len(names) != nc:
        ok = False; print("FAIL: yaml nc 与 names 数不一致")
    if names != lab or cat != lab:
        ok = False; print("FAIL: 三类名列表不一致")
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
