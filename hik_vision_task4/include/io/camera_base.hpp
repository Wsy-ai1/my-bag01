#pragma once
/**
 * camera_base.hpp —— 相机统一接口
 *
 * 所有后端（海康 HikRobot / OpenCV 摄像头 / 视频文件）都实现 CameraBase，
 * 上层只调用 read(img, timestamp)，换相机不用改识别代码。
 */

#include <chrono>
#include <string>

#include <opencv2/core.hpp>

namespace io {

using TimePoint = std::chrono::steady_clock::time_point;

/** 相机配置，字段与 configs/camera.yaml 一一对应 */
struct CameraConfig {
    std::string camera_name   = "opencv";   ///< hikrobot | opencv | video
    double      exposure_ms   = 3.0;        ///< 曝光（毫秒），驱动内 *1000 转微秒
    double      gain          = 10.0;       ///< 增益（dB）
    std::string vid_pid       = "2bdf:0001";///< USB VID:PID，lsusb 可查
    int         width         = 0;          ///< 0 = 用相机默认值
    int         height        = 0;
    double      fps           = 0.0;        ///< 0 = 不设置
    std::string pixel_format  = "BayerRG8"; ///< BayerRG8 / Mono8 / RGB8 ...
    int         device_index  = 0;          ///< opencv 后端：/dev/videoN 的 N
    std::string video_file;                 ///< video 后端：视频路径
    bool        flip_horizontal = false;
    bool        flip_vertical   = false;

    /** 从 YAML 读取（cv::FileStorage，支持 %YAML:1.0 头） */
    static CameraConfig fromYaml(const std::string& path);

    void validate() const;
    std::string toStr() const;
};

/** 相机抽象基类 */
class CameraBase {
public:
    virtual ~CameraBase() = default;

    virtual bool open()  = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    /**
     * 取一帧图像。
     * @param img        输出 BGR 三通道图/灰度图
     * @param timestamp  出帧时刻（steady_clock）
     * @param timeout_ms 等待超时
     * @return 成功取到返回 true
     */
    virtual bool read(cv::Mat& img, TimePoint& timestamp, int timeout_ms = 1000) = 0;

    virtual std::string name() const = 0;
    virtual int width()  const = 0;
    virtual int height() const = 0;
};

}  // namespace io
