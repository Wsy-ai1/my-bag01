#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
draw_boxes.py —— 把 json 里的框"画"到图上，生成可以发给别人看的预览图

用途：数据集提交要求的是"原图 + 同名 json"，框本身不画在图上；
      但给学姐/老师看进度、写报告、做截图时，需要"有框的图"。
      这个脚本把框烧进副本，输出到独立目录，绝不动原始数据集。

用法：
  python3 tools/draw_boxes.py --images data/wsy_s01
  python3 tools/draw_boxes.py --images data/wsy_s01 --out output/preview --sheet 12
  python3 tools/draw_boxes.py --images data --recursive --limit 30 --out output/preview

产物：
  <out>/xxx.png          每张图一张带框预览
  <out>/sheet_01.png     缩略图拼版（默认每 12 张一张，方便一次看完）
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import cv2
import numpy as np

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp"}

COLORS = [
    (0, 255, 0), (0, 200, 255), (255, 120, 0), (255, 0, 200),
    (0, 255, 255), (180, 105, 255), (60, 200, 60), (0, 120, 255),
]


def boxes_of(data: dict):
    """返回 [(label, (x1,y1,x2,y2)), ...]，矩形只取 2 个点也能算"""
    out = []
    for sh in data.get("shapes") or []:
        pts = sh.get("points") or []
        if len(pts) < 2:
            continue
        xs = [float(p[0]) for p in pts]
        ys = [float(p[1]) for p in pts]
        out.append((sh.get("label", ""), (min(xs), min(ys), max(xs), max(ys))))
    return out


def draw(img: np.ndarray, items, thickness: int) -> np.ndarray:
    canvas = img.copy()
    for i, (label, (x1, y1, x2, y2)) in enumerate(items):
        color = COLORS[i % len(COLORS)]
        p1 = (int(round(x1)), int(round(y1)))
        p2 = (int(round(x2)), int(round(y2)))
        cv2.rectangle(canvas, p1, p2, color, thickness, cv2.LINE_AA)

        text = label or "object"
        (tw, th), base = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.55, 1)
        ty = max(p1[1] - 6, th + 6)
        cv2.rectangle(canvas, (p1[0], ty - th - 4), (p1[0] + tw + 6, ty + base - 2),
                      color, cv2.FILLED)
        cv2.putText(canvas, text, (p1[0] + 3, ty - 2), cv2.FONT_HERSHEY_SIMPLEX, 0.55,
                    (0, 0, 0), 1, cv2.LINE_AA)
    return canvas


def make_sheet(items, cols: int, tile_w: int) -> np.ndarray:
    """items: [(文件名, 图)]，等比缩放到 tile_w，拼成网格"""
    tiles = []
    for name, img in items:
        h, w = img.shape[:2]
        scale = tile_w / float(w)
        tile = cv2.resize(img, (tile_w, max(1, int(round(h * scale)))), interpolation=cv2.INTER_AREA)
        # 顶部留一条黑带写文件名（只写 ASCII 文件名，cv2 不支持中文）
        header = np.zeros((26, tile_w, 3), np.uint8)
        cv2.putText(header, Path(name).name[:44], (6, 19), cv2.FONT_HERSHEY_SIMPLEX, 0.45,
                    (200, 200, 200), 1, cv2.LINE_AA)
        tiles.append(np.vstack([header, tile]))

    max_h = max(t.shape[0] for t in tiles)
    padded = []
    for t in tiles:
        if t.shape[0] < max_h:
            pad = np.zeros((max_h - t.shape[0], t.shape[1], 3), np.uint8)
            t = np.vstack([t, pad])
        padded.append(t)

    rows = []
    for i in range(0, len(padded), cols):
        row = padded[i:i + cols]
        while len(row) < cols:
            row.append(np.zeros_like(padded[0]))
        rows.append(np.hstack(row))
    return np.vstack(rows)


def main() -> int:
    ap = argparse.ArgumentParser(description="把 json 的框画到副本上，生成预览图/拼版")
    ap.add_argument("--images", required=True, help="图片目录")
    ap.add_argument("--out", default="output/preview", help="输出目录，默认 output/preview")
    ap.add_argument("--recursive", action="store_true", help="递归子目录（多组一起出）")
    ap.add_argument("--limit", type=int, default=0, help="最多处理多少张，0 = 全部")
    ap.add_argument("--thickness", type=int, default=2, help="框线粗细")
    ap.add_argument("--sheet", type=int, default=12, help="每张拼版放几张，0 = 不出拼版")
    ap.add_argument("--cols", type=int, default=4, help="拼版列数")
    ap.add_argument("--tile-width", type=int, default=560, help="拼版里每张的宽度")
    ap.add_argument("--with-label", action="store_true", help="在框上写类别名（默认也写）")
    args = ap.parse_args()

    root = Path(args.images)
    if not root.is_dir():
        print(f"[错误] 不是目录: {root}", file=sys.stderr)
        return 1

    it = root.rglob("*") if args.recursive else root.iterdir()
    images = sorted(p for p in it if p.is_file() and p.suffix.lower() in IMAGE_EXTS)
    if args.limit > 0:
        images = images[:args.limit]
    if not images:
        print(f"[错误] {root} 里没有图片", file=sys.stderr)
        return 1

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    done = no_json = 0
    batch, sheet_idx = [], 0
    for img_path in images:
        js = img_path.with_suffix(".json")
        if not js.exists():
            no_json += 1
            continue
        try:
            data = json.loads(js.read_text(encoding="utf-8"))
        except Exception as exc:  # noqa: BLE001
            print(f"[skip] {js.name} 解析失败: {exc}", file=sys.stderr)
            continue

        img = cv2.imread(str(img_path), cv2.IMREAD_COLOR)
        if img is None:
            print(f"[skip] 读不了 {img_path.name}", file=sys.stderr)
            continue

        items = boxes_of(data)
        vis = draw(img, items, args.thickness)

        # 角标：文件名 + 框数
        txt = f"{img_path.name}  boxes={len(items)}"
        cv2.putText(vis, txt, (8, vis.shape[0] - 12), cv2.FONT_HERSHEY_SIMPLEX, 0.5,
                    (255, 255, 255), 2, cv2.LINE_AA)
        cv2.putText(vis, txt, (8, vis.shape[0] - 12), cv2.FONT_HERSHEY_SIMPLEX, 0.5,
                    (0, 0, 0), 1, cv2.LINE_AA)

        rel = img_path.relative_to(root).with_suffix(".png")
        dst = out_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(str(dst), vis)
        done += 1

        if args.sheet > 0:
            batch.append((img_path.name, vis))
            if len(batch) >= args.sheet:
                sheet_idx += 1
                cv2.imwrite(str(out_dir / f"sheet_{sheet_idx:02d}.png"),
                            make_sheet(batch, args.cols, args.tile_width))
                batch = []

    if args.sheet > 0 and batch:
        sheet_idx += 1
        cv2.imwrite(str(out_dir / f"sheet_{sheet_idx:02d}.png"),
                    make_sheet(batch, args.cols, args.tile_width))

    print(f"[完成] 生成 {done} 张预览图 -> {out_dir}")
    if args.sheet > 0:
        print(f"       拼版 {sheet_idx} 张（sheet_XX.png，适合直接发群里/贴报告）")
    if no_json:
        print(f"[提示] {no_json} 张图没有对应 json（还没粗标/没标注），已跳过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
