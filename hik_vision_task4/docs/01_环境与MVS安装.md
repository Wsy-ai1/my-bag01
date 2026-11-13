# 01 · Ubuntu 环境与 MVS 安装

> 目标：`lsusb` 能看到相机、`/opt/MVS/include/MvCameraControl.h` 存在、`cmake` 能找到 OpenCV。

## 1. 系统与依赖

推荐 Ubuntu 20.04 / 22.04 / 24.04（x86_64）。MVS 官方对 22.04 支持最好。

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config git \
                    libopencv-dev python3-opencv usbutils v4l-utils
```

检查 OpenCV：

```bash
pkg-config --modversion opencv4      # 例如 4.5.4
```

如果你是**源码安装**的 OpenCV（`/usr/local`），`pkg-config` 可能看不到，编译时改用：

```bash
cmake -B build -DOpenCV_DIR=/usr/local/lib/cmake/opencv4
```

## 2. 安装 MVS（海康机器人客户端 + SDK）

### 2.1 下载

1. 打开海康机器人官网下载中心：<https://www.hikrobotics.com/cn/machinevision/service/download>
2. 注册/登录账号（下载需要账号）
3. 产品分类选 **机器视觉 → 客户端 → MVS**，系统选 **Linux**，架构 **x86_64**
4. 下载对应 Ubuntu 版本的安装包（通常是 `MVS-x.x.x_x86_64_YYYYMMDD.zip` / `.tar.gz` / `.deb`）

> 讲义里说的"B 站搜安装教程"就是这个流程；版本号不必和讲义一致，能用即可。
> 也可以直接搜关键词：`海康机器人 MVS Ubuntu 安装`、`MVS Ubuntu 相机权限`。

### 2.2 安装

常见两种包：

```bash
# A. deb 包
sudo dpkg -i MVS-*_amd64.deb
sudo apt -f install          # 补依赖

# B. tar.gz / zip（里面是 MVS.sh）
unzip MVS-*.zip -d mvs_install && cd mvs_install
chmod +x MVS.sh
sudo ./MVS.sh                # 一路回车，默认装到 /opt/MVS
```

安装完成后目录结构：

```
/opt/MVS/
├── bin/MVS                       # 图形客户端
├── include/MvCameraControl.h     # SDK 头文件（编译要这个）
├── include/PixelType.h
├── lib/64/libMvCameraControl.so  # SDK 动态库
└── Samples/64/C++/...            # 官方例程（GrabImage 等）
```

### 2.3 权限与动态库（关键，80% 的"找不到相机"都在这里）

```bash
# 1) USB 权限：允许普通用户读写海康设备（VID 2bdf）
sudo tee /etc/udev/rules.d/99-hikrobot.rules >/dev/null <<'EOF'
SUBSYSTEM=="usb", ATTR{idVendor}=="2bdf", MODE="0666", GROUP="plugdev"
EOF
sudo udevadm control --reload-rules
sudo udevadm trigger          # 然后拔插一次相机

# 2) 把当前用户加进 plugdev（需要注销重登才生效）
sudo usermod -aG plugdev $USER

# 3) 让运行时找得到 libMvCameraControl.so
echo "/opt/MVS/lib/64" | sudo tee /etc/ld.so.conf.d/mvs.conf
sudo ldconfig
```

> 本仓库的 `bash scripts/setup_ubuntu.sh` 会把上面三件事做完并逐项自检。

### 2.4 先验证客户端，再写代码

```bash
/opt/MVS/bin/MVS          # 打开图形客户端
```

在客户端里确认：

- 左侧设备树能看到相机（USB3.0 接口）
- 点"连续采集"能出画面
- 能改曝光、增益，画面亮暗跟着变

**客户端都取不到图时不要碰代码**，先查：USB3 线是否插在蓝色口、相机指示灯、`lsusb | grep 2bdf`、是否被别的进程占用。

## 3. 编译本工程

```bash
cd hik_vision_task4
bash scripts/build.sh
# 等价于：
# cmake -B build -DCMAKE_BUILD_TYPE=Release -DMVS_ROOT=/opt/MVS
# cmake --build build -j$(nproc)
```

看到这样的输出说明 SDK 接上了：

```
-- HikRobot MVS: /opt/MVS/lib/64/libMvCameraControl.so (headers: /opt/MVS/include)
```

如果显示 `未找到海康 MVS SDK`：只编出了 OpenCV 后端，`camera_name: hikrobot` 会在运行时抛异常。此时先用 `configs/camera_opencv.yaml` 调试其它逻辑。

## 4. 常见问题

| 现象 | 原因 / 处理 |
| --- | --- |
| `cannot find -lMvCameraControl` | `-DMVS_ROOT` 不对；或库在 `/opt/MVS/lib/64` 而 CMake 找的是 `/opt/MVS/lib` |
| 运行时报 `libMvCameraControl.so: cannot open shared object file` | 没 `ldconfig`，或没用 `scripts/setup_ubuntu.sh` |
| `MV_CC_EnumDevices` 返回 0 个设备 | udev 规则没生效（拔插相机）、不在 plugdev 组、USB2 口供电不足、MVS 客户端占用 |
| 打开设备报 `MV_E_HANDLE` / 访问被拒 | 相机被 MVS 客户端或上一个进程占用，先关掉 |
| 图像全黑 | 曝光太短；先 `exposure_ms: 10` 试；或镜头盖没摘 |
| 图像偏绿/彩色错乱 | `pixel_format` 与相机实际输出不符，试 `BayerRG8` / `BayerGB8` / `Mono8` |
| 帧率只有个位数 | 曝光时间过长（曝光 20ms 理论上限 50fps）、USB 带宽不足、开了自动曝光 |
