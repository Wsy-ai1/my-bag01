#!/usr/bin/env bash
# ============================================================================
# setup_ubuntu.sh —— Ubuntu 20.04/22.04/24.04 环境自检 + 海康相机权限
#   用法： bash scripts/setup_ubuntu.sh
#   说明： 只做检查和小改动，不会自动下载/安装 MVS（需要海康账号手动装）
# ============================================================================
set -uo pipefail

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[1;33m'; NC='\033[0m'
ok()   { echo -e "${GREEN}[ok]${NC}   $*"; }
warn() { echo -e "${YELLOW}[warn]${NC} $*"; }
bad()  { echo -e "${RED}[fail]${NC} $*"; }

echo "== 1. 编译与 OpenCV =================================================="
for tool in cmake g++ pkg-config; do
  if command -v "$tool" >/dev/null 2>&1; then ok "$tool = $($tool --version | head -1)"; else bad "缺少 $tool：sudo apt install -y build-essential cmake pkg-config"; fi
done

if pkg-config --exists opencv4 2>/dev/null; then
  ok "OpenCV $(pkg-config --modversion opencv4)（pkg-config 可见）"
elif [ -d /usr/local/lib/cmake/opencv4 ]; then
  ok "OpenCV (cmake 包位于 /usr/local/lib/cmake/opencv4)"
else
  warn "pkg-config 找不到 opencv4。若你是源码装的，编译时请加 -DOpenCV_DIR=/path/to/opencv/build"
fi

echo
echo "== 2. 海康 MVS SDK ===================================================="
MVS_ROOT=${MVS_ROOT:-/opt/MVS}
if [ -f "$MVS_ROOT/include/MvCameraControl.h" ]; then
  ok "头文件 $MVS_ROOT/include/MvCameraControl.h"
else
  bad "没找到 $MVS_ROOT/include/MvCameraControl.h —— 先装 MVS（见 docs/01_环境与MVS安装.md）"
fi
if ls "$MVS_ROOT"/lib/64/libMvCameraControl.so* >/dev/null 2>&1; then
  ok "动态库 $(ls "$MVS_ROOT"/lib/64/libMvCameraControl.so* | head -1)"
else
  bad "没找到 $MVS_ROOT/lib/64/libMvCameraControl.so"
fi

echo
echo "== 3. USB 权限（2bdf = 海康机器人） =================================="
RULE=/etc/udev/rules.d/99-hikrobot.rules
if [ -f "$RULE" ]; then
  ok "已存在 $RULE"
else
  echo "将写入 udev 规则（需要 sudo）："
  echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="2bdf", MODE="0666", GROUP="plugdev"'
  sudo tee "$RULE" >/dev/null <<'EOF'
# 海康机器人 USB 工业相机：允许 plugdev 组读写
SUBSYSTEM=="usb", ATTR{idVendor}=="2bdf", MODE="0666", GROUP="plugdev"
EOF
  sudo udevadm control --reload-rules
  sudo udevadm trigger
  ok "已写入并重载 udev"
fi

if id -nG "$USER" | grep -qw plugdev; then
  ok "$USER 已在 plugdev 组"
else
  warn "$USER 不在 plugdev 组，执行：sudo usermod -aG plugdev $USER  然后注销重登"
fi

echo
echo "== 4. 动态库搜索路径 =================================================="
if grep -qs "$MVS_ROOT/lib/64" /etc/ld.so.conf.d/*.conf; then
  ok "ld.so.conf 已包含 $MVS_ROOT/lib/64"
else
  echo "$MVS_ROOT/lib/64" | sudo tee /etc/ld.so.conf.d/mvs.conf >/dev/null
  sudo ldconfig
  ok "已写入 /etc/ld.so.conf.d/mvs.conf 并 ldconfig"
fi

echo
echo "== 5. 相机是否插好 ===================================================="
if command -v lsusb >/dev/null 2>&1; then
  if lsusb | grep -i '2bdf'; then ok "检测到海康 USB 设备"; else warn "lsusb 没有 2bdf:xxxx —— 检查线缆/供电/接口"; fi
else
  warn "没有 lsusb：sudo apt install -y usbutils"
fi

echo
echo "接下来： cmake -B build && cmake --build build -j\$(nproc)"
echo "然后：   ./build/camera_test --config-path=configs/camera.yaml --display"
