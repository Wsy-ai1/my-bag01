/**
 * calibrate_camera.cpp —— 相机内参标定（任务三）
 *
 * 流程：读图 -> findCirclesGrid -> 生成标定板三维点 -> calibrateCamera
 *      -> 每张图 projectPoints 算重投影误差 -> 保存 YAML
 *
 * 用法：
 *   ./build/calibrate_camera assets/calibration_imgs \
 *        --config-path=configs/calibration.yaml
 *
 * 可选：--output=configs/camera_intrinsics.yaml  --show   --debug-dir=output/detections
 */

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "io/cli.hpp"
#include "vision/calib.hpp"

namespace {

namespace fs = std::filesystem;

struct Options {
    std::string images_dir;
    std::string calib_config = "configs/calibration.yaml";
    std::string output;
    std::string debug_dir;
    int         cols       = 0;     ///< 0 = 用配置文件
    int         rows       = 0;
    double      spacing    = 0.0;
    int         asymmetric = -1;    ///< -1 = 用配置文件
    bool        show       = false;
};

void printUsage(const char* exe) {
    std::cout << "用法: " << exe << " <标定图片目录> [选项]\n"
              << "  --config-path <yaml>   标定板配置，默认 configs/calibration.yaml\n"
              << "  --output <yaml>        输出内参文件（默认用配置里的 output）\n"
              << "  --cols <N> --rows <N>  覆盖行列数\n"
              << "  --spacing <mm>         覆盖圆心间距\n"
              << "  --asymmetric           覆盖为非对称圆点阵\n"
              << "  --show                 逐张显示检测结果（按任意键继续）\n"
              << "  --debug-dir <目录>     把检测结果图另存一份，便于排查\n";
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
        else if (arg == "--config-path") opts.calib_config = value("--config-path");
        else if (arg == "--output")      opts.output       = value("--output");
        else if (arg == "--cols")        opts.cols         = std::stoi(value("--cols"));
        else if (arg == "--rows")        opts.rows         = std::stoi(value("--rows"));
        else if (arg == "--spacing")     opts.spacing      = std::stod(value("--spacing"));
        else if (arg == "--asymmetric")  opts.asymmetric   = 1;
        else if (arg == "--show")        opts.show         = true;
        else if (arg == "--debug-dir")   opts.debug_dir    = value("--debug-dir");
        else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "未知参数: " << arg << '\n';
            return false;
        } else {
            opts.images_dir = arg;   // 位置参数：图片目录
        }
    }
    if (opts.images_dir.empty()) {
        printUsage(argv[0]);
        return false;
    }
    return true;
}

bool isImageFile(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
           ext == ".tif" || ext == ".tiff";
}

std::vector<fs::path> listImages(const std::string& dir) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        throw std::runtime_error("不是目录: " + dir);
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && isImageFile(entry.path())) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

void printMat(const cv::Mat& m, const std::string& name) {
    std::cout << name << ":\n";
    for (int r = 0; r < m.rows; ++r) {
        std::cout << "  [";
        for (int c = 0; c < m.cols; ++c) {
            std::cout << std::setw(12) << std::fixed << std::setprecision(4) << m.at<double>(r, c);
            if (c + 1 != m.cols) std::cout << ',';
        }
        std::cout << " ]\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parseArgs(argc, argv, opts)) {
        return 1;
    }

    try {
        calib::Config cfg = calib::Config::fromYaml(opts.calib_config);
        if (opts.cols > 0)        cfg.pattern.cols = opts.cols;
        if (opts.rows > 0)        cfg.pattern.rows = opts.rows;
        if (opts.spacing > 0.0)   cfg.pattern.spacing_mm = opts.spacing;
        if (opts.asymmetric >= 0) cfg.pattern.asymmetric = opts.asymmetric == 1;
        const std::string output = opts.output.empty() ? cfg.output : opts.output;

        std::cout << "[info] 标定板 " << cfg.pattern.cols << "x" << cfg.pattern.rows
                  << "  间距 " << cfg.pattern.spacing_mm << " mm"
                  << (cfg.pattern.asymmetric ? "  非对称" : "  对称") << '\n';

        const std::vector<fs::path> files = listImages(opts.images_dir);
        if (files.empty()) {
            std::cerr << "[错误] 目录里没有图片: " << opts.images_dir << '\n';
            return 1;
        }
        std::cout << "[info] 找到 " << files.size() << " 张图片\n";

        if (!opts.debug_dir.empty()) {
            std::error_code ec;
            fs::create_directories(opts.debug_dir, ec);
        }

        const std::vector<cv::Point3f> board = calib::objectPoints(cfg.pattern);
        std::vector<std::vector<cv::Point3f>> objectPoints;
        std::vector<std::vector<cv::Point2f>> imagePoints;
        std::vector<std::string>              usedNames;
        cv::Size imageSize;

        int skipped = 0;
        for (const auto& file : files) {
            const cv::Mat img = cv::imread(file.string(), cv::IMREAD_COLOR);
            if (img.empty()) {
                std::cerr << "[skip] 读不了: " << file << '\n';
                ++skipped;
                continue;
            }
            if (imageSize.area() == 0) {
                imageSize = img.size();
            } else if (img.size() != imageSize) {
                std::cerr << "[skip] 分辨率与首张不一致: " << file << '\n';
                ++skipped;
                continue;
            }

            cv::Mat gray;
            cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);

            std::vector<cv::Point2f> centers;
            const bool found = calib::detectGrid(gray, cfg.pattern, centers,
                                                 cfg.subpixel, cfg.use_clahe);

            if (found) {
                objectPoints.push_back(board);
                imagePoints.push_back(centers);
                usedNames.push_back(file.filename().string());
                std::cout << "[ok]   " << file.filename().string() << "  找到 "
                          << centers.size() << " 个圆心\n";
            } else {
                std::cerr << "[skip] " << file.filename().string()
                          << "  没找到完整圆点阵（行列数/间距是否与标定板一致？）\n";
                ++skipped;
            }

            if (!opts.debug_dir.empty()) {
                cv::imwrite((fs::path(opts.debug_dir) / file.filename()).string(),
                            calib::drawGrid(img, centers, found));
            }
            if (opts.show) {
                cv::imshow("calibrate_camera", calib::drawGrid(img, centers, found));
                cv::waitKey(30);
            }
        }
        if (opts.show) {
            cv::destroyAllWindows();
        }

        std::cout << "\n[info] 成功 " << imagePoints.size() << " 张，跳过 " << skipped << " 张\n";
        if (imagePoints.size() < 8) {
            std::cerr << "[错误] 有效图片太少（" << imagePoints.size()
                      << "），至少要 8 张，建议 15~25 张且角度/距离有变化\n";
            return 1;
        }

        cv::Mat K, D;
        std::vector<cv::Mat> rvecs, tvecs;
        const double rms = cv::calibrateCamera(objectPoints, imagePoints, imageSize, K, D,
                                               rvecs, tvecs, cfg.calib_flags);

        std::vector<double> perView;
        perView.reserve(imagePoints.size());
        for (std::size_t i = 0; i < imagePoints.size(); ++i) {
            perView.push_back(calib::meanReprojectionError(objectPoints[i], imagePoints[i],
                                                           K, D, rvecs[i], tvecs[i]));
        }
        const double meanErr =
            std::accumulate(perView.begin(), perView.end(), 0.0) / perView.size();
        const double maxErr = *std::max_element(perView.begin(), perView.end());

        std::cout << "\n========== 标定结果 ==========\n";
        printMat(K, "camera_matrix");
        std::cout << "distortion_coefficients (k1 k2 p1 p2 k3 ...):\n  [";
        for (int i = 0; i < static_cast<int>(D.total()); ++i) {
            std::cout << std::fixed << std::setprecision(6) << D.at<double>(i);
            if (i + 1 != static_cast<int>(D.total())) std::cout << ", ";
        }
        std::cout << "]\n";
        std::cout << "rms (calibrateCamera)   : " << std::fixed << std::setprecision(4) << rms
                  << " px\n"
                  << "mean reprojection error : " << meanErr << " px\n"
                  << "max  reprojection error : " << maxErr << " px\n";

        std::cout << "\n每张图的重投影误差：\n";
        for (std::size_t i = 0; i < perView.size(); ++i) {
            std::cout << "  " << std::setw(28) << usedNames[i] << "  " << std::fixed
                      << std::setprecision(4) << perView[i] << " px"
                      << (perView[i] > cfg.max_reproj_error ? "   <-- 偏大，考虑剔除" : "") << '\n';
        }

        if (meanErr > cfg.max_reproj_error) {
            std::cout << "\n[warn] 平均误差 " << meanErr << " px 超过阈值 "
                      << cfg.max_reproj_error
                      << " px：多半是标定板不够平整、图片太少或存在模糊/反光\n";
        }

        cv::FileStorage fsOut(output, cv::FileStorage::WRITE);
        if (!fsOut.isOpened()) {
            std::cerr << "[错误] 无法写入 " << output << '\n';
            return 1;
        }
        fsOut << "image_width"  << imageSize.width;
        fsOut << "image_height" << imageSize.height;
        fsOut << "camera_matrix" << K;
        fsOut << "distortion_coefficients" << D;
        fsOut << "rms_reprojection_error" << rms;
        fsOut << "mean_reprojection_error" << meanErr;
        fsOut << "max_reprojection_error" << maxErr;
        fsOut << "pattern_cols" << cfg.pattern.cols;
        fsOut << "pattern_rows" << cfg.pattern.rows;
        fsOut << "center_distance_mm" << cfg.pattern.spacing_mm;
        fsOut << "pattern_type" << (cfg.pattern.asymmetric ? "asymmetric" : "symmetric");
        fsOut << "images" << "[";
        for (const auto& name : usedNames) {
            fsOut << name;
        }
        fsOut << "]";
        fsOut << "per_view_errors" << "[";
        for (double e : perView) {
            fsOut << e;
        }
        fsOut << "]";
        fsOut.release();

        std::cout << "\n[saved] " << output << '\n'
                  << "下一步：\n"
                  << "  ./build/undistort_compare --intrinsics " << output
                  << " --input " << files.front().string() << " --output output\n";
        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "[异常] " << ex.what() << '\n';
        return 2;
    }
}
