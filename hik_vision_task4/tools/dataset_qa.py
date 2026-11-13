#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
dataset_qa.py —— 数据集标注质检

检查内容：
  * 每张图是否有对应 json；每个 json 是否有对应图片
  * JSON 能否解析、必需字段是否齐全
  * 类别名是否都是 energy_unit（避免手误写成 energy unit / EnergyUnit）
  * 框的坐标是否越界、是否退化成零面积
  * 框是否贴边（目标被裁切）、是否过小、是否严重重叠
统计每个场景的图片数、标注数、框数，最后给一个可复制的汇总表。

用法：
  python3 tools/dataset_qa.py data
  python3 tools/dataset_qa.py data --class-name energy_unit --report output/qa.md
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp"}


def is_image(p: Path) -> bool:
    return p.suffix.lower() in IMAGE_EXTS


def box_of(shape):
    pts = shape.get("points") or []
    if len(pts) < 2:
        return None
    xs = [float(p[0]) for p in pts]
    ys = [float(p[1]) for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def iou(a, b):
    ax1, ay1, ax2, ay2 = a
    bx1, by1, bx2, by2 = b
    ix1, iy1 = max(ax1, bx1), max(ay1, by1)
    ix2, iy2 = min(ax2, bx2), min(ay2, by2)
    if ix2 <= ix1 or iy2 <= iy1:
        return 0.0
    inter = (ix2 - ix1) * (iy2 - iy1)
    union = (ax2 - ax1) * (ay2 - ay1) + (bx2 - bx1) * (by2 - by1) - inter
    return inter / union if union > 0 else 0.0


def main() -> int:
    ap = argparse.ArgumentParser(description="标注质检")
    ap.add_argument("root", help="数据根目录（下面可以有多个场景文件夹）")
    ap.add_argument("--class-name", default="energy_unit")
    ap.add_argument("--min-box", type=float, default=16.0, help="框短边小于该值算过小（像素）")
    ap.add_argument("--edge-margin", type=float, default=1.0, help="贴边判定阈值（像素）")
    ap.add_argument("--overlap", type=float, default=0.85, help="框重叠 IoU 超过该值告警")
    ap.add_argument("--report", default="", help="把汇总写成 markdown")
    args = ap.parse_args()

    root = Path(args.root)
    if not root.is_dir():
        print(f"[错误] 不是目录: {root}", file=sys.stderr)
        return 1

    scenes = sorted({p.parent for p in root.rglob("*") if p.is_file() and is_image(p)})
    if not scenes:
        print(f"[错误] {root} 下没有找到图片", file=sys.stderr)
        return 1

    issues: list[str] = []
    rows = []
    grand_images = grand_labeled = grand_boxes = 0

    for scene in scenes:
        images = sorted(p for p in scene.iterdir() if p.is_file() and is_image(p))
        jsons = {p.stem: p for p in scene.iterdir() if p.is_file() and p.suffix.lower() == ".json"}

        labeled = 0
        boxes_total = 0
        sizes: list[float] = []
        problems = 0

        for img in images:
            key = img.stem
            js = jsons.get(key)
            if js is None:
                issues.append(f"{scene.name}/{img.name}: 没有对应 json")
                problems += 1
                continue
            try:
                data = json.loads(js.read_text(encoding="utf-8"))
            except Exception as exc:  # noqa: BLE001
                issues.append(f"{scene.name}/{js.name}: JSON 解析失败: {exc}")
                problems += 1
                continue

            w = int(data.get("imageWidth") or 0)
            h = int(data.get("imageHeight") or 0)
            shapes = data.get("shapes") or []
            labeled += 1
            boxes_total += len(shapes)

            if not w or not h:
                issues.append(f"{scene.name}/{js.name}: 缺少 imageWidth/imageHeight")
                problems += 1
                continue
            if not shapes:
                issues.append(f"{scene.name}/{js.name}: 没有任何标注框")
                problems += 1

            boxes = []
            for i, sh in enumerate(shapes):
                label = sh.get("label", "")
                if label != args.class_name:
                    issues.append(f"{scene.name}/{js.name}#{i}: 类别是 '{label}'，应为 '{args.class_name}'")
                    problems += 1
                box = box_of(sh)
                if box is None:
                    issues.append(f"{scene.name}/{js.name}#{i}: points 少于 2 个")
                    problems += 1
                    continue
                x1, y1, x2, y2 = box
                bw, bh = x2 - x1, y2 - y1
                if bw <= 1 or bh <= 1:
                    issues.append(f"{scene.name}/{js.name}#{i}: 框退化（{bw:.0f}x{bh:.0f}）")
                    problems += 1
                    continue
                if x1 < 0 or y1 < 0 or x2 > w or y2 > h:
                    issues.append(f"{scene.name}/{js.name}#{i}: 框越界 ({x1:.0f},{y1:.0f})-({x2:.0f},{y2:.0f}) / {w}x{h}")
                    problems += 1
                if min(bw, bh) < args.min_box:
                    issues.append(f"{scene.name}/{js.name}#{i}: 框过小 {bw:.0f}x{bh:.0f}")
                    problems += 1
                if x1 <= args.edge_margin or y1 <= args.edge_margin or \
                   x2 >= w - args.edge_margin or y2 >= h - args.edge_margin:
                    issues.append(f"{scene.name}/{js.name}#{i}: 框贴边，目标可能被裁切")
                    problems += 1
                sizes.append(min(bw, bh))
                boxes.append((x1, y1, x2, y2))

            for i in range(len(boxes)):
                for j in range(i + 1, len(boxes)):
                    if iou(boxes[i], boxes[j]) > args.overlap:
                        issues.append(f"{scene.name}/{js.name}: 第 {i}/{j} 个框几乎重合")
                        problems += 1

        for stem, js in jsons.items():
            if not any(img.stem == stem for img in images):
                issues.append(f"{scene.name}/{js.name}: 找不到对应图片")
                problems += 1

        rows.append({
            "scene": scene.name,
            "images": len(images),
            "labeled": labeled,
            "boxes": boxes_total,
            "avg": (boxes_total / labeled) if labeled else 0.0,
            "min_side": min(sizes) if sizes else 0.0,
            "problems": problems,
        })
        grand_images += len(images)
        grand_labeled += labeled
        grand_boxes += boxes_total

    lines = []
    lines.append("# 数据集标注质检\n")
    lines.append("| 场景 | 图片数 | 已标注 | 框总数 | 框/图 | 最小短边px | 问题数 |")
    lines.append("| --- | --- | --- | --- | --- | --- | --- |")
    for r in rows:
        lines.append(f"| {r['scene']} | {r['images']} | {r['labeled']} | {r['boxes']} | "
                     f"{r['avg']:.2f} | {r['min_side']:.0f} | {r['problems']} |")
    lines.append(f"| **合计** | {grand_images} | {grand_labeled} | {grand_boxes} | "
                 f"{grand_boxes / grand_labeled if grand_labeled else 0:.2f} | - | {len(issues)} |")

    text = "\n".join(lines)
    print(text)
    print()
    if issues:
        print(f"发现 {len(issues)} 个问题（最多显示 60 条）:")
        for item in issues[:60]:
            print("  -", item)
        if len(issues) > 60:
            print(f"  ... 其余 {len(issues) - 60} 条省略")
    else:
        print("[通过] 没有发现问题")

    if args.report:
        out = Path(args.report)
        out.parent.mkdir(parents=True, exist_ok=True)
        details = text + "\n\n## 问题清单\n\n" + ("\n".join(f"- {i}" for i in issues) if issues else "无")
        out.write_text(details, encoding="utf-8")
        print(f"\n[report] {out}")

    return 0 if not issues else 2


if __name__ == "__main__":
    sys.exit(main())
