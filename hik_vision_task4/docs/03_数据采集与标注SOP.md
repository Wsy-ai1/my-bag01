# 03 · 能量单元数据集：采集与标注 SOP

> 来源：`能量单元数据集采集标注.pdf`。最终交付是**一个 zip**，里面按"一次拍摄一组"分文件夹，
> 每个文件夹 = 图片 + 同名预标注 json。

## 一、交付物结构

```
submission_YYYYmmdd.zip
├── wsy_s01/                 # 姓名缩写_s两位场景编号
│   ├── IMG_0001.png
│   ├── IMG_0001.json        # X-AnyLabeling 标注，class = energy_unit
│   ├── IMG_0002.png
│   ├── IMG_0002.json
│   └── ...
├── wsy_s02/
└── ...
```

- 文件夹名：`缩写_s01`…`缩写_s10`，**一组场景一个文件夹**
- 每组 **100~200 张** 就可以换场景
- json 与图片**同名同目录**（X-AnyLabeling 默认就是这样）
- 认不出是能量单元的照片用 `Ctrl+Shift+Delete` 删（会移到 `_delete_`，打包脚本已排除）

## 二、拍什么（场景清单）

| 类别 | 具体内容 |
| --- | --- |
| 在机构上 | 架子上插满能量单元；从满架开始一个一个取走，直到空架（空架不用拍） |
| 离开机构 | 单个放在桌上 / 地上 / 不同颜色背景前；拿在手里 |
| 不同角度 | 正面、侧面、端面（从短边看）；立着、斜着、躺着 |
| 不同距离 | 近（占画面一大块）、中、远（实际工作距离） |
| 其他要素 | 被手或别的东西挡住一部分；只有一部分在画面边缘；亮一点、暗一点、有反光 |

另外：**画面里只有 1 个** 和 **同时有 3~6 个** 的情况都要有。
建议两个人配合：一个拍，一个拔/放能量单元。

## 三、怎么拍

### 手机

- 用**专业模式**，拍之前把 ISO/快门/白平衡固定住，中途不要让它自动变
- 慢慢移动手机或物体，每张换一点角度/距离
- 略微糊但能认出是能量单元可以留；糊到看不清就删

### 海康相机（MVS 连续抓图）

1. 打开 MVS，**打开自动曝光**，增益尽量拉高（保证不糊）
2. 设置 → 录像/抓图 → 存图格式选 **PNG**
3. 取图间隔：**每 1 秒 1 张**，或**每 10 张抓 1 张**
4. 点"连续抓图"，再点"开始采集"，一边采集一边按场景要求移动物体/相机
5. 采完把图片按场景分文件夹，命名 `缩写_s01`

> 用 MVS 采的图和用 `build/camera_test --save-every` 存的原图都行——只要是同一场景的一组连续变化。

## 四、标注（X-AnyLabeling）

```bash
chmod +x X-AnyLabeling-v4.3.1-Linux-CPU
./X-AnyLabeling-v4.3.1-Linux-CPU
```

1. `File → Open Dir` 选这一组图片所在文件夹（**整个文件夹一次打开**）
2. 画框：按 `R`，拖出矩形
3. **第一次画框会弹输入框，填 `energy_unit`，确定**；之后自动沿用这个名字
4. 删框：鼠标点一下框 → `Delete`
5. 调大小：点一下框 → 拖框角
6. 撤销：`Ctrl + Z`
7. 上一张/下一张：`A` / `D`
8. 看整组进度：`Ctrl + G` 打开总览
9. 删废图：`Ctrl + Shift + Delete`（移到 `_delete_`）
10. 保存：`Ctrl + S`（切图时也会自动存，但养成按一下的习惯）

**质量要求：框要刚好把能量单元包住，不要多留背景，也不要切掉目标本体。**

### 照片是 .tif / .tiff 怎么办

TIFF 能用，但不建议直接标/交：

- X-AnyLabeling 对 **16bit / 多页 TIFF** 支持不稳，容易打不开或画面全黑
- TIFF 基本不压缩，1280×1024 一张 3~4 MB，1500 张就是 5 GB+
- 后续转 YOLO 训练也需要 png/jpg

先批量转成 png（顺带把同名 json 里的 `imagePath` 改好）：

```bash
python3 tools/convert_tiff.py data/wsy_s01 --delete        # 转完删 tif
python3 tools/convert_tiff.py data --recursive --delete    # 整个 data 递归处理
```

16bit 图默认按 1%~99% 分位拉伸到 8bit（不然会一片黑）；想保留绝对亮度用 `--no-stretch`。
转完再跑 `prelabel.py`，框坐标完全不受影响。

### 先粗标再人工修（强烈建议）

```bash
# 用 OpenCV 粗标一遍，生成可直接打开的 json
python3 tools/prelabel.py --images data/wsy_s01 --mode otsu
# 目标颜色鲜艳时试 --mode hsv；背景复杂试 --mode edge；光照不均试 --mode adaptive
# 想先看效果： --debug-dir output/prelabel

# 然后在 X-AnyLabeling 里打开同一目录，只做"增、删、微调"
```

粗标只是为了省时间，**最终每个框都要人眼过一遍**。

### 粗标没框 / 打开后看不到框？先分清三种情况

1. **看的是 PNG 原图** → 脚本不改图片，只写同名 `.json`；框要在 X-AnyLabeling 里 `File → Open Dir` 才看得到
2. **json 里 `shapes` 是空的** → 检测失败，按下面的表调参
3. **json 有框但软件不显示** → 图片和 json 必须同名同目录；打开的是"目录"不是单文件

快速确认某张图到底标了几个框（在项目根目录执行）：

```bash
python3 - <<'EOF'
import json, glob
f = sorted(glob.glob("data/wsy_s01/*.json"))[0]
d = json.load(open(f))
print(f, "->", len(d["shapes"]), "个框")
EOF
```

若结果是 0，按顺序试（每次只跑 2~3 张，几秒出结果）：

| 症状 | 参数 |
| --- | --- |
| 目标比背景**亮**（浅色能量单元 + 深色桌面） | `--invert` |
| 目标是彩色的 | `--mode hsv` |
| 目标和背景颜色接近、但有明显边 | `--mode edge` |
| 光照不均 / 有阴影 | `--mode adaptive` |
| 目标在画面里很小（远距离） | `--min-area 0.0001 --min-aspect 0.1 --max-aspect 10` |
| 想知道二值图长什么样 | `--debug-dir output/prelabel`，看 `*_mask.png` |
| 以前跑过、json 已存在被跳过 | 加 `--overwrite` |

脚本会逐张打印 `文件名: N 个框 (mask x%)`；某个文件 mask 覆盖率 > 60% 基本可判定阈值反了（把背景当目标），换 `--invert` 或换模式。

## 五、质检（打包前必做）

```bash
python3 tools/dataset_qa.py data --report output/qa_report.md
```

检查项：

- 每张图有没有 json、每个 json 有没有图
- 类别是不是都是 `energy_unit`（防手误 `energy unit` / `EnergyUnit`）
- 框是否越界、退化、过小
- 框是否贴边（目标被裁切）
- 两个框是否几乎完全重合（重复标注）
- 每组的图片数、框数、框/图 平均值

有问题的会逐条列出来，改完再跑，直到通过。

## 六、打包提交

```bash
bash tools/package_submission.sh data submission_20261112.zip
```

脚本会先跑一次质检，再打包，并自动排除 `_delete_`、`.DS_Store` 等垃圾。

## 七、（可选）转成 YOLO 训练集

```bash
python3 tools/to_yolo.py data --out datasets/energy_unit --val-ratio 0.2
# 生成 datasets/energy_unit/{images,labels}/{train,val} + data.yaml
```

## 八、工作量估算

| 项目 | 数量 | 说明 |
| --- | --- | --- |
| 场景 | 8~10 组 | 机构上 2~3 组、桌面/地面 3~4 组、手持 1~2 组、远近/遮挡/反光各 1 组 |
| 每组 | 100~200 张 | 合计约 1000~2000 张 |
| 拍摄 | 2~3 小时 | 两人配合，一人拍一人摆 |
| 标注 | 3~6 小时 | 用 `prelabel.py` 粗标后，人工修正约 5~10 秒/张 |
| 质检+打包 | 0.5 小时 | `dataset_qa.py` + `package_submission.sh` |

## 九、检查清单

- [ ] 文件夹命名 `缩写_s01..sNN`
- [ ] 每组 100~200 张，画面有变化
- [ ] 1 个目标 / 3~6 个目标同框 都有
- [ ] 角度、距离、遮挡、亮度、反光都覆盖
- [ ] 每张图都有同名 json，类别全是 `energy_unit`
- [ ] 框贴紧目标，没有越界/贴边/重复
- [ ] `dataset_qa.py` 无报错
- [ ] zip 里没有 `_delete_` 和废图
