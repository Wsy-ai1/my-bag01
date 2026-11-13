# assets

| 目录 | 放什么 | 说明 |
| --- | --- | --- |
| `calibration_imgs/` | 真实标定板照片 15~25 张 | `capture_calib` 或 `camera_test` 的 `s` 键存到这里，文件名 `calib_0001.png…` |
| `demo_calib/` | 18 张**合成**标定板图（真值 fx=fy=1200, cx=640, cy=360） | 没有相机/标定板时验证标定程序是否正常 |

## 用合成图标定（自检，不是最终结果）

```bash
./build/calibrate_camera assets/demo_calib \
    --config-path=configs/calibration.yaml \
    --output=output/demo_intrinsics.yaml
```

预期输出（±0.5 像素级差异属正常）：

```
camera_matrix:
  [ 1182.17,    0.00,  615.53 ]
  [    0.00, 1186.28,  344.33 ]
  [    0.00,    0.00,    1.00 ]
mean reprojection error : 0.58 px
```

> 合成图是程序画出来的理想圆点，真实标定精度取决于标定板平整度、图片数量与角度覆盖，
> **提交材料必须用真实相机拍的真实标定板**。
