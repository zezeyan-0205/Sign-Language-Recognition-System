#include "pipeline/inc/KeypointFeatureUtils.hpp"

#include <cmath>

namespace keypoint_feature {
namespace {

float squared(float value)
{
    return value * value;
}

bool isZero3(const float* values)
{
    return values[0] == 0.0F && values[1] == 0.0F && values[2] == 0.0F;
}

bool handAllZero(const float* values)
{
    for (int i = 0; i < kHandDim; ++i) {
        if (values[i] != 0.0F) {
            return false;
        }
    }
    return true;
}

void normalizeXyz(float* values, int landmark_count, const float* center, float scale, int stride)
{
    for (int i = 0; i < landmark_count; ++i) {
        float* landmark = values + i * stride;
        landmark[0] = (landmark[0] - center[0]) / scale;
        landmark[1] = (landmark[1] - center[1]) / scale;
        landmark[2] = (landmark[2] - center[2]) / scale;
    }
}

} // namespace

bool normalizeSequenceInPlace(std::vector<float>& sequence, int sequence_length, int feature_dim)
{
    if (sequence_length <= 0 || feature_dim != kRawFeatureDim ||
        static_cast<int>(sequence.size()) != sequence_length * feature_dim) {
        return false;
    }

    for (int t = 0; t < sequence_length; ++t) {
        float* frame = sequence.data() + t * feature_dim;
        float* pose = frame;
        float* left_hand = frame + kPoseDim;
        float* right_hand = frame + kPoseDim + kHandDim;

        const float* left_shoulder = pose + 11 * 4;
        const float* right_shoulder = pose + 12 * 4;
        if (isZero3(left_shoulder) || isZero3(right_shoulder)) {
            continue;
        }

        const float center[3] = {
            (left_shoulder[0] + right_shoulder[0]) * 0.5F,
            (left_shoulder[1] + right_shoulder[1]) * 0.5F,
            (left_shoulder[2] + right_shoulder[2]) * 0.5F,
        };

        const float shoulder_width = std::sqrt(
            squared(left_shoulder[0] - right_shoulder[0]) +
            squared(left_shoulder[1] - right_shoulder[1]));
        if (shoulder_width < 1e-6F) {
            continue;
        }

        normalizeXyz(pose, kPoseLandmarkCount, center, shoulder_width, 4);
        if (!handAllZero(left_hand)) {
            normalizeXyz(left_hand, kHandLandmarkCount, center, shoulder_width, 3);
        }
        if (!handAllZero(right_hand)) {
            normalizeXyz(right_hand, kHandLandmarkCount, center, shoulder_width, 3);
        }
    }

    return true;
}

} // namespace keypoint_feature
