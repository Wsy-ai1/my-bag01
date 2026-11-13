#!/usr/bin/env bash
# ============================================================================
# package_submission.sh —— 按任务要求打包提交
#   一次拍摄 = 一个文件夹（图片 + 同名 json），整体打成一个 zip
#   用法： bash tools/package_submission.sh [数据目录] [压缩包名]
#   默认： data  ->  submission_YYYYmmdd_HHMM.zip
# ============================================================================
set -euo pipefail

DATA_DIR=${1:-data}
NAME=${2:-"submission_$(date +%Y%m%d_%H%M).zip"}

if [ ! -d "$DATA_DIR" ]; then
  echo "找不到数据目录: $DATA_DIR" >&2
  exit 1
fi

# 顺手做一次质检（有 python3 才做）
if command -v python3 >/dev/null 2>&1 && [ -f tools/dataset_qa.py ]; then
  echo "== 打包前质检 =="
  python3 tools/dataset_qa.py "$DATA_DIR" --report output/qa_report.md || true
  echo
fi

# 排除临时文件、_delete_ 目录、系统垃圾
zip -r -q "$NAME" "$DATA_DIR" \
  -x "*/_delete_/*" "*.DS_Store" "*/.ipynb_checkpoints/*" "*/__pycache__/*"

echo "已生成 $NAME ($(du -h "$NAME" | cut -f1))"
echo "检查内容：$(unzip -l "$NAME" | tail -1)"
echo
echo "提示：每组照片建议 100~200 张；画面里 1 个目标、3~6 个目标的情况都要有"
