#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
to_yolo.py —— 把 X-AnyLabeling 的矩形标注转成 YOLO 格式

产物：
  <out>/images/{train,val}/*.jpg|png
  <out>/labels/{train,val}/*.txt     每行: cls cx cy w h（归一化）
  <out>/data.yaml                    训练配置，可直接给 ultralytics/yolov5

用法：
  python3 tools/to_yolo.py data --out datasets/energy_unit --val-ratio 0.2
"""

from __future__ import annotations

import argparse
import json
import random
import shutil
import sys
from pathlib import Path

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp"}


def rect_points(shape):
    pts = shape.get("points") or []
    if len(pts) < 2:
        return None
    xs = [float(p[0]) for p in pts]
    ys = [float(p[1]) for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def main() -> int:
    ap = argparse.ArgumentParser(description="X-AnyLabeling -> YOLO")
    ap.add_argument("root")
    ap.add_argument("--out", default="datasets/energy_unit")
    ap.add_argument("--class-name", default="energy_unit")
    ap.add_argument("--val-ratio", type=float, default=0.2)
    ap.add_argument("--seed", type=int, default=2026)
    args = ap.parse_args()

    root = Path(args.root)
    if not root.is_dir():
        print(f"[错误] 不是目录: {root}", file=sys.stderr)
        return 1

    items = []
    for img in sorted(root.rglob("*")):
        if not img.is_file() or img.suffix.lower() not in IMAGE_EXTS:
            continue
        js = img.with_suffix(".json")
        if not js.exists():
            continue
        try:
            data = json.loads(js.read_text(encoding="utf-8"))
        except Exception:  # noqa: BLE001
            continue
        w = float(data.get("imageWidth") or 0)
        h = float(data.get("imageHeight") or 0)
        if w <= 0 or h <= 0:
            continue
        lines = []
        for sh in data.get("shapes") or []:
            if sh.get("label") != args.class_name:
                continue
            box = rect_points(sh)
            if box is None:
                continue
            x1, y1, x2, y2 = box
            x1, y1 = max(0.0, x1), max(0.0, y1)
            x2, y2 = min(w, x2), min(h, y2)
            bw, bh = x2 - x1, y2 - y1
            if bw <= 1 or bh <= 1:
                continue
            lines.append(f"0 {(x1 + bw / 2) / w:.6f} {(y1 + bh / 2) / h:.6f} "
                         f"{bw / w:.6f} {bh / h:.6f}")
        items.append((img, lines))

    if not items:
        print("[错误] 没有找到带标注的图片", file=sys.stderr)
        return 1

    rng = random.Random(args.seed)
    rng.shuffle(items)
    val_count = max(1, int(round(len(items) * args.val_ratio))) if args.val_ratio > 0 else 0
    splits = {"val": items[:val_count], "train": items[val_count:]}

    out = Path(args.out)
    for sub in ("images", "labels"):
        for split in splits:
            (out / sub / split).mkdir(parents=True, exist_ok=True)

    total_boxes = 0
    for split, entries in splits.items():
        for img, lines in entries:
            shutil.copy2(img, out / "images" / split / img.name)
            label_path = out / "labels" / split / (img.stem + ".txt")
            label_path.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")
            total_boxes += len(lines)

    (out / "data.yaml").write_text(
        "path: {p}\ntrain: images/train\nval: images/val\nnc: 1\nnames: ['{n}']\n".format(
            p=out.resolve().as_posix(), n=args.class_name),
        encoding="utf-8")

    print(f"[完成] {len(items)} 张图 / {total_boxes} 个框")
    print(f"  train: {len(splits['train'])} 张   val: {len(splits['val'])} 张")
    print(f"  输出: {out}/data.yaml")
    return 0


if __name__ == "__main__":
    sys.exit(main())
