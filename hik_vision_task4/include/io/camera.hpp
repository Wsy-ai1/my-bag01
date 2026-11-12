#pragma once
/**
 * camera.hpp —— 统一相机对象（门面）
 *
 * 根据 YAML 里的 camera_name 创建对应后端：
 *   hikrobot -> 海康 MVS SDK
 *   opencv   -> /dev/videoN 或 Windows 摄像头
 *   video    -> 视频文件回放（无硬件时调试用）
 */

#include <memory>
#include <string>

#include "io/camera_base.hpp"

namespace io {

class Camera {
public:
    explicit Camera(const std::string& config_path);
    explicit Camera(const CameraConfig& config);
    ~Camera();

    Camera(const Camera&)            = delete;
    Camera& operator=(const Camera&) = delete;
    Camera(Camera&&) noexcept;
    Camera& operator=(Camera&&) noexcept;

    bool open();
    void close();
    bool isOpen() const;

    bool read(cv::Mat& img, TimePoint& timestamp, int timeout_ms = 1000);

    /** 便捷重载：只关心图 */
    cv::Mat read(int timeout_ms = 1000);

    std::string backend() const;
    int width()  const;
    int height() const;
    const CameraConfig& config() const;

private:
    std::unique_ptr<CameraBase> impl_;
    CameraConfig                cfg_;
};

}  // namespace io
