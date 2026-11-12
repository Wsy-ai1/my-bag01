#pragma once
/**
 * calib.hpp —— 圆点标定板的检测、三维点生成与误差评估
 *
 * 对应讲义 4.2/4.5：findCirclesGrid -> 生成三维点 -> calibrateCamera
 * -> projectPoints -> 平均重投影误差。
 */

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>

#include <string>
#include <vector>

namespace calib {

/** 标定板参数，与 configs/calibration.yaml 对应 */
struct Pattern {
    int    cols        = 10;      ///< 圆点列数
    int    rows        = 7;       ///< 圆点行数
    double spacing_mm  = 25.0;    ///< 相邻圆心实际距离（毫米）
    bool   asymmetric  = false;   ///< true = 非对称圆点阵(如 4x11)

    cv::Size size() const { return cv::Size(cols, rows); }
    int count() const { return cols * rows; }
};

/** 标定流程配置 */
struct Config {
    Pattern     pattern;
    bool        use_clahe   = true;   ///< 光照不均时先做 CLAHE
    bool        subpixel    = true;   ///< 亚像素精化圆心
    int         calib_flags = 0;      ///< 透传给 calibrateCamera
    double      max_reproj_error = 1.0;  ///< 单张图超过这个误差就提示剔除
    std::string output = "configs/camera_intrinsics.yaml";

    static Config fromYaml(const std::string& path);
};

/** findCirclesGrid：找到所有圆心像素坐标 */
bool detectGrid(const cv::Mat& gray,
                const Pattern& pattern,
                std::vector<cv::Point2f>& centers,
                bool subpixel = true,
                bool use_clahe = true);

/** 按行列数与间距生成标定板三维点（Z=0 平面） */
std::vector<cv::Point3f> objectPoints(const Pattern& pattern);

/** 单张图的平均重投影误差（像素） */
double meanReprojectionError(const std::vector<cv::Point3f>& objectPoints,
                             const std::vector<cv::Point2f>& imagePoints,
                             const cv::Mat& cameraMatrix,
                             const cv::Mat& distCoeffs,
                             const cv::Mat& rvec,
                             const cv::Mat& tvec);

/** 画检测结果，便于检查是否找对了 */
cv::Mat drawGrid(const cv::Mat& bgr, const std::vector<cv::Point2f>& centers, bool ok);

}  // namespace calib
