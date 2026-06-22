#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "mediapipe/framework/formats/image.h"
#include "mediapipe/framework/formats/image_frame.h"
#include "mediapipe/tasks/cc/vision/core/running_mode.h"
#include "mediapipe/tasks/cc/vision/hand_landmarker/hand_landmarker.h"
#include "mediapipe/tasks/cc/vision/pose_landmarker/pose_landmarker.h"

namespace hand = mediapipe::tasks::vision::hand_landmarker;
namespace pose = mediapipe::tasks::vision::pose_landmarker;
namespace vision = mediapipe::tasks::vision;

namespace {

constexpr int kPoseLandmarkCount = 33;
constexpr int kHandLandmarkCount = 21;
constexpr int kPoseDim = kPoseLandmarkCount * 4;
constexpr int kHandDim = kHandLandmarkCount * 3;
constexpr int kFeatureDim = kPoseDim + kHandDim * 2;

thread_local std::string g_last_error;

struct SignMediaPipeExtractor {
    std::unique_ptr<pose::PoseLandmarker> pose_landmarker;
    std::unique_ptr<hand::HandLandmarker> hand_landmarker;
};

mediapipe::Image MakeImageFromBgr(const unsigned char* bgr,
                                  int width,
                                  int height,
                                  int stride)
{
    cv::Mat bgr_view(height, width, CV_8UC3, const_cast<unsigned char*>(bgr), stride);
    cv::Mat rgb;
    cv::cvtColor(bgr_view, rgb, cv::COLOR_BGR2RGB);

    auto image_frame = std::make_shared<mediapipe::ImageFrame>(
        mediapipe::ImageFormat::SRGB,
        rgb.cols,
        rgb.rows,
        mediapipe::ImageFrame::kDefaultAlignmentBoundary);

    std::memcpy(
        image_frame->MutablePixelData(),
        rgb.data,
        rgb.total() * rgb.elemSize());

    return mediapipe::Image(image_frame);
}

void FillPoseFeature(const pose::PoseLandmarkerResult& pose_result, float* out_feature)
{
    if (pose_result.pose_landmarks.empty()) {
        return;
    }

    const auto& landmarks = pose_result.pose_landmarks[0].landmarks;
    for (std::size_t i = 0; i < landmarks.size() && i < kPoseLandmarkCount; ++i) {
        out_feature[i * 4 + 0] = landmarks[i].x;
        out_feature[i * 4 + 1] = landmarks[i].y;
        out_feature[i * 4 + 2] = landmarks[i].z;
        out_feature[i * 4 + 3] = landmarks[i].visibility.value_or(0.0F);
    }
}

void FillHandFeature(const hand::HandLandmarkerResult& hand_result,
                     int width,
                     int height,
                     float* out_feature,
                     float* out_display_points,
                     int* out_display_point_count)
{
    int point_count = 0;

    for (std::size_t h = 0; h < hand_result.hand_landmarks.size(); ++h) {
        std::string handedness = "Unknown";
        if (h < hand_result.handedness.size() && !hand_result.handedness[h].categories.empty()) {
            handedness = hand_result.handedness[h].categories[0].category_name.value_or("Unknown");
        }

        int base = -1;
        if (handedness == "Left") {
            base = kPoseDim;
        } else if (handedness == "Right") {
            base = kPoseDim + kHandDim;
        }

        if (base < 0) {
            continue;
        }

        const auto& landmarks = hand_result.hand_landmarks[h].landmarks;
        for (std::size_t i = 0; i < landmarks.size() && i < kHandLandmarkCount; ++i) {
            out_feature[base + i * 3 + 0] = landmarks[i].x;
            out_feature[base + i * 3 + 1] = landmarks[i].y;
            out_feature[base + i * 3 + 2] = landmarks[i].z;

            if (out_display_points && point_count < kHandLandmarkCount * 2) {
                out_display_points[point_count * 2 + 0] = landmarks[i].x * static_cast<float>(width);
                out_display_points[point_count * 2 + 1] = landmarks[i].y * static_cast<float>(height);
            }
            ++point_count;
        }
    }

    if (out_display_point_count) {
        *out_display_point_count = point_count;
    }
}

} // namespace

extern "C" const char* sign_mp_last_error()
{
    return g_last_error.c_str();
}

extern "C" void* sign_mp_create(const char* pose_model_path, const char* hand_model_path)
{
    g_last_error.clear();

    auto extractor = std::make_unique<SignMediaPipeExtractor>();

    auto pose_options = std::make_unique<pose::PoseLandmarkerOptions>();
    pose_options->base_options.model_asset_path = pose_model_path;
    pose_options->running_mode = vision::core::RunningMode::IMAGE;
    pose_options->num_poses = 1;
    pose_options->min_pose_detection_confidence = 0.5F;
    pose_options->min_pose_presence_confidence = 0.5F;
    pose_options->min_tracking_confidence = 0.5F;

    auto hand_options = std::make_unique<hand::HandLandmarkerOptions>();
    hand_options->base_options.model_asset_path = hand_model_path;
    hand_options->running_mode = vision::core::RunningMode::IMAGE;
    hand_options->num_hands = 2;
    hand_options->min_hand_detection_confidence = 0.5F;
    hand_options->min_hand_presence_confidence = 0.5F;
    hand_options->min_tracking_confidence = 0.5F;

    auto pose_status = pose::PoseLandmarker::Create(std::move(pose_options));
    if (!pose_status.ok()) {
        g_last_error = "Create PoseLandmarker failed: " + pose_status.status().ToString();
        return nullptr;
    }

    auto hand_status = hand::HandLandmarker::Create(std::move(hand_options));
    if (!hand_status.ok()) {
        g_last_error = "Create HandLandmarker failed: " + hand_status.status().ToString();
        return nullptr;
    }

    extractor->pose_landmarker = std::move(pose_status.value());
    extractor->hand_landmarker = std::move(hand_status.value());
    return extractor.release();
}

extern "C" int sign_mp_extract(void* handle,
                               const unsigned char* bgr,
                               int width,
                               int height,
                               int stride,
                               float* out_feature_258,
                               float* out_display_points,
                               int* out_display_point_count)
{
    g_last_error.clear();

    if (!handle || !bgr || !out_feature_258 || width <= 0 || height <= 0 || stride <= 0) {
        g_last_error = "Invalid sign_mp_extract arguments";
        return -1;
    }

    auto* extractor = static_cast<SignMediaPipeExtractor*>(handle);
    std::fill(out_feature_258, out_feature_258 + kFeatureDim, 0.0F);
    if (out_display_point_count) {
        *out_display_point_count = 0;
    }

    mediapipe::Image image = MakeImageFromBgr(bgr, width, height, stride);

    auto pose_result = extractor->pose_landmarker->Detect(image);
    if (!pose_result.ok()) {
        g_last_error = "Pose detect failed: " + pose_result.status().ToString();
        return -2;
    }

    auto hand_result = extractor->hand_landmarker->Detect(image);
    if (!hand_result.ok()) {
        g_last_error = "Hand detect failed: " + hand_result.status().ToString();
        return -3;
    }

    FillPoseFeature(pose_result.value(), out_feature_258);
    FillHandFeature(
        hand_result.value(),
        width,
        height,
        out_feature_258,
        out_display_points,
        out_display_point_count);

    return 0;
}

extern "C" void sign_mp_destroy(void* handle)
{
    delete static_cast<SignMediaPipeExtractor*>(handle);
}
