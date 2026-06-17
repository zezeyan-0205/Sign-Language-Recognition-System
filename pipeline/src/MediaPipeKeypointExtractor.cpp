#include "pipeline/inc/MediaPipeKeypointExtractor.hpp"

#include "pipeline/inc/KeypointFeatureUtils.hpp"

#include <cstdint>
#include <string>

MediaPipeKeypointExtractor::~MediaPipeKeypointExtractor() = default;

bool MediaPipeKeypointExtractor::load(const std::string& pose_model_path,
                                      const std::string& hand_model_path)
{
    (void)pose_model_path;
    (void)hand_model_path;

#ifdef HAVE_MEDIAPIPE_TASKS
    // TODO: 初始化 C++ MediaPipe Tasks PoseLandmarker 和 HandLandmarker。
    // 保持接口稳定后，只需要在这里补齐真实 graph/task 调用，不再改 pipeline。
    status_ = "MediaPipe C++ support is declared but extractor implementation is pending";
    ready_ = false;
#else
    status_ = "MediaPipe C++ support is not enabled at build time";
    ready_ = false;
#endif

    return ready_;
}

bool MediaPipeKeypointExtractor::isReady() const
{
    return ready_;
}

MediaPipeKeypointResult MediaPipeKeypointExtractor::extract(const cv::Mat& bgr_image,
                                                            int64_t timestamp_ms)
{
    (void)bgr_image;
    (void)timestamp_ms;

    MediaPipeKeypointResult result;
    result.status = status_.empty() ? "MediaPipe extractor is not initialized" : status_;
    result.feature.assign(keypoint_feature::kRawFeatureDim, 0.0F);
    return result;
}
