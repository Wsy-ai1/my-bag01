/**
 * opencv_camera.cpp —— OpenCV 摄像头 / 视频文件后端
 */

#include "io/opencv_camera.hpp"

#include <stdexcept>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace io {

OpenCvCamera::OpenCvCamera(const CameraConfig& config)
    : config_(config), video_file_(config.camera_name == "video" ? config.video_file : "") {}

OpenCvCamera::~OpenCvCamera() = default;

bool OpenCvCamera::open() {
    if (capture_.isOpened()) {
        return true;
    }

    bool ok = false;
    if (!video_file_.empty()) {
        ok = capture_.open(video_file_);
    } else {
        ok = capture_.open(config_.device_index);
    }
    if (!ok) {
        return false;
    }

    if (config_.width > 0) {
        capture_.set(cv::CAP_PROP_FRAME_WIDTH, config_.width);
    }
    if (config_.height > 0) {
        capture_.set(cv::CAP_PROP_FRAME_HEIGHT, config_.height);
    }
    if (config_.fps > 0.0) {
        capture_.set(cv::CAP_PROP_FPS, config_.fps);
    }

    cv::Mat probe;
    if (!capture_.read(probe) || probe.empty()) {
        capture_.release();
        return false;
    }
    width_  = probe.cols;
    height_ = probe.rows;
    return true;
}

void OpenCvCamera::close() {
    capture_.release();
    width_  = 0;
    height_ = 0;
}

bool OpenCvCamera::isOpen() const {
    return capture_.isOpened();
}

bool OpenCvCamera::read(cv::Mat& img, TimePoint& timestamp, int /*timeout_ms*/) {
    if (!capture_.isOpened()) {
        return false;
    }
    cv::Mat frame;
    if (!capture_.read(frame) || frame.empty()) {
        return false;
    }
    timestamp = std::chrono::steady_clock::now();

    if (config_.flip_horizontal) {
        cv::flip(frame, frame, 1);
    }
    if (config_.flip_vertical) {
        cv::flip(frame, frame, 0);
    }

    width_  = frame.cols;
    height_ = frame.rows;
    img     = std::move(frame);
    return true;
}

}  // namespace io
