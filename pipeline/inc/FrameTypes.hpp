#ifndef FRAME_TYPES_HPP
#define FRAME_TYPES_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

// 摄像头采集阶段产生的原始帧。
struct RawFrame {
    int frame_id = -1;
    // stop 是流水线内部的结束信号，用于通知下游线程退出。
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
    int width = 0;
    int height = 0;
    std::uint32_t pixel_format = 0;
    std::vector<std::uint8_t> data;
};

// 图像预处理后的帧，后续会替换为 OpenCV/RGA 输出的真实图像数据。
struct PreprocessedFrame {
    int frame_id = -1;
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
    std::string layout;
    cv::Mat bgr_image;
};

// Mock 算法链路统一结果：检测框、21 点骨骼、文本结果和可视化图像逐级填充。
struct DetectionResult {
    int frame_id = -1;
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
    cv::Mat bgr_image;
    cv::Rect hand_box;
    std::vector<cv::Point2f> keypoints;
    std::vector<float> keypoint_feature;
    std::string text_result;
};

#endif
