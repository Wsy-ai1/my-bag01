/**
 * calib.cpp —— 圆点标定板检测与误差计算
 */

#include "vision/calib.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

namespace calib {
namespace {

template <typename T>
void readIf(const cv::FileStorage& fs, const char* key, T& value) {
    const cv::FileNode node = fs[key];
    if (!node.empty()) {
        node >> value;
    }
}

}  // namespace

Config Config::fromYaml(const std::string& path) {
    cv::FileStorage fs(path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        throw std::runtime_error("打不开标定配置文件: " + path);
    }

    Config cfg;
    readIf(fs, "pattern_cols",      cfg.pattern.cols);
    readIf(fs, "pattern_rows",      cfg.pattern.rows);
    readIf(fs, "center_distance_mm", cfg.pattern.spacing_mm);

    std::string type = "symmetric";
    readIf(fs, "pattern_type", type);
    if (type == "asymmetric") {
        cfg.pattern.asymmetric = true;
    }

    int subpixel = 1;
    int clahe    = 1;
    readIf(fs, "subpixel", subpixel);
    readIf(fs, "clahe",    clahe);
    cfg.subpixel = subpixel != 0;
    cfg.use_clahe = clahe != 0;

    readIf(fs, "flags",               cfg.calib_flags);
    readIf(fs, "max_reproj_error",    cfg.max_reproj_error);
    readIf(fs, "output",              cfg.output);

    if (cfg.pattern.cols < 3 || cfg.pattern.rows < 3) {
        throw std::runtime_error("pattern_cols / pattern_rows 至少为 3（当前 " +
                                 std::to_string(cfg.pattern.cols) + "x" +
                                 std::to_string(cfg.pattern.rows) + "）");
    }
    if (cfg.pattern.spacing_mm <= 0.0) {
        throw std::runtime_error("center_distance_mm 必须 > 0");
    }
    return cfg;
}

bool detectGrid(const cv::Mat& gray,
                const Pattern& pattern,
                std::vector<cv::Point2f>& centers,
                bool subpixel,
                bool use_clahe) {
    centers.clear();
    if (gray.empty()) {
        return false;
    }

    cv::Mat img = gray;
    if (img.channels() == 3) {
        cv::cvtColor(img, img, cv::COLOR_BGR2GRAY);
    }

    cv::Mat work;
    if (use_clahe) {
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
        clahe->apply(img, work);
    } else {
        work = img;
    }

    // 黑点白底：blobColor=0 找暗色连通域
    cv::SimpleBlobDetector::Params params;
    params.filterByArea       = true;
    params.minArea            = 20.0;
    params.maxArea            = 1e6;
    params.filterByCircularity = true;
    params.minCircularity     = 0.55f;
    params.filterByConvexity  = true;
    params.minConvexity       = 0.80f;
    params.filterByInertia    = true;
    params.minInertiaRatio    = 0.30f;
    params.blobColor          = 0;

    const auto detector = cv::SimpleBlobDetector::create(params);

    int flags = pattern.asymmetric ? cv::CALIB_CB_ASYMMETRIC_GRID : cv::CALIB_CB_SYMMETRIC_GRID;
    bool found = cv::findCirclesGrid(work, pattern.size(), centers,
                                     flags | cv::CALIB_CB_CLUSTERING, detector);
    if (!found) {
        // 有些标定板对 clustering 反而敏感，退化再试一次
        found = cv::findCirclesGrid(work, pattern.size(), centers, flags, detector);
    }
    if (!found) {
        return false;
    }

    if (subpixel) {
        cv::cornerSubPix(img, centers, cv::Size(5, 5), cv::Size(-1, -1),
                         cv::TermCriteria(cv::TermCriteria::EPS | cv::TermCriteria::MAX_ITER,
                                          30, 0.01));
    }
    return true;
}

std::vector<cv::Point3f> objectPoints(const Pattern& pattern) {
    std::vector<cv::Point3f> points;
    points.reserve(static_cast<std::size_t>(pattern.count()));

    // 原点放在标定板左上角第一个圆点，Z=0
    for (int r = 0; r < pattern.rows; ++r) {
        for (int c = 0; c < pattern.cols; ++c) {
            const float x = static_cast<float>(
                (pattern.asymmetric ? 2 * c + (r % 2) : c) * pattern.spacing_mm);
            const float y = static_cast<float>(r * pattern.spacing_mm);
            points.emplace_back(x, y, 0.0f);
        }
    }
    return points;
}

double meanReprojectionError(const std::vector<cv::Point3f>& objectPoints,
                             const std::vector<cv::Point2f>& imagePoints,
                             const cv::Mat& cameraMatrix,
                             const cv::Mat& distCoeffs,
                             const cv::Mat& rvec,
                             const cv::Mat& tvec) {
    if (objectPoints.empty() || objectPoints.size() != imagePoints.size()) {
        return -1.0;
    }

    std::vector<cv::Point2f> projected;
    cv::projectPoints(objectPoints, rvec, tvec, cameraMatrix, distCoeffs, projected);

    double sum = 0.0;
    for (std::size_t i = 0; i < projected.size(); ++i) {
        const cv::Point2f d = projected[i] - imagePoints[i];
        sum += std::sqrt(static_cast<double>(d.dot(d)));
    }
    return sum / static_cast<double>(projected.size());
}

cv::Mat drawGrid(const cv::Mat& bgr, const std::vector<cv::Point2f>& centers, bool ok) {
    cv::Mat canvas = bgr.clone();
    if (canvas.channels() == 1) {
        cv::cvtColor(canvas, canvas, cv::COLOR_GRAY2BGR);
    }

    for (std::size_t i = 0; i < centers.size(); ++i) {
        cv::circle(canvas, centers[i], 5, ok ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255), 2,
                   cv::LINE_AA);
        if (i == 0) {
            cv::circle(canvas, centers[i], 9, cv::Scalar(255, 0, 0), 2, cv::LINE_AA);
        }
    }
    cv::putText(canvas, ok ? "grid: OK" : "grid: NOT FOUND", cv::Point(20, 40),
                cv::FONT_HERSHEY_SIMPLEX, 1.0, ok ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255),
                2, cv::LINE_AA);
    return canvas;
}

}  // namespace calib
