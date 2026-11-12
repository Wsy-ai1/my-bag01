/**
 * camera_test.cpp —— 最小取图程序（任务二）
 *
 * 功能：
 *   - 连续取图并显示
 *   - q / ESC 退出，s 保存当前原图（默认存到 assets/calibration_imgs）
 *   - 叠加显示实时帧率，终端每秒打印一次实际帧率
 *   - 支持无显示器环境（SSH）：自动切换成只统计帧率
 *
 * 用法：
 *   ./build/camera_test --config-path=configs/camera.yaml --display
 *   ./build/camera_test --frames 100            # 跑 100 帧就退出
 *   ./build/camera_test --save-every 1.0        # 每秒自动存一张
 */

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "io/camera.hpp"
#include "io/cli.hpp"

namespace {

struct Options {
    std::string config_path = "configs/camera.yaml";
    std::string save_dir    = "assets/calibration_imgs";
    bool        display     = true;
    int         frames      = 0;     ///< 0 = 一直跑
    double      save_every  = 0.0;   ///< 秒，0 = 不自动保存
    int         warmup      = 5;     ///< 预热帧数（不显示、不计入 fps）
};

void printUsage(const char* exe) {
    std::cout << "用法: " << exe << " [选项]\n"
              << "  --config-path <yaml>   相机配置，默认 configs/camera.yaml\n"
              << "  --display              显示窗口（默认开）\n"
              << "  --no-display           不显示窗口（SSH 下自动生效）\n"
              << "  --frames <N>           跑 N 帧后退出，0 = 无限\n"
              << "  --save-every <秒>      每隔多少秒自动存一张原图，0 = 关闭\n"
              << "  --save-dir <目录>      保存目录，默认 assets/calibration_imgs\n"
              << "  --warmup <N>           预热帧数，默认 5\n"
              << "  -h, --help             显示本帮助\n";
}

bool parseArgs(int argc, char** argv, Options& opts) {
    const std::vector<std::string> args = cli::expand(argc, argv);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        auto value = [&](const char* name) -> std::string {
            if (i + 1 >= args.size()) {
                std::cerr << "参数 " << name << " 缺少取值\n";
                std::exit(1);
            }
            return args[++i];
        };

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        } else if (arg == "--config-path") {
            opts.config_path = value("--config-path");
        } else if (arg == "--display") {
            opts.display = true;
        } else if (arg == "--no-display") {
            opts.display = false;
        } else if (arg == "--frames") {
            opts.frames = std::stoi(value("--frames"));
        } else if (arg == "--save-every") {
            opts.save_every = std::stod(value("--save-every"));
        } else if (arg == "--save-dir") {
            opts.save_dir = value("--save-dir");
        } else if (arg == "--warmup") {
            opts.warmup = std::stoi(value("--warmup"));
        } else {
            std::cerr << "未知参数: " << arg << "\n";
            return false;
        }
    }
    return true;
}

std::string nowStamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) % 1000;

    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S") << '_' << std::setw(3) << std::setfill('0')
        << ms.count();
    return oss.str();
}

std::string saveFrame(const cv::Mat& img, const std::string& dir, const std::string& prefix) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        std::cerr << "[save] 创建目录失败: " << dir << " (" << ec.message() << ")\n";
        return {};
    }
    const std::string path = (std::filesystem::path(dir) / (prefix + nowStamp() + ".png")).string();
    if (!cv::imwrite(path, img)) {
        std::cerr << "[save] 写文件失败: " << path << "\n";
        return {};
    }
    return path;
}

void drawOverlay(cv::Mat& img, double fps, int frameIndex, const std::string& backend) {
    const std::string line1 = "FPS " + cv::format("%.1f", fps);
    const std::string line2 = "frame " + std::to_string(frameIndex) + "  " + backend;

    cv::rectangle(img, cv::Rect(0, 0, img.cols, 74), cv::Scalar(0, 0, 0), cv::FILLED);
    cv::putText(img, line1, cv::Point(12, 32), cv::FONT_HERSHEY_SIMPLEX, 0.9,
                cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
    cv::putText(img, line2, cv::Point(12, 62), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                cv::Scalar(200, 200, 200), 1, cv::LINE_AA);
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parseArgs(argc, argv, opts)) {
        printUsage(argv[0]);
        return 1;
    }

#if !defined(_WIN32)
    if (std::getenv("DISPLAY") == nullptr && std::getenv("WAYLAND_DISPLAY") == nullptr) {
        if (opts.display) {
            std::cout << "[info] 没有检测到图形界面，自动切换为 headless 模式\n";
        }
        opts.display = false;
    }
#endif

    try {
        io::Camera camera(opts.config_path);
        std::cout << "[info] 配置: " << camera.config().toStr() << '\n';
        std::cout << "[info] 后端: " << camera.backend() << '\n';

        if (!camera.open()) {
            std::cerr << "[错误] 相机打开失败\n";
            return 1;
        }
        std::cout << "[info] 已打开，分辨率 " << camera.width() << "x" << camera.height() << '\n'
                  << "[info] 按键：q 退出，s 保存当前原图\n";

        int    frameIndex = 0;
        int    savedCount = 0;
        int    missCount  = 0;
        double fps        = 0.0;

        const auto runStart   = std::chrono::steady_clock::now();
        auto fpsWindowStart   = runStart;
        int  fpsWindowCount = 0;
        auto lastSave       = std::chrono::steady_clock::now();

        while (true) {
            cv::Mat img;
            io::TimePoint ts;
            if (!camera.read(img, ts, 1000) || img.empty()) {
                if (++missCount > 100) {
                    std::cerr << "[错误] 连续 " << missCount << " 次取图失败，退出\n";
                    break;
                }
                continue;
            }
            missCount = 0;
            ++frameIndex;
            ++fpsWindowCount;

            // 每秒统计一次实际帧率
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - fpsWindowStart).count();
            if (elapsed >= 1.0) {
                fps = fpsWindowCount / elapsed;
                fpsWindowStart = now;
                fpsWindowCount = 0;
                if (frameIndex > opts.warmup) {
                    std::cout << "[fps] " << std::fixed << std::setprecision(1) << fps
                              << " 帧/秒  分辨率 " << img.cols << "x" << img.rows << '\n';
                }
            }

            // 自动保存
            if (opts.save_every > 0.0 &&
                std::chrono::duration<double>(now - lastSave).count() >= opts.save_every) {
                if (!saveFrame(img, opts.save_dir, "cap_").empty()) {
                    ++savedCount;
                }
                lastSave = now;
            }

            if (opts.display) {
                cv::Mat canvas = img.clone();
                drawOverlay(canvas, fps, frameIndex, camera.backend());
                cv::imshow("camera_test  (q: quit, s: save)", canvas);
                const int key = cv::waitKey(1) & 0xFF;
                if (key == 'q' || key == 27) {
                    break;
                }
                if (key == 's') {
                    const std::string path = saveFrame(img, opts.save_dir, "cap_");
                    if (!path.empty()) {
                        std::cout << "[save] " << path << '\n';
                        ++savedCount;
                    }
                }
            }

            if (opts.frames > 0 && frameIndex >= opts.frames) {
                break;
            }
        }

        const double runSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count();
        const double avgFps = runSeconds > 0.0 ? frameIndex / runSeconds : 0.0;
        std::cout << "\n[summary] 共取 " << frameIndex << " 帧，用时 " << std::fixed
                  << std::setprecision(2) << runSeconds << " s，平均 " << std::setprecision(1)
                  << avgFps << " fps（最近一秒 " << fps << " fps），保存 " << savedCount
                  << " 张\n";
        camera.close();
        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "[异常] " << ex.what() << '\n';
        return 2;
    }
}
