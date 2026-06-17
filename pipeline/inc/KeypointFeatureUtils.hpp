#ifndef KEYPOINT_FEATURE_UTILS_HPP
#define KEYPOINT_FEATURE_UTILS_HPP

#include <vector>

namespace keypoint_feature {

constexpr int kSequenceLength = 30;
constexpr int kPoseLandmarkCount = 33;
constexpr int kHandLandmarkCount = 21;
constexpr int kPoseDim = kPoseLandmarkCount * 4;
constexpr int kHandDim = kHandLandmarkCount * 3;
constexpr int kRawFeatureDim = kPoseDim + kHandDim * 2;

bool normalizeSequenceInPlace(std::vector<float>& sequence,
                              int sequence_length = kSequenceLength,
                              int feature_dim = kRawFeatureDim);

} // namespace keypoint_feature

#endif
