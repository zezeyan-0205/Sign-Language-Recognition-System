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

// 手部检测阶段输出的结果，后续会承载手部 ROI 和检测置信度。
struct DetectedHand {
    int frame_id = -1;
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
    float confidence = 0.0F;
};

// 关键点阶段输出的拓扑特征，目标是表示 21 个手部骨骼点。
struct KeypointFeature {
    int frame_id = -1;
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
    std::size_t keypoint_count = 0;
};

// 时序模型阶段输出的识别文本。
struct RecognitionResult {
    int frame_id = -1;
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
    std::string text;
};

#endif
