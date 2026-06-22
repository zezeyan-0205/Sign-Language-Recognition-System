#ifndef MEDIAPIPE_KEYPOINT_EXTRACTOR_HPP
#define MEDIAPIPE_KEYPOINT_EXTRACTOR_HPP

#include <cstdint>
#include <memory>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

struct MediaPipeKeypointResult {
    bool ok = false;
    std::string status;
    std::vector<float> feature;
    std::vector<cv::Point2f> display_points;
};

// C++ MediaPipe 关键点提取接口。当前默认实现为可编译占位，真实实现放在同一封装内补齐。
class MediaPipeKeypointExtractor {
public:
    MediaPipeKeypointExtractor();
    ~MediaPipeKeypointExtractor();

    MediaPipeKeypointExtractor(const MediaPipeKeypointExtractor&) = delete;
    MediaPipeKeypointExtractor& operator=(const MediaPipeKeypointExtractor&) = delete;

    bool load(const std::string& pose_model_path, const std::string& hand_model_path);
    bool isReady() const;
    MediaPipeKeypointResult extract(const cv::Mat& bgr_image, int64_t timestamp_ms);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool ready_ = false;
    std::string status_;
};

#endif
