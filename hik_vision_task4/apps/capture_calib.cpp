/**
 * capture_calib.cpp —— 标定图采集（任务三第一步）
 *
 * 采集时实时检测圆点阵，只有找到整块标定板才允许保存，
 * 避免采回一堆检测不到的废图。文件名按 calib_0001.png 递增。
 *
 * 用法：
 *   ./build/capture_calib --config-path=configs/camera.yaml \
 *        --calibration-config=configs/calibration.yaml \
 *        --out-dir=assets/calibration_imgs --target=20
 *
 * 按键：s 保存  a 自动连拍开关  q 退出
 */

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "io/camera.hpp"
#include "io/cli.hpp"
#include "vision/calib.hpp"

namespace {

struct Options {
    std::string camera_config  = "configs/camera.yaml";
    std::string calib_config   = "configs/calibration.yaml";
    std::string out_dir        = "assets/calibration_imgs";
    std::string prefix         = "calib";
    int         target         = 20;    ///< 目标张数，只用于提示进度
    int         auto_interval  = 0;     ///< 毫秒，0 = 默认关闭自动连拍
    bool        display        = true;
};

void printUsage(const char* exe) {
    std::cout << "用法: " << exe << " [选项]\n"
              << "  --config-path <yaml>          相机配置（默认 configs/camera.yaml）\n"
              << "  --calibration-config <yaml>   标定板配置（默认 configs/calibration.yaml）\n"
              << "  --out-dir <目录>              保存目录（默认 assets/calibration_imgs）\n"
              << "  --prefix <前缀>               文件名前缀（默认 calib）\n"
              << "  --target <N>                  目标张数，仅提示（默认 20）\n"
              << "  --auto <毫秒>                 开启自动连拍，间隔多少毫秒\n"
              << "  --no-display                  不显示（自动连拍/SSH 场景）\n"
              << "按键: s 保存, a 自动连拍开关, q 退出\n";
}

bool parseArgs(int argc, char** argv, Options& opts) {
    const std::vector<std::string> args = cli::expand(argc, argv);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        auto value = [&](const char* name) {
            if (i + 1 >= args.size()) {
                std::cerr << "参数 " << name << " 缺少取值\n";
                std::exit(1);
            }
            return args[++i];
        };
        if (arg == "-h" || arg == "--help") { printUsage(argv[0]); std::exit(0); }
        else if (arg == "--config-path")        opts.camera_config = value("--config-path");
        else if (arg == "--calibration-config") opts.calib_config  = value("--calibration-config");
        else if (arg == "--out-dir")            opts.out_dir       = value("--out-dir");
        else if (arg == "--prefix")             opts.prefix        = value("--prefix");
        else if (arg == "--target")             opts.target        = std::stoi(value("--target"));
        else if (arg == "--auto")               opts.auto_interval = std::stoi(value("--auto"));
        else if (arg == "--no-display")         opts.display       = false;
        else {
            std::cerr << "未知参数: " << arg << '\n';
            return false;
        }
    }
    return true;
}

int nextIndexInDir(const std::string& dir, const std::string& prefix) {
    namespace fs = std::filesystem;
    int maxIndex = 0;
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return 1;
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().stem().string();
        if (name.rfind(prefix + "_", 0) != 0) {
            continue;
        }
        try {
            maxIndex = std::max(maxIndex, std::stoi(name.substr(prefix.size() + 1)));
        } catch (const std::exception&) {
            // 忽略不符合命名规则的文件
        }
    }
    return maxIndex + 1;
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
        opts.display = false;
    }
#endif

    try {
        const calib::Config cfg = calib::Config::fromYaml(opts.calib_config);
        std::cout << "[info] 标定板: " << cfg.pattern.cols << "x" << cfg.pattern.rows
                  << "  圆心间距 " << cfg.pattern.spacing_mm << " mm"
                  << (cfg.pattern.asymmetric ? "  (非对称)" : "  (对称)") << '\n'
                  << "[info] 图纸参数必须和手里的标定板完全一致，否则检测不到\n";

        std::error_code ec;
        std::filesystem::create_directories(opts.out_dir, ec);
        if (ec) {
            std::cerr << "[错误] 无法创建目录 " << opts.out_dir << ": " << ec.message() << '\n';
            return 1;
        }

        int index = nextIndexInDir(opts.out_dir, opts.prefix);
        int saved = 0;
        bool autoCapture = opts.auto_interval > 0;

        io::Camera camera(opts.camera_config);
        if (!camera.open()) {
            std::cerr << "[错误] 相机打开失败\n";
            return 1;
        }
        std::cout << "[info] 相机就绪 " << camera.width() << "x" << camera.height()
                  << "，按 s 保存，a 自动连拍，q 退出\n";

        auto lastAuto = std::chrono::steady_clock::now() - std::chrono::hours(1);
        int  misses   = 0;

        while (true) {
            cv::Mat img;
            io::TimePoint ts;
            if (!camera.read(img, ts, 1000) || img.empty()) {
                if (++misses > 100) {
                    std::cerr << "[错误] 连续取图失败（相机掉线或视频已结束）\n";
                    break;
                }
                continue;
            }
            misses = 0;

            cv::Mat gray;
            cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
            std::vector<cv::Point2f> centers;
            const bool found = calib::detectGrid(gray, cfg.pattern, centers,
                                                 cfg.subpixel, cfg.use_clahe);

            cv::Mat canvas = calib::drawGrid(img, centers, found);
            cv::putText(canvas,
                        "saved " + std::to_string(saved) + " / " + std::to_string(opts.target) +
                            (autoCapture ? "   [AUTO]" : ""),
                        cv::Point(20, canvas.rows - 24), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                        cv::Scalar(0, 255, 255), 2, cv::LINE_AA);

            bool wantSave = false;
            if (opts.display) {
                cv::imshow("capture_calib  (s: save, a: auto, q: quit)", canvas);
                const int key = cv::waitKey(1) & 0xFF;
                if (key == 'q' || key == 27) {
                    break;
                }
                if (key == 's') {
                    wantSave = true;
                }
                if (key == 'a') {
                    autoCapture = !autoCapture;
                    std::cout << "[info] 自动连拍 " << (autoCapture ? "开" : "关") << '\n';
                }
            }

            const auto now = std::chrono::steady_clock::now();
            if (autoCapture && opts.auto_interval > 0 &&
                std::chrono::duration_cast<std::chrono::milliseconds>(now - lastAuto).count() >=
                    opts.auto_interval) {
                wantSave = true;
            }

            if (wantSave) {
                if (!found) {
                    std::cout << "[skip] 当前画面没有检测到完整标定板，未保存\n";
                } else {
                    std::ostringstream name;
                    name << opts.prefix << '_' << std::setw(4) << std::setfill('0') << index
                         << ".png";
                    const std::string path =
                        (std::filesystem::path(opts.out_dir) / name.str()).string();
                    if (cv::imwrite(path, img)) {
                        std::cout << "[save] " << path << '\n';
                        ++index;
                        ++saved;
                    } else {
                        std::cerr << "[错误] 写文件失败: " << path << '\n';
                    }
                }
                lastAuto = now;
            }

            if (!opts.display && !autoCapture && opts.auto_interval == 0) {
                std::cerr << "[提示] 无界面时请用 --auto <毫秒> 自动采集\n";
                break;
            }
        }

        camera.close();
        std::cout << "\n[summary] 本次新增 " << saved << " 张，下一步：\n"
                  << "  ./build/calibrate_camera " << opts.out_dir
                  << " --config-path=" << opts.calib_config << '\n';
        return saved > 0 ? 0 : 1;

    } catch (const std::exception& ex) {
        std::cerr << "[异常] " << ex.what() << '\n';
        return 2;
    }
}
