cmake_minimum_required(VERSION 3.16)
project(hik_vision_task4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui videoio)
find_package(Threads REQUIRED)

message(STATUS "OpenCV ${OpenCV_VERSION}: ${OpenCV_DIR}")

# ---------------------------------------------------------------------------
# 海康 MVS SDK（可选）
#   默认安装路径 /opt/MVS，可用 -DMVS_ROOT=/path/to/MVS 覆盖
#   找不到 SDK 时自动退化为 OpenCV 后端（笔记本摄像头 / 视频文件）
# ---------------------------------------------------------------------------
option(USE_HIKROBOT "编译海康 MVS 后端" ON)
set(MVS_ROOT "/opt/MVS" CACHE PATH "MVS 安装根目录")

set(MVS_FOUND OFF)
if(USE_HIKROBOT)
  find_path(MVS_INCLUDE_DIR
    NAMES MvCameraControl.h
    PATHS ${MVS_ROOT}/include ${MVS_ROOT}/include/MVS
    NO_DEFAULT_PATH)
  find_library(MVS_LIBRARY
    NAMES MvCameraControl
    PATHS ${MVS_ROOT}/lib/64 ${MVS_ROOT}/lib/64/Release ${MVS_ROOT}/lib
    NO_DEFAULT_PATH)
  if(MVS_INCLUDE_DIR AND MVS_LIBRARY)
    set(MVS_FOUND ON)
  endif()
endif()

if(MVS_FOUND)
  message(STATUS "HikRobot MVS: ${MVS_LIBRARY} (headers: ${MVS_INCLUDE_DIR})")
else()
  message(WARNING
    "未找到海康 MVS SDK（MvCameraControl.h / libMvCameraControl.so）。\n"
    "  将只编译 OpenCV 后端，camera_name 只能填 opencv / video。\n"
    "  安装 MVS 后用 -DMVS_ROOT=/opt/MVS 重新 cmake 即可启用 hikrobot 后端。")
endif()

# ---------------------------------------------------------------------------
# 公共库：相机抽象 + 两种后端
# ---------------------------------------------------------------------------
add_library(vision_io STATIC
  src/io/camera.cpp
  src/io/opencv_camera.cpp
  src/io/hikrobot/hikrobot.cpp
  src/calib/calib.cpp
)

target_include_directories(vision_io PUBLIC include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(vision_io PUBLIC ${OpenCV_LIBS} Threads::Threads)

if(MVS_FOUND)
  target_include_directories(vision_io PUBLIC ${MVS_INCLUDE_DIR})
  target_link_libraries(vision_io PUBLIC ${MVS_LIBRARY})
  target_compile_definitions(vision_io PUBLIC HAS_HIKROBOT=1)
endif()



# ---------------------------------------------------------------------------
# 可执行程序
# ---------------------------------------------------------------------------
add_executable(camera_test      apps/camera_test.cpp)
add_executable(capture_calib    apps/capture_calib.cpp)
add_executable(calibrate_camera apps/calibrate_camera.cpp)
add_executable(undistort_compare apps/undistort_compare.cpp)

foreach(app camera_test capture_calib calibrate_camera undistort_compare)
  target_link_libraries(${app} PRIVATE vision_io)
  if(MVS_FOUND)
    # 运行时优先从 MVS 目录找 libMvCameraControl.so，省得配 LD_LIBRARY_PATH
    set_target_properties(${app} PROPERTIES BUILD_RPATH "${MVS_ROOT}/lib/64")
  endif()
endforeach()

# 让 build/ 目录里也能直接找到配置文件（可选，方便 ctrl+c 复制运行）
file(COPY ${CMAKE_SOURCE_DIR}/configs DESTINATION ${CMAKE_BINARY_DIR})
