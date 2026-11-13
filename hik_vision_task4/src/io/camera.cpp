/**
 * camera.cpp —— 配置解析 + 后端工厂
 */

#include "io/camera.hpp"

#include <sstream>
#include <stdexcept>

#include <opencv2/core.hpp>

#include "io/hikrobot/hikrobot.hpp"
#include "io/opencv_camera.hpp"

namespace io {
namespace {

template <typename T>
void readIf(const cv::FileStorage& fs, const char* key, T& value) {
    const cv::FileNode node = fs[key];
    if (!node.empty()) {
        node >> value;
    }
}

}  // namespace

std::string CameraConfig::toStr() const {
    std::ostringstream oss;
    oss << "camera=" << camera_name << " exposure_ms=" << exposure_ms
        << " gain=" << gain << " vid_pid=" << vid_pid
        << " size=" << width << "x" << height << " fps=" << fps
        << " pixel_format=" << pixel_format;
    return oss.str();
}

void CameraConfig::validate() const {
    if (camera_name != "hikrobot" && camera_name != "opencv" && camera_name != "video") {
        throw std::invalid_argument("camera_name 只能是 hikrobot / opencv / video，当前为: " +
                                    camera_name);
    }
    if (exposure_ms <= 0.0) {
        throw std::invalid_argument("exposure_ms 必须 > 0");
    }
    if (camera_name == "video" && video_file.empty()) {
        throw std::invalid_argument("camera_name=video 时必须提供 video_file");
    }
    if (!vid_pid.empty() && vid_pid.find(':') == std::string::npos) {
        throw std::invalid_argument("vid_pid 格式应为 2bdf:0001（lsusb 可查）");
    }
}

CameraConfig CameraConfig::fromYaml(const std::string& path) {
    cv::FileStorage fs(path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        throw std::runtime_error("打不开相机配置文件: " + path);
    }

    CameraConfig cfg;
    readIf(fs, "camera_name",  cfg.camera_name);
    readIf(fs, "exposure_ms",  cfg.exposure_ms);
    readIf(fs, "gain",         cfg.gain);
    readIf(fs, "vid_pid",      cfg.vid_pid);
    readIf(fs, "width",        cfg.width);
    readIf(fs, "height",       cfg.height);
    readIf(fs, "fps",          cfg.fps);
    readIf(fs, "pixel_format", cfg.pixel_format);
    readIf(fs, "device_index", cfg.device_index);
    readIf(fs, "video_file",   cfg.video_file);

    int flip_h = 0;
    int flip_v = 0;
    readIf(fs, "flip_horizontal", flip_h);
    readIf(fs, "flip_vertical",   flip_v);
    cfg.flip_horizontal = flip_h != 0;
    cfg.flip_vertical   = flip_v != 0;

    cfg.validate();
    return cfg;
}

/* ------------------------------ Camera 门面 ------------------------------ */

Camera::Camera(const std::string& config_path)
    : Camera(CameraConfig::fromYaml(config_path)) {}

Camera::Camera(const CameraConfig& config) : cfg_(config) {
    cfg_.validate();
    if (cfg_.camera_name == "hikrobot") {
        impl_ = std::make_unique<HikRobot>(cfg_);
    } else {
        impl_ = std::make_unique<OpenCvCamera>(cfg_);
    }
}

Camera::~Camera() = default;
Camera::Camera(Camera&&) noexcept            = default;
Camera& Camera::operator=(Camera&&) noexcept = default;

bool Camera::open()         { return impl_ && impl_->open(); }
void Camera::close()        { if (impl_) impl_->close(); }
bool Camera::isOpen() const { return impl_ && impl_->isOpen(); }

bool Camera::read(cv::Mat& img, TimePoint& timestamp, int timeout_ms) {
    if (!impl_) {
        return false;
    }
    return impl_->read(img, timestamp, timeout_ms);
}

cv::Mat Camera::read(int timeout_ms) {
    cv::Mat img;
    TimePoint ts;
    if (!read(img, ts, timeout_ms)) {
        return {};
    }
    return img;
}

std::string Camera::backend() const { return impl_ ? impl_->name() : "none"; }
int Camera::width()  const { return impl_ ? impl_->width() : 0; }
int Camera::height() const { return impl_ ? impl_->height() : 0; }
const CameraConfig& Camera::config() const { return cfg_; }

}  // namespace io
