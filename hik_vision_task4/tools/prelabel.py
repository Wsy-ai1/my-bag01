#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
prelabel.py —— 用 OpenCV 先粗标一遍，生成 X-AnyLabeling 可直接打开的 JSON

重要：本脚本**不会在图片上画框**，而是给每张图生成同名 .json（框写在 json 里）。
想看框 → 用 X-AnyLabeling 打开这个目录；想看中间结果 → 加 --debug-dir。

用法：
  python3 tools/prelabel.py --images data/wsy_s01 --mode otsu
  python3 tools/prelabel.py --images data/wsy_s01 --mode otsu --invert      # 目标比背景亮
  python3 tools/prelabel.py --images data/wsy_s01 --mode hsv --debug-dir output/prelabel
  python3 tools/prelabel.py --images data/wsy_s01 --overwrite               # 覆盖已有 json

模式：
  otsu      深色目标 + 浅色背景（默认）；目标比背景亮就加 --invert
  adaptive  自适应阈值（光照不均、有阴影）
  edge      Canny 轮廓（目标和背景颜色接近，但有边）
  hsv       彩色目标（按饱和度分割，白色/灰色目标不适合）
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import cv2
import numpy as np

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp"}

HINTS = """\
检测到 0 个框，按顺序排查：
  1) 目标比背景亮？            加 --invert
  2) 目标是彩色的？            --mode hsv
  3) 目标和背景颜色接近？      --mode edge
  4) 目标很小/拍得远？         --min-area 0.0001 --min-aspect 0.1 --max-aspect 10
  5) 先看中间二值图对不对？    --debug-dir output/prelabel，然后看 *_mask.png
  6) 只想先调参？把 --images 指向一个只放 2~3 张的小目录，跑一次几秒钟
"""


def build_mask(bgr: np.ndarray, mode: str, invert: bool, block: int = 31,
               c: int = 5) -> np.ndarray:
    gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)

    if mode == "otsu":
        _, mask = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY_INV | cv2.THRESH_OTSU)
    elif mode == "adaptive":
        mask = cv2.adaptiveThreshold(gray, 255, cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
                                     cv2.THRESH_BINARY_INV, block, c)
    elif mode == "edge":
        mask = cv2.Canny(gray, 50, 150)
    elif mode == "hsv":
        hsv = cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV)
        mask = cv2.inRange(hsv, np.array([0, 60, 40]), np.array([180, 255, 255]))
    else:
        raise ValueError(f"未知 mode: {mode}")

    if invert:
        mask = cv2.bitwise_not(mask)

    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel, iterations=2)
    mask = cv2.dilate(mask, kernel, iterations=1)
    return mask


def iou(a, b) -> float:
    ax1, ay1, aw, ah = a
    bx1, by1, bw, bh = b
    ix1, iy1 = max(ax1, bx1), max(ay1, by1)
    ix2, iy2 = min(ax1 + aw, bx1 + bw), min(ay1 + ah, by1 + bh)
    if ix2 <= ix1 or iy2 <= iy1:
        return 0.0
    inter = (ix2 - ix1) * (iy2 - iy1)
    union = aw * ah + bw * bh - inter
    return inter / union if union > 0 else 0.0


def nms(boxes, thr=0.5):
    boxes = sorted(boxes, key=lambda b: b[2] * b[3], reverse=True)
    kept = []
    for box in boxes:
        if all(iou(box, k) < thr for k in kept):
            kept.append(box)
    return kept


def detect(bgr: np.ndarray, args):
    h, w = bgr.shape[:2]
    mask = build_mask(bgr, args.mode, args.invert, args.block, args.c)
    coverage = float(np.count_nonzero(mask)) / float(h * w)

    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    area_img = float(h * w)
    boxes = []
    for cnt in contours:
        x, y, bw, bh = cv2.boundingRect(cnt)
        area_ratio = (bw * bh) / area_img
        if area_ratio < args.min_area or area_ratio > args.max_area:
            continue
        aspect = bw / max(bh, 1)
        if not (args.min_aspect <= aspect <= args.max_aspect):
            continue
        pad = args.pad
        x1 = max(0, x - pad)
        y1 = max(0, y - pad)
        x2 = min(w - 1, x + bw + pad)
        y2 = min(h - 1, y + bh + pad)
        boxes.append((x1, y1, x2 - x1, y2 - y1))

    return nms(boxes, args.nms), mask, coverage


def make_json(image_path: Path, size, boxes, label: str) -> dict:
    shapes = []
    for (x, y, w, h) in boxes:
        shapes.append({
            "label": label,
            "score": None,
            "points": [[float(x), float(y)], [float(x + w), float(y + h)]],
            "group_id": None,
            "description": "",
            "difficult": False,
            "shape_type": "rectangle",
            "flags": {},
            "attributes": {},
        })
    return {
        "version": "2.4.0",
        "flags": {},
        "shapes": shapes,
        "imagePath": image_path.name,
        "imageData": None,
        "imageHeight": int(size[1]),
        "imageWidth": int(size[0]),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="OpenCV 粗标 -> X-AnyLabeling JSON（不改图片）")
    parser.add_argument("--images", required=True, help="图片目录")
    parser.add_argument("--mode", default="otsu", choices=["otsu", "edge", "adaptive", "hsv"])
    parser.add_argument("--invert", action="store_true", help="反相（目标比背景亮时用）")
    parser.add_argument("--class-name", default="energy_unit", help="类别名，必须和标注规范一致")
    parser.add_argument("--min-area", type=float, default=0.0005, help="最小面积占比，默认 0.05%%")
    parser.add_argument("--max-area", type=float, default=0.90, help="最大面积占比")
    parser.add_argument("--min-aspect", type=float, default=0.15)
    parser.add_argument("--max-aspect", type=float, default=6.0)
    parser.add_argument("--pad", type=int, default=2, help="框外扩像素")
    parser.add_argument("--nms", type=float, default=0.5, help="重叠框合并阈值")
    parser.add_argument("--block", type=int, default=31, help="adaptive 模式的邻域大小（奇数）")
    parser.add_argument("--c", type=int, default=5, help="adaptive 模式的 C 值")
    parser.add_argument("--overwrite", action="store_true", help="覆盖已有 json")
    parser.add_argument("--debug-dir", default="", help="存 *_preview.png（画框）和 *_mask.png（二值图）")
    parser.add_argument("--quiet", action="store_true", help="只打印汇总")
    args = parser.parse_args()

    src = Path(args.images)
    if not src.is_dir():
        print(f"[错误] 不是目录: {src}", file=sys.stderr)
        return 1

    images = sorted(p for p in src.iterdir() if p.suffix.lower() in IMAGE_EXTS)
    if not images:
        print(f"[错误] {src} 里没有图片（支持的扩展名: {', '.join(sorted(IMAGE_EXTS))}）",
              file=sys.stderr)
        return 1

    debug_dir = Path(args.debug_dir) if args.debug_dir else None
    if debug_dir:
        debug_dir.mkdir(parents=True, exist_ok=True)

    done = skipped = total_boxes = 0
    empty_images = []
    high_coverage = []

    for img_path in images:
        json_path = img_path.with_suffix(".json")
        if json_path.exists() and not args.overwrite:
            skipped += 1
            if not args.quiet:
                try:
                    old = json.loads(json_path.read_text(encoding="utf-8"))
                    print(f"[skip] {img_path.name}: 已有 json（{len(old.get('shapes') or [])} 个框），"
                          f"要重标加 --overwrite")
                except Exception:  # noqa: BLE001
                    print(f"[skip] {img_path.name}: 已有 json，加 --overwrite 可覆盖")
            continue

        bgr = cv2.imread(str(img_path), cv2.IMREAD_COLOR)
        if bgr is None:
            print(f"[skip] 读不了 {img_path}", file=sys.stderr)
            continue

        boxes, mask, coverage = detect(bgr, args)
        data = make_json(img_path, (bgr.shape[1], bgr.shape[0]), boxes, args.class_name)
        json_path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")

        if debug_dir:
            vis = bgr.copy()
            for (x, y, w, h) in boxes:
                cv2.rectangle(vis, (x, y), (x + w, y + h), (0, 255, 0), 2)
            cv2.imwrite(str(debug_dir / f"{img_path.stem}_preview.png"), vis)
            cv2.imwrite(str(debug_dir / f"{img_path.stem}_mask.png"), mask)

        done += 1
        total_boxes += len(boxes)
        if not boxes:
            empty_images.append(img_path.name)
        if coverage > 0.60:
            high_coverage.append((img_path.name, coverage))
        if not args.quiet:
            print(f"[ok] {img_path.name}: {len(boxes)} 个框  (mask {coverage * 100:.1f}%)")

    print(f"\n[完成] 新生成 {done} 个 json，跳过 {skipped} 个，共 {total_boxes} 个框")
    if high_coverage:
        name, cov = high_coverage[0]
        print(f"[提示] 有 {len(high_coverage)} 张图的 mask 覆盖率 > 60%（如 {name} {cov * 100:.0f}%），"
              f"可能把背景当成了目标 —— 试试 --invert 或换 --mode")
    if total_boxes == 0:
        print("\n" + HINTS)
    elif empty_images:
        print(f"[提示] {len(empty_images)} 张图没框出任何目标（如 {empty_images[0]}），"
              f"这些需要在 X-AnyLabeling 里手动画，或调参重跑这几张")
    print("[下一步] 打开 X-AnyLabeling → File → Open Dir 选这个目录，逐个检查/修正框")
    return 0


if __name__ == "__main__":
    sys.exit(main())
