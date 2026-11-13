#pragma once
/**
 * cli.hpp —— 命令行参数预处理
 *
 * 同时支持两种写法：
 *   --config-path configs/camera.yaml
 *   --config-path=configs/camera.yaml      （讲义里的写法）
 */

#include <string>
#include <vector>

namespace cli {

inline std::vector<std::string> expand(int argc, char** argv) {
    std::vector<std::string> out;
    if (argc > 1) {
        out.reserve(static_cast<std::size_t>(argc - 1));
    }
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--", 0) == 0) {
            const std::size_t eq = arg.find('=');
            if (eq != std::string::npos && eq > 2) {
                out.push_back(arg.substr(0, eq));
                out.push_back(arg.substr(eq + 1));
                continue;
            }
        }
        out.push_back(std::move(arg));
    }
    return out;
}

}  // namespace cli
