#pragma once
/**
 * opencv_camera.hpp —— OpenCV 后端（笔记本摄像头 / USB 摄像头 / 视频文件）
 *
 * 用途：没有海康相机时先把整条链路（配置、显示、帧率、保存、标定）跑通，
 * 上机时只改 configs/camera.yaml 的 camera_name 即可。
 */

#include <opencv2/videoio.hpp>

#include "io/camera_base.hpp"

namespace io {

class OpenCvCamera final : public CameraBase {
public:
    explicit OpenCvCamera(const CameraConfig& config);
    ~OpenCvCamera() override;

    bool open() override;
    void close() override;
    bool isOpen() const override;

    bool read(cv::Mat& img, TimePoint& timestamp, int timeout_ms = 1000) override;

    std::string name() const override { return video_file_.empty() ? "opencv" : "video"; }
    int width()  const override { return width_; }
    int height() const override { return height_; }

private:
    CameraConfig  config_;
    std::string   video_file_;
    cv::VideoCapture capture_;
    int           width_ = 0;
    int           height_ = 0;
};

}  // namespace io
