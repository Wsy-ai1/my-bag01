#pragma once
/**
 * hikrobot.hpp —— 海康机器人 USB 工业相机后端（MVS SDK）
 *
 * 取图流程（对应讲义 3.2）：
 *   MV_CC_EnumDevices -> MV_CC_CreateHandle -> MV_CC_OpenDevice
 *   -> 关自动曝光/增益，写曝光与增益 -> MV_CC_StartGrabbing
 *   -> 取图线程循环 MV_CC_GetImageBuffer -> Bayer 转 BGR -> 入队
 *   -> MV_CC_FreeImageBuffer（必须，否则不会再出图）
 * 析构时 MV_CC_StopGrabbing -> MV_CC_CloseDevice -> MV_CC_DestroyHandle。
 *
 * 未安装 MVS SDK 时编译 hikrobot_stub.cpp，构造对象会直接抛异常，
 * 其余后端仍可正常使用。
 */

#include <memory>

#include "io/camera_base.hpp"

namespace io {

class HikRobot final : public CameraBase {
public:
    explicit HikRobot(const CameraConfig& config);
    ~HikRobot() override;

    HikRobot(const HikRobot&)            = delete;
    HikRobot& operator=(const HikRobot&) = delete;

    bool open() override;
    void close() override;
    bool isOpen() const override;

    bool read(cv::Mat& img, TimePoint& timestamp, int timeout_ms = 1000) override;

    std::string name() const override { return "hikrobot"; }
    int width()  const override;
    int height() const override;

    /** 取图线程实测的最近一秒帧率（0 表示还没统计出来） */
    double measuredFps() const;

    /** 取图线程是否因 SDK 错误退出，返回错误码（0 = 正常） */
    int lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace io
