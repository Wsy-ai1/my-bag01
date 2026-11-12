#!/usr/bin/env bash
# ============================================================================
# build.sh —— 一键编译
#   bash scripts/build.sh                 # 自动探测 /opt/MVS
#   bash scripts/build.sh -DMVS_ROOT=/opt/MVS
# ============================================================================
set -euo pipefail

cd "$(dirname "$0")/.."

JOBS=$(nproc 2>/dev/null || echo 4)

cmake -B build -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build -j"$JOBS"

echo
echo "编译完成："
ls -1 build/camera_test build/capture_calib build/calibrate_camera build/undistort_compare 2>/dev/null || true
echo
echo "先跑一遍无硬件自检： ./build/camera_test --config-path=configs/camera_opencv.yaml --frames 60"
