/**
 * undistort_compare.cpp —— 去畸变前后对比（任务三最后一项）
 *
 * 用法：
 *   ./build/undistort_compare --intrinsics=configs/camera_intrinsics.yaml \
 *        --input=assets/calibration_imgs/calib_0001.png --output=output
 *
 * 输出：
 *   output/<name>_before.png
 *   output/<name>_after.png
 *   output/<name>_compare.png   （左右拼接，带文字标注）
 */

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "io/cli.hpp"

namespace {

struct Options {
    std::string intrinsics = "configs/camera_intrinsics.yaml";
    std::string input;
    std::string output   = "output";
    double      alpha    = 1.0;   ///< 0 = 裁掉黑边, 1 = 保留全部像素
    double      scale    = 0.5;   ///< 对比图缩放，1.0 = 原尺寸
};

void printUsage(const char* exe) {
    std::cout << "用法: " << exe << " --intrinsics <yaml> --input <图片> [--output 目录]\n"
              << "  --alpha <0~1>   0=裁掉畸变黑边，1=保留全部像素（默认 1）\n"
              << "  --scale <倍数>  对比图缩放比例（默认 0.5）\n";
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
        else if (arg == "--intrinsics") opts.intrinsics = value("--intrinsics");
        else if (arg == "--input")      opts.input      = value("--input");
        else if (arg == "--output")     opts.output     = value("--output");
        else if (arg == "--alpha")      opts.alpha      = std::stod(value("--alpha"));
        else if (arg == "--scale")      opts.scale      = std::stod(value("--scale"));
        else {
            std::cerr << "未知参数: " << arg << '\n';
            return false;
        }
    }
    if (opts.input.empty()) {
        printUsage(argv[0]);
        return false;
    }
    return true;
}

cv::Mat labelImage(const cv::Mat& img, const std::string& text) {
    cv::Mat out = img.clone();
    cv::rectangle(out, cv::Rect(0, 0, out.cols, 44), cv::Scalar(0, 0, 0), cv::FILLED);
    cv::putText(out, text, cv::Point(12, 31), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parseArgs(argc, argv, opts)) {
        return 1;
    }

    try {
        cv::FileStorage fs(opts.intrinsics, cv::FileStorage::READ);
        if (!fs.isOpened()) {
            std::cerr << "[错误] 打不开内参文件: " << opts.intrinsics << '\n';
            return 1;
        }

        cv::Mat K, D;
        fs["camera_matrix"] >> K;
        fs["distortion_coefficients"] >> D;
        if (K.empty() || D.empty()) {
            std::cerr << "[错误] 文件里缺少 camera_matrix / distortion_coefficients\n";
            return 1;
        }

        int width = 0;
        int height = 0;
        fs["image_width"] >> width;
        fs["image_height"] >> height;
        double meanErr = -1.0;
        if (!fs["mean_reprojection_error"].empty()) {
            fs["mean_reprojection_error"] >> meanErr;
        }

        std::cout << "[info] 内参来自 " << opts.intrinsics << '\n';
        std::cout << "  fx=" << std::fixed << std::setprecision(3) << K.at<double>(0, 0)
                  << "  fy=" << K.at<double>(1, 1)
                  << "  cx=" << K.at<double>(0, 2)
                  << "  cy=" << K.at<double>(1, 2) << '\n';
        std::cout << "  k1=" << D.at<double>(0)
                  << "  k2=" << (D.total() > 1 ? D.at<double>(1) : 0.0)
                  << "  p1=" << (D.total() > 2 ? D.at<double>(2) : 0.0)
                  << "  p2=" << (D.total() > 3 ? D.at<double>(3) : 0.0)
                  << "  k3=" << (D.total() > 4 ? D.at<double>(4) : 0.0) << '\n';
        if (meanErr >= 0.0) {
            std::cout << "  标定时平均重投影误差: " << meanErr << " px\n";
        }

        const cv::Mat before = cv::imread(opts.input, cv::IMREAD_COLOR);
        if (before.empty()) {
            std::cerr << "[错误] 读不了图片: " << opts.input << '\n';
            return 1;
        }
        if (width > 0 && height > 0 && (before.cols != width || before.rows != height)) {
            std::cout << "[warn] 图片分辨率 " << before.cols << "x" << before.rows
                      << " 与标定分辨率 " << width << "x" << height
                      << " 不一致，内参不能跨分辨率复用\n";
        }

        const cv::Size size = before.size();
        const cv::Mat newK = cv::getOptimalNewCameraMatrix(K, D, size, opts.alpha, size);

        cv::Mat map1, map2;
        cv::initUndistortRectifyMap(K, D, cv::Mat(), newK, size, CV_16SC2, map1, map2);
        cv::Mat after;
        cv::remap(before, after, map1, map2, cv::INTER_LINEAR);

        std::error_code ec;
        std::filesystem::create_directories(opts.output, ec);
        if (ec) {
            std::cerr << "[错误] 创建输出目录失败: " << ec.message() << '\n';
            return 1;
        }

        const std::string stem = std::filesystem::path(opts.input).stem().string();
        const std::string pBefore =
            (std::filesystem::path(opts.output) / (stem + "_before.png")).string();
        const std::string pAfter =
            (std::filesystem::path(opts.output) / (stem + "_after.png")).string();
        const std::string pCompare =
            (std::filesystem::path(opts.output) / (stem + "_compare.png")).string();

        cv::imwrite(pBefore, before);
        cv::imwrite(pAfter, after);

        // 左右拼接；中间 6px 黑线分隔
        const double scale = std::clamp(opts.scale, 0.1, 1.0);
        cv::Mat left  = labelImage(before, "before (raw)");
        cv::Mat right = labelImage(after,  "after (undistorted)");
        if (scale < 1.0) {
            cv::resize(left,  left,  cv::Size(), scale, scale, cv::INTER_AREA);
            cv::resize(right, right, cv::Size(), scale, scale, cv::INTER_AREA);
        }
        cv::Mat compare(left.rows, left.cols + right.cols + 6, CV_8UC3, cv::Scalar(0, 0, 0));
        left.copyTo(compare(cv::Rect(0, 0, left.cols, left.rows)));
        right.copyTo(compare(cv::Rect(left.cols + 6, 0, right.cols, right.rows)));
        cv::imwrite(pCompare, compare);

        std::cout << "\n[saved]\n  " << pBefore << "\n  " << pAfter << "\n  " << pCompare << '\n'
                  << "检查方法：看画面边缘的直线（门框、桌子边缘）是否变直，而不是看中间\n";
        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "[异常] " << ex.what() << '\n';
        return 2;
    }
}
