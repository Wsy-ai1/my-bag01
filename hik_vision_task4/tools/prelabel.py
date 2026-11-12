#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
prelabel.py —— 用 OpenCV 先粗标一遍，生成 X-AnyLabeling 可直接打开的 JSON

思路：把"明显是目标"的连通域框出来，人工在 X-AnyLabeling 里只做增删/微调，
比从头画框快很多；粗标错了也没关系，反正要人工过一遍。

用法：
  python3 tools/prelabel.py --images data/sxh_s01 --mode otsu
  python3 tools/prelabel.py --images data/sxh_s01 --mode hsv --debug-dir output/prelabel
  # 然后在 X-AnyLabeling 里打开 data/sxh_s01 继续改

模式：
  otsu      深色物体 + 浅色背景（最常用）
  edge      Canny 轮廓（目标与背景颜色接近时）
  adaptive  自适应阈值（光照不均）
  hsv       彩色目标（按饱和度分割）
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import cv2
import numpy as np

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp"}


def build_mask(bgr: np.ndarray, mode: str) -> np.ndarray:
    gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)

    if mode == "otsu":
        _, mask = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY_INV | cv2.THRESH_OTSU)
    elif mode == "adaptive":
        mask = cv2.adaptiveThreshold(gray, 255, cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
                                     cv2.THRESH_BINARY_INV, 31, 5)
    elif mode == "edge":
        mask = cv2.Canny(gray, 50, 150)
    elif mode == "hsv":
        hsv = cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV)
        mask = cv2.inRange(hsv, np.array([0, 60, 40]), np.array([180, 255, 255]))
    else:
        raise ValueError(f"未知 mode: {mode}")

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


def detect(bgr: np.ndarray, args) -> list[tuple[int, int, int, int]]:
    h, w = bgr.shape[:2]
    mask = build_mask(bgr, args.mode)
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
    return nms(boxes, args.nms)


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
    parser = argparse.ArgumentParser(description="OpenCV 粗标 -> X-AnyLabeling JSON")
    parser.add_argument("--images", required=True, help="图片目录")
    parser.add_argument("--mode", default="otsu", choices=["otsu", "edge", "adaptive", "hsv"])
    parser.add_argument("--class-name", default="energy_unit", help="类别名，必须和标注规范一致")
    parser.add_argument("--min-area", type=float, default=0.001, help="最小面积占比")
    parser.add_argument("--max-area", type=float, default=0.90, help="最大面积占比")
    parser.add_argument("--min-aspect", type=float, default=0.2)
    parser.add_argument("--max-aspect", type=float, default=5.0)
    parser.add_argument("--pad", type=int, default=2, help="框外扩像素")
    parser.add_argument("--nms", type=float, default=0.5, help="重叠框合并阈值")
    parser.add_argument("--overwrite", action="store_true", help="覆盖已有 json")
    parser.add_argument("--debug-dir", default="", help="把画了框的图另存一份")
    args = parser.parse_args()

    src = Path(args.images)
    if not src.is_dir():
        print(f"[错误] 不是目录: {src}", file=sys.stderr)
        return 1

    images = sorted(p for p in src.iterdir() if p.suffix.lower() in IMAGE_EXTS)
    if not images:
        print(f"[错误] {src} 里没有图片", file=sys.stderr)
        return 1

    debug_dir = Path(args.debug_dir) if args.debug_dir else None
    if debug_dir:
        debug_dir.mkdir(parents=True, exist_ok=True)

    done = skipped = total_boxes = 0
    for img_path in images:
        json_path = img_path.with_suffix(".json")
        if json_path.exists() and not args.overwrite:
            skipped += 1
            continue

        bgr = cv2.imread(str(img_path), cv2.IMREAD_COLOR)
        if bgr is None:
            print(f"[skip] 读不了 {img_path}", file=sys.stderr)
            continue

        boxes = detect(bgr, args)
        data = make_json(img_path, (bgr.shape[1], bgr.shape[0]), boxes, args.class_name)
        json_path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")

        if debug_dir:
            vis = bgr.copy()
            for (x, y, w, h) in boxes:
                cv2.rectangle(vis, (x, y), (x + w, y + h), (0, 255, 0), 2)
            cv2.imwrite(str(debug_dir / img_path.name), vis)

        done += 1
        total_boxes += len(boxes)

    print(f"[完成] 新生成 {done} 个 json，跳过 {skipped} 个，共 {total_boxes} 个框")
    print("[下一步] 打开 X-AnyLabeling，File -> Open Dir 选这个目录，逐个检查/修正框")
    return 0


if __name__ == "__main__":
    sys.exit(main())
