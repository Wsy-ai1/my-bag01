#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
convert_tiff.py —— 把 .tif / .tiff 批量转成 .png（可选顺带改小、修 json）

为什么需要：
  * X-AnyLabeling 对 16bit / 多页 TIFF 支持不稳，容易出现"打不开/全黑/很慢"
  * TIFF 一般没压缩，1280×1024 一张 3~4 MB，1500 张就是 5 GB+，打包/上传都难受
  * 后续转 YOLO 训练时，png/jpg 才是通用格式
  * 框坐标不受格式影响，但 json 里的 imagePath 要跟着改成新文件名

用法：
  python3 tools/convert_tiff.py data/wsy_s01                 # 转成 png，保留 tif
  python3 tools/convert_tiff.py data/wsy_s01 --delete        # 转成功后删掉 tif
  python3 tools/convert_tiff.py data --recursive --delete    # 整个 data 目录递归处理
  python3 tools/convert_tiff.py data/wsy_s01 --no-stretch    # 16bit 直接右移 8 位（不做拉伸）
  python3 tools/convert_tiff.py data/wsy_s01 --format jpg --quality 95
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import cv2
import numpy as np

TIFF_EXTS = {".tif", ".tiff"}


def to_8bit(img: np.ndarray, stretch: bool) -> np.ndarray:
    """16bit/32bit -> 8bit；stretch=False 时直接右移，保持绝对亮度"""
    if img.dtype == np.uint8:
        return img

    if img.dtype == np.uint16:
        if stretch:
            lo, hi = np.percentile(img, [1.0, 99.0])
            if hi <= lo:
                hi = lo + 1.0
            out = (img.astype(np.float32) - float(lo)) * (255.0 / (float(hi) - float(lo)))
            return np.clip(out, 0, 255).astype(np.uint8)
        return (img >> 8).astype(np.uint8)

    if img.dtype in (np.float32, np.float64):
        lo, hi = float(np.nanmin(img)), float(np.nanmax(img))
        if hi <= lo:
            hi = lo + 1.0
        out = (img - lo) * (255.0 / (hi - lo))
        return np.clip(np.nan_to_num(out), 0, 255).astype(np.uint8)

    return cv2.normalize(img, None, 0, 255, cv2.NORM_MINMAX).astype(np.uint8)


def iter_tiffs(root: Path, recursive: bool):
    it = root.rglob("*") if recursive else root.iterdir()
    for p in sorted(it):
        if p.is_file() and p.suffix.lower() in TIFF_EXTS:
            yield p


def main() -> int:
    ap = argparse.ArgumentParser(description="TIFF 批量转 PNG/JPG，并修正同名 json 的 imagePath")
    ap.add_argument("path", help="图片目录（或单个 tif 文件）")
    ap.add_argument("--recursive", action="store_true", help="递归子目录")
    ap.add_argument("--format", default="png", choices=["png", "jpg"], help="输出格式，默认 png")
    ap.add_argument("--quality", type=int, default=95, help="jpg 质量")
    ap.add_argument("--no-stretch", action="store_true", help="16bit 不做 1%%~99%% 拉伸，直接右移 8 位")
    ap.add_argument("--delete", action="store_true", help="转换成功后删除原 tif")
    ap.add_argument("--force", action="store_true", help="目标文件已存在也重写")
    args = ap.parse_args()

    root = Path(args.path)
    if root.is_file():
        files = [root] if root.suffix.lower() in TIFF_EXTS else []
    elif root.is_dir():
        files = list(iter_tiffs(root, args.recursive))
    else:
        print(f"[错误] 路径不存在: {root}", file=sys.stderr)
        return 1

    if not files:
        print(f"[提示] {root} 下没有 .tif/.tiff 文件")
        return 0

    ok = skipped = failed = 0
    for tif in files:
        img = cv2.imread(str(tif), cv2.IMREAD_UNCHANGED)
        if img is None:
            print(f"[fail] 读不了 {tif}（可能是多页/特殊压缩 TIFF）", file=sys.stderr)
            failed += 1
            continue

        img8 = to_8bit(img, stretch=not args.no_stretch)
        if img8.ndim == 2:
            out_img = img8
        elif img8.shape[2] == 4:
            out_img = cv2.cvtColor(img8, cv2.COLOR_BGRA2BGR)
        else:
            out_img = img8

        dst = tif.with_suffix("." + args.format)
        if dst.exists() and not args.force:
            print(f"[skip] {dst.name} 已存在（--force 可覆盖）")
            skipped += 1
            continue

        if args.format == "jpg":
            write_ok = cv2.imwrite(str(dst), out_img, [int(cv2.IMWRITE_JPEG_QUALITY), args.quality])
        else:
            write_ok = cv2.imwrite(str(dst), out_img)
        if not write_ok:
            print(f"[fail] 写不了 {dst}", file=sys.stderr)
            failed += 1
            continue

        # 同名 json 的 imagePath 跟着改（json 名不随扩展名变，因为 stem 一样）
        js = tif.with_suffix(".json")
        if js.exists():
            try:
                data = json.loads(js.read_text(encoding="utf-8"))
                data["imagePath"] = dst.name
                js.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
            except Exception as exc:  # noqa: BLE001
                print(f"[warn] {js.name} 更新 imagePath 失败: {exc}", file=sys.stderr)

        if args.delete:
            tif.unlink()

        ok += 1
        if ok <= 5 or ok % 50 == 0:
            print(f"[ok] {tif.name} -> {dst.name}  ({img.shape[1]}x{img.shape[0]}, {img.dtype})")

    print(f"\n[完成] 转换 {ok}，跳过 {skipped}，失败 {failed}")
    if failed:
        print("[提示] 失败的通常是多页 TIFF 或 12/14bit 打包格式，用 ImageMagick 试：")
        print("       mogrify -format png *.tif")
    return 0 if failed == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
