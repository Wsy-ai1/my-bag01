# hik_vision_task4

Task 4「海康相机取图 + 相机标定」的完整工程，外加 AAA 能量单元数据集的采集/标注工具链。
目标平台：**Ubuntu（已装 OpenCV）+ 海康 USB3 工业相机 + MVS SDK**。

- 相机取图：`io::Camera` 统一接口，海康 MVS 后端 + OpenCV 后端（无硬件也能开发）
- 相机标定：圆点标定板检测 → `calibrateCamera` → 重投影误差 → 内参 YAML → 去畸变对比
- 数据集：X-AnyLabeling 标注规范、OpenCV 粗标脚本、质检脚本、YOLO 转换、打包脚本

## 目录

```
hik_vision_task4/
├── CMakeLists.txt
├── configs/
│   ├── camera.yaml              # 海康相机参数（曝光/增益/VID:PID/像素格式）
│   ├── camera_opencv.yaml       # 无相机时用笔记本摄像头
│   ├── camera_video.yaml        # 用视频文件回放（示例）
│   └── calibration.yaml         # 标定板行列数/圆心间距/误差阈值
├── include/
│   ├── io/camera_base.hpp       # CameraBase + CameraConfig
│   ├── io/camera.hpp            # 门面：按 YAML 选后端
│   ├── io/opencv_camera.hpp
│   ├── io/hikrobot/hikrobot.hpp # 海康后端（PIMPL，SDK 只出现在 .cpp）
│   ├── io/latest_frame.hpp      # 容量 1 的线程安全帧队列
│   ├── io/cli.hpp               # --key=value / --key value 两种写法
│   └── vision/calib.hpp         # 圆点检测 / 三维点 / 重投影误差
├── src/…                        # 对应实现
├── apps/
│   ├── camera_test.cpp          # 任务二：实时取图、帧率、q 退出、s 存图
│   ├── capture_calib.cpp        # 采集标定图（只有检测到标定板才允许保存）
│   ├── calibrate_camera.cpp     # 任务三：标定 + 误差表 + 写 YAML
│   └── undistort_compare.cpp    # 去畸变前后对比图
├── tools/
│   ├── prelabel.py              # OpenCV 粗标 -> X-AnyLabeling JSON
│   ├── convert_tiff.py          # .tif/.tiff 批量转 png，并修正 json 的 imagePath
│   ├── draw_boxes.py            # 把 json 的框画到副本上，出预览图 / 拼版（发人看用）
│   ├── dataset_qa.py            # 标注质检（类别/越界/贴边/重复/缺失）
│   ├── to_yolo.py               # 转 YOLO 数据集
│   └── package_submission.sh    # 按规范打包 zip
├── scripts/
│   ├── setup_ubuntu.sh          # 依赖自检 + udev 权限 + ldconfig
│   └── build.sh
└── docs/
    ├── 01_环境与MVS安装.md
    ├── 02_取图与标定操作.md
    ├── 03_数据采集与标注SOP.md
    └── 04_数据清单与获取途径.md
```

## 快速开始（Ubuntu）

```bash
# 0) 依赖（OpenCV 若已源码安装可跳过 libopencv-dev）
sudo apt install -y build-essential cmake pkg-config libopencv-dev

# 1) 装好 MVS 后自检（会写入 udev 规则，需要 sudo）
bash scripts/setup_ubuntu.sh

# 2) 编译（自动探测 /opt/MVS，找不到就只编 OpenCV 后端）
bash scripts/build.sh
#   或手动：cmake -B build -DMVS_ROOT=/opt/MVS && cmake --build build -j

# 3) 没有相机也能先跑通（笔记本摄像头）
./build/camera_test --config-path=configs/camera_opencv.yaml --frames 60

# 4) 上机取图
./build/camera_test --config-path=configs/camera.yaml --display
#    q 退出，s 存当前原图到 assets/calibration_imgs/

# 5) 采标定图（实时检测圆点，检测到才存）
./build/capture_calib --config-path=configs/camera.yaml \
    --calibration-config=configs/calibration.yaml \
    --out-dir=assets/calibration_imgs --target=20

# 6) 标定
./build/calibrate_camera assets/calibration_imgs \
    --config-path=configs/calibration.yaml
#    输出 configs/camera_intrinsics.yaml + 每张图重投影误差

# 7) 去畸变对比
./build/undistort_compare --intrinsics=configs/camera_intrinsics.yaml \
    --input=assets/calibration_imgs/calib_0001.png --output=output
```

## 5 分钟自检（不需要相机）

```bash
# 用 18 张合成标定板图跑一遍标定，验证 OpenCV 链路
./build/calibrate_camera assets/demo_calib \
    --config-path=configs/calibration.yaml --output=output/demo_intrinsics.yaml
```

预期：`fx≈1182, fy≈1186, cx≈615, cy≈344`，平均重投影误差 ≈0.58 px（真值 1200/1200/640/360）。
这份结果只用来确认程序没编译/逻辑问题，**提交要用真实相机拍的标定板图**。

## 文档

| 文档 | 内容 |
| --- | --- |
| [01_环境与MVS安装](docs/01_环境与MVS安装.md) | Ubuntu 依赖、MVS 下载安装、udev 权限、ldconfig、排错表 |
| [02_取图与标定操作](docs/02_取图与标定操作.md) | 9 步取图流程与 SDK 调用、参数含义、采集与标定命令、结果判读 |
| [03_数据采集与标注SOP](docs/03_数据采集与标注SOP.md) | 能量单元拍什么/怎么拍/怎么标/质检/打包 |
| [04_数据清单与获取途径](docs/04_数据清单与获取途径.md) | 缺哪些数据、从哪拿、怎么拿、时间线、降级方案 |

## 命令速查

| 目的 | 命令 |
| --- | --- |
| 只统计帧率不显示 | `./build/camera_test --frames 300 --no-display` |
| 每秒自动存图 | `./build/camera_test --save-every 1.0` |
| 无界面自动采标定图 | `./build/capture_calib --no-display --auto 800 --target 20` |
| 覆盖标定板尺寸 | `./build/calibrate_camera <dir> --cols 10 --rows 7 --spacing 25` |
| 存检测结果图排查 | `./build/calibrate_camera <dir> --debug-dir output/detections` |
| 去畸变保留黑边/裁剪 | `--alpha 1`（保留全部像素）/ `--alpha 0`（裁掉黑边） |

## 本机自测结果（无相机，用合成数据验证过）

| 项目 | 结果 |
| --- | --- |
| 4 个可执行程序 | 全部编译通过（`-Wall -Wextra` 零警告） |
| 视频后端取图 | 45 帧正常，帧率统计正常 |
| 合成标定板 18 张（真值 fx=fy=1200, cx=640, cy=360） | 标定得 fx=1182.17, fy=1186.28, cx=615.53, cy=344.33，平均重投影误差 **0.58 px** |
| 采集程序 | 在含标定板的视频上自动存下 7 张，文件名 `calib_0001..0007.png` |
| 去畸变 | 生成 before/after/compare 三张图，桶形畸变明显被拉直 |
| 数据集工具 | 粗标 4 张出 280 个框；质检 0 问题；转 YOLO 3 train / 1 val |
| 合成标定板复跑 | `assets/demo_calib` 18 张 → 结果可复现（见下方自检） |

> 合成数据只用来看流程是否通，真实精度取决于标定板平整度、图片数量与角度覆盖。

## 任务要求对照

| 任务 | 要求 | 本工程对应 |
| --- | --- | --- |
| 任务一 | 说清「查找设备→打开→设参→取流→取图→还缓冲」 | `docs/02` 有逐行说明；`hikrobot.cpp` 里就是这 9 步 |
| 任务二 | SDK 取图转 `cv::Mat`、实时显示、q 退出、YAML 曝光增益、显示帧率、s 存原图 | `apps/camera_test.cpp` + `configs/camera.yaml` |
| 任务三 | 标定板参数入配置、≥15 张多角度图、`calibrateCamera`、输出 K/D/误差、去畸变对比 | `capture_calib` + `calibrate_camera` + `undistort_compare` |
| 提交 | 源码/CMake/YAML、演示截图、标定图、结果 YAML 与误差、对比图、说明 | 全部就位，打包见 `docs/03` 末尾 |

## 关于数据

代码只是容器，**真正要交的是数据和结果**：标定需要 15~25 张真实标定板照片，数据集需要每组 100~200 张能量单元照片 + 标注。
需要哪些东西、去哪里拿、怎么拍、怎么标，全部写在 [docs/04_数据清单与获取途径.md](docs/04_数据清单与获取途径.md)。

## 常见问题

- **cmake 报找不到 MvCameraControl.h**：MVS 没装或路径不同，`-DMVS_ROOT=/your/MVS`。
- **运行时 `libMvCameraControl.so: cannot open shared object file`**：`scripts/setup_ubuntu.sh` 第 4 步会写 `/etc/ld.so.conf.d/mvs.conf`。
- **枚举不到相机**：`lsusb | grep 2bdf`；检查 USB3 线/供电；MVS 客户端是否还开着占用了相机；udev 规则是否生效（重插相机）。
- **能取图但全黑/全绿**：`pixel_format` 与相机实际输出不一致，改成 BayerRG8 或 Mono8 再试。
- **标定检测不到圆点**：99% 是 `pattern_cols/rows/spacing` 与实际标定板不符，或标定板没拍全；用 `--debug-dir` 存图看检测结果。
- **重投影误差大**：图太少/角度变化小/有模糊反光；把误差明显偏大的那几张删掉重标。
