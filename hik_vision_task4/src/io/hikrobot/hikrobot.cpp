/**
 * hikrobot.cpp —— 海康机器人 USB 相机后端（MVS SDK 实现）
 *
 * 只在这里包含 MVS 的头文件：上层代码完全不依赖海康 SDK。
 */

#include "io/hikrobot/hikrobot.hpp"

#include <atomic>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <opencv2/imgproc.hpp>

#include "io/latest_frame.hpp"

#if defined(HAS_HIKROBOT)
#  include <MvCameraControl.h>
#endif

namespace io {
namespace {

struct Frame {
    cv::Mat   image;
    TimePoint timestamp;
};

#if defined(HAS_HIKROBOT)

/** MV_CC_* 返回码转人话（只列常见几个，其余打印十进制码） */
std::string mvErrorText(int code) {
    // MV_E_* 是 0x8000xxxx 的无符号常量，用 unsigned 做 switch 才不会被当成窄化
    switch (static_cast<unsigned int>(code)) {
        case MV_OK:                 return "MV_OK";
        case MV_E_HANDLE:           return "MV_E_HANDLE（句柄无效）";
        case MV_E_SUPPORT:          return "MV_E_SUPPORT（不支持该功能/参数）";
        case MV_E_BUFOVER:          return "MV_E_BUFOVER（缓存已满）";
        case MV_E_CALLORDER:        return "MV_E_CALLORDER（调用顺序错误）";
        case MV_E_PARAMETER:        return "MV_E_PARAMETER（参数错误）";
        case MV_E_RESOURCE:         return "MV_E_RESOURCE（资源申请失败）";
        case MV_E_NODATA:           return "MV_E_NODATA（暂无数据）";
        case MV_E_PRECONDITION:     return "MV_E_PRECONDITION（前置条件不满足）";
        case MV_E_NOENOUGH_BUF:     return "MV_E_NOENOUGH_BUF（缓冲区不足）";
        case MV_E_ABNORMAL_IMAGE:   return "MV_E_ABNORMAL_IMAGE（图像异常，已丢弃）";
        case MV_E_UNKNOW:           return "MV_E_UNKNOW（未知错误）";
        default:                    return "错误码 " + std::to_string(code);
    }
}

bool mvOk(int code, const char* what, bool fatal) {
    if (code == MV_OK) {
        return true;
    }
    std::cerr << "[hikrobot] " << what << " 失败: " << mvErrorText(code) << '\n';
    if (fatal) {
        throw std::runtime_error(std::string("海康 SDK 调用失败: ") + what + " -> " +
                                 mvErrorText(code));
    }
    return false;
}

/** "2bdf:0001" -> vendor / product */
bool parseVidPid(const std::string& text, unsigned int& vid, unsigned int& pid) {
    const std::size_t pos = text.find(':');
    if (pos == std::string::npos) {
        return false;
    }
    try {
        vid = static_cast<unsigned int>(std::stoul(text.substr(0, pos), nullptr, 16));
        pid = static_cast<unsigned int>(std::stoul(text.substr(pos + 1), nullptr, 16));
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

/** 字符串像素格式 -> MVS 枚举值 */
bool pixelFormatFromString(const std::string& name, unsigned int& value) {
    if (name == "BayerRG8") { value = PixelType_Gvsp_BayerRG8; return true; }
    if (name == "BayerGR8") { value = PixelType_Gvsp_BayerGR8; return true; }
    if (name == "BayerGB8") { value = PixelType_Gvsp_BayerGB8; return true; }
    if (name == "BayerBG8") { value = PixelType_Gvsp_BayerBG8; return true; }
    if (name == "Mono8")    { value = PixelType_Gvsp_Mono8;    return true; }
    if (name == "RGB8")     { value = PixelType_Gvsp_RGB8_Packed; return true; }
    if (name == "BGR8")     { value = PixelType_Gvsp_BGR8_Packed; return true; }
    if (name == "YUYV")     { value = PixelType_Gvsp_YUV422_YUYV_Packed; return true; }
    return false;
}

#endif  // HAS_HIKROBOT

}  // namespace

/* ========================================================================== */

struct HikRobot::Impl {
    explicit Impl(const CameraConfig& config) : cfg(config) {}

    CameraConfig       cfg;
    LatestFrame<Frame> queue;

#if defined(HAS_HIKROBOT)
    void*  handle = nullptr;
    bool   opened = false;
    std::thread worker;

    std::atomic<bool>   running{false};
    std::atomic<int>    errorCode{0};
    std::atomic<int>    width{0};
    std::atomic<int>    height{0};
    std::atomic<double> fps{0.0};

    bool open();
    void close();
    bool read(cv::Mat& img, TimePoint& timestamp, int timeout_ms);

    int  pickDevice(const MV_CC_DEVICE_INFO_LIST& list) const;
    void applyParams();
    void grabLoop();
    cv::Mat toBgr(const MV_FRAME_OUT& out);
    void printDeviceInfo(const MV_CC_DEVICE_INFO& info) const;
#endif
};

#if defined(HAS_HIKROBOT)

int HikRobot::Impl::pickDevice(const MV_CC_DEVICE_INFO_LIST& list) const {
    unsigned int wantVid = 0;
    unsigned int wantPid = 0;
    const bool   matchVidPid = parseVidPid(cfg.vid_pid, wantVid, wantPid);

    int firstUsb = -1;
    for (unsigned int i = 0; i < list.nDeviceNum; ++i) {
        const MV_CC_DEVICE_INFO* info = list.pDeviceInfo[i];
        if (info == nullptr) {
            continue;
        }
        if (info->nTLayerType != MV_USB_DEVICE) {
            continue;
        }
        if (firstUsb < 0) {
            firstUsb = static_cast<int>(i);
        }
        const unsigned int vid = info->SpecialInfo.stUsb3VInfo.nVendorID;
        const unsigned int pid = info->SpecialInfo.stUsb3VInfo.nDeviceID;
        if (matchVidPid && vid == wantVid && pid == wantPid) {
            return static_cast<int>(i);
        }
    }
    if (firstUsb >= 0) {
        std::cerr << "[hikrobot] 没有 VID:PID = " << cfg.vid_pid
                  << " 的设备，退而使用第一个 USB 相机\n";
        return firstUsb;
    }
    return -1;
}

void HikRobot::Impl::printDeviceInfo(const MV_CC_DEVICE_INFO& info) const {
    if (info.nTLayerType == MV_USB_DEVICE) {
        const MV_USB3_DEVICE_INFO& usb = info.SpecialInfo.stUsb3VInfo;
        std::cout << "[hikrobot] USB 设备: " << usb.chModelName
                  << "  SN=" << usb.chSerialNumber
                  << "  VID:PID=" << std::hex << usb.nVendorID << ':' << usb.nDeviceID
                  << std::dec << '\n';
    }
}

void HikRobot::Impl::applyParams() {
    // 触发模式必须先关掉，否则只能拿到一帧
    mvOk(MV_CC_SetEnumValue(handle, "TriggerMode", MV_TRIGGER_MODE_OFF), "设置 TriggerMode", true);

    // 自动曝光/自动增益关掉，再用固定值，保证标定和识别时亮度稳定
    mvOk(MV_CC_SetEnumValue(handle, "ExposureAuto", MV_EXPOSURE_AUTO_MODE_OFF),
         "关闭自动曝光", false);
    mvOk(MV_CC_SetFloatValue(handle, "ExposureTime", cfg.exposure_ms * 1000.0),  // ms -> us
         "设置曝光时间", true);

    mvOk(MV_CC_SetEnumValue(handle, "GainAuto", MV_GAIN_MODE_OFF), "关闭自动增益", false);
    mvOk(MV_CC_SetFloatValue(handle, "Gain", cfg.gain), "设置增益", false);

    if (cfg.fps > 0.0) {
        mvOk(MV_CC_SetBoolValue(handle, "AcquisitionFrameRateEnable", true),
             "打开帧率控制", false);
        mvOk(MV_CC_SetFloatValue(handle, "AcquisitionFrameRate", cfg.fps), "设置帧率", false);
    }

    unsigned int pixelType = 0;
    if (pixelFormatFromString(cfg.pixel_format, pixelType)) {
        mvOk(MV_CC_SetEnumValue(handle, "PixelFormat", pixelType),
             ("设置像素格式 " + cfg.pixel_format).c_str(), false);
    } else {
        std::cerr << "[hikrobot] 未识别的 pixel_format: " << cfg.pixel_format
                  << "，保持相机默认值\n";
    }

    // 分辨率放在最后设置：改宽高可能需要同时改 OffsetX/OffsetY，失败只告警
    if (cfg.width > 0) {
        mvOk(MV_CC_SetIntValue(handle, "Width", static_cast<unsigned int>(cfg.width)),
             "设置宽度", false);
    }
    if (cfg.height > 0) {
        mvOk(MV_CC_SetIntValue(handle, "Height", static_cast<unsigned int>(cfg.height)),
             "设置高度", false);
    }
}

cv::Mat HikRobot::Impl::toBgr(const MV_FRAME_OUT& out) {
    const MV_FRAME_OUT_INFO_EX& info = out.stFrameInfo;
    const int w = static_cast<int>(info.nWidth);
    const int h = static_cast<int>(info.nHeight);
    if (w <= 0 || h <= 0 || out.pBufAddr == nullptr) {
        return {};
    }

    const auto* data = reinterpret_cast<const unsigned char*>(out.pBufAddr);
    cv::Mat bgr;

    switch (info.enPixelType) {
        case PixelType_Gvsp_Mono8: {
            const cv::Mat gray(h, w, CV_8UC1, const_cast<unsigned char*>(data));
            cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);
            break;
        }
        case PixelType_Gvsp_BayerRG8: {
            const cv::Mat raw(h, w, CV_8UC1, const_cast<unsigned char*>(data));
            cv::cvtColor(raw, bgr, cv::COLOR_BayerRG2BGR);
            break;
        }
        case PixelType_Gvsp_BayerGR8: {
            const cv::Mat raw(h, w, CV_8UC1, const_cast<unsigned char*>(data));
            cv::cvtColor(raw, bgr, cv::COLOR_BayerGR2BGR);
            break;
        }
        case PixelType_Gvsp_BayerGB8: {
            const cv::Mat raw(h, w, CV_8UC1, const_cast<unsigned char*>(data));
            cv::cvtColor(raw, bgr, cv::COLOR_BayerGB2BGR);
            break;
        }
        case PixelType_Gvsp_BayerBG8: {
            const cv::Mat raw(h, w, CV_8UC1, const_cast<unsigned char*>(data));
            cv::cvtColor(raw, bgr, cv::COLOR_BayerBG2BGR);
            break;
        }
        case PixelType_Gvsp_RGB8_Packed: {
            const cv::Mat rgb(h, w, CV_8UC3, const_cast<unsigned char*>(data));
            cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
            break;
        }
        case PixelType_Gvsp_BGR8_Packed: {
            bgr = cv::Mat(h, w, CV_8UC3, const_cast<unsigned char*>(data)).clone();
            break;
        }
        case PixelType_Gvsp_YUV422_YUYV_Packed: {
            const cv::Mat yuv(h, w, CV_8UC2, const_cast<unsigned char*>(data));
            cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_YUYV);
            break;
        }
        default: {
            static std::atomic<bool> warned{false};
            if (!warned.exchange(true)) {
                std::cerr << "[hikrobot] 暂不支持的像素格式 0x" << std::hex
                          << info.enPixelType << std::dec
                          << "，请在 configs/camera.yaml 里改成 BayerRG8/Mono8 等\n";
            }
            return {};
        }
    }

    if (cfg.flip_horizontal) {
        cv::flip(bgr, bgr, 1);
    }
    if (cfg.flip_vertical) {
        cv::flip(bgr, bgr, 0);
    }
    width  = bgr.cols;
    height = bgr.rows;
    return bgr;
}

void HikRobot::Impl::grabLoop() {
    std::size_t   frames = 0;
    TimePoint     lastStat = std::chrono::steady_clock::now();

    while (running.load(std::memory_order_relaxed)) {
        MV_FRAME_OUT out{};
        // MV_E_* 是 0x8000xxxx 无符号常量，统一按 unsigned 比较，避免有符号告警
        const unsigned int ret =
            static_cast<unsigned int>(MV_CC_GetImageBuffer(handle, &out, 200));

        if (ret == MV_OK) {
            const TimePoint ts = std::chrono::steady_clock::now();
            cv::Mat img = toBgr(out);
            mvOk(MV_CC_FreeImageBuffer(handle, &out), "归还缓冲区", false);  // 必须归还

            if (!img.empty()) {
                queue.push(Frame{std::move(img), ts});

                ++frames;
                const TimePoint now = std::chrono::steady_clock::now();
                const double elapsed =
                    std::chrono::duration<double>(now - lastStat).count();
                if (elapsed >= 1.0) {
                    fps.store(static_cast<double>(frames) / elapsed);
                    frames   = 0;
                    lastStat = now;
                }
            }
        } else if (ret == MV_E_NODATA) {
            continue;  // 超时没图，正常
        } else if (ret == MV_E_ABNORMAL_IMAGE) {
            MV_CC_FreeImageBuffer(handle, &out);
            continue;
        } else {
            errorCode.store(static_cast<int>(ret));
            std::cerr << "[hikrobot] 取图线程退出: " << mvErrorText(static_cast<int>(ret)) << '\n';
            break;
        }
    }
    queue.close();
}

bool HikRobot::Impl::open() {
    if (opened) {
        return true;
    }

    MV_CC_DEVICE_INFO_LIST list{};
    mvOk(MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &list), "MV_CC_EnumDevices", true);
    if (list.nDeviceNum == 0) {
        throw std::runtime_error(
            "没有找到海康相机。检查：USB3 线/供电、lsusb 是否能看到 2bdf:xxxx、"
            "MVS 客户端是否占用了相机、当前用户是否有 USB 访问权限");
    }

    const int index = pickDevice(list);
    if (index < 0) {
        throw std::runtime_error("枚举到了设备但没有可用的 USB 相机，请检查 vid_pid 配置");
    }
    printDeviceInfo(*list.pDeviceInfo[index]);

    mvOk(MV_CC_CreateHandle(&handle, list.pDeviceInfo[index]), "MV_CC_CreateHandle", true);
    mvOk(MV_CC_OpenDevice(handle), "MV_CC_OpenDevice", true);

    applyParams();
    mvOk(MV_CC_StartGrabbing(handle), "MV_CC_StartGrabbing", true);

    running.store(true);
    worker = std::thread(&Impl::grabLoop, this);
    opened = true;
    return true;
}

void HikRobot::Impl::close() {
    if (!opened) {
        return;
    }
    running.store(false);
    if (worker.joinable()) {
        worker.join();
    }
    queue.close();

    if (handle != nullptr) {
        MV_CC_StopGrabbing(handle);
        MV_CC_CloseDevice(handle);
        MV_CC_DestroyHandle(handle);
        handle = nullptr;
    }
    opened = false;
}

bool HikRobot::Impl::read(cv::Mat& img, TimePoint& timestamp, int timeout_ms) {
    Frame frame;
    if (!queue.waitPop(frame, timeout_ms)) {
        return false;
    }
    img       = std::move(frame.image);
    timestamp = frame.timestamp;
    return !img.empty();
}

#endif  // HAS_HIKROBOT

/* ============================== 对外接口 ================================= */

HikRobot::HikRobot(const CameraConfig& config) : impl_(std::make_unique<Impl>(config)) {}

HikRobot::~HikRobot() { close(); }

bool HikRobot::open() {
#if defined(HAS_HIKROBOT)
    return impl_->open();
#else
    throw std::runtime_error(
        "本程序编译时没有找到海康 MVS SDK（HAS_HIKROBOT 未定义）。\n"
        "  1) 安装 MVS 后执行 cmake -B build -DMVS_ROOT=/opt/MVS\n"
        "  2) 或把 configs/camera.yaml 的 camera_name 改成 opencv 先用普通摄像头调试");
#endif
}

void HikRobot::close() {
#if defined(HAS_HIKROBOT)
    impl_->close();
#endif
}

bool HikRobot::isOpen() const {
#if defined(HAS_HIKROBOT)
    return impl_->opened;
#else
    return false;
#endif
}

bool HikRobot::read(cv::Mat& img, TimePoint& timestamp, int timeout_ms) {
#if defined(HAS_HIKROBOT)
    return impl_->read(img, timestamp, timeout_ms);
#else
    (void)img;
    (void)timestamp;
    (void)timeout_ms;
    return false;
#endif
}

int HikRobot::width() const {
#if defined(HAS_HIKROBOT)
    return impl_->width.load();
#else
    return 0;
#endif
}

int HikRobot::height() const {
#if defined(HAS_HIKROBOT)
    return impl_->height.load();
#else
    return 0;
#endif
}

double HikRobot::measuredFps() const {
#if defined(HAS_HIKROBOT)
    return impl_->fps.load();
#else
    return 0.0;
#endif
}

int HikRobot::lastError() const {
#if defined(HAS_HIKROBOT)
    return impl_->errorCode.load();
#else
    return 0;
#endif
}

}  // namespace io
