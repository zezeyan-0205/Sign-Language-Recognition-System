#include "pipeline/inc/KeypointSequenceBuffer.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kPoseDim = 33 * 4;
constexpr int kHandDim = 21 * 3;

} // namespace

KeypointSequenceBuffer::KeypointSequenceBuffer(int target_length,
                                               int feature_dim,
                                               std::size_t max_raw_frames,
                                               std::size_t min_ready_frames)
    : target_length_(target_length),
      feature_dim_(feature_dim),
      max_raw_frames_(max_raw_frames),
      min_ready_frames_(min_ready_frames)
{
}

void KeypointSequenceBuffer::clear()
{
    frames_.clear();
}

bool KeypointSequenceBuffer::push(const std::vector<float>& frame_feature)
{
    if (static_cast<int>(frame_feature.size()) != feature_dim_) {
        return false;
    }

    frames_.push_back(frame_feature);
    if (frames_.size() > max_raw_frames_) {
        frames_.erase(frames_.begin());
    }
    return true;
}

bool KeypointSequenceBuffer::ready() const
{
    return frames_.size() >= min_ready_frames_;
}

std::vector<float> KeypointSequenceBuffer::makeInput() const
{
    std::vector<float> output(
        static_cast<std::size_t>(target_length_) * static_cast<std::size_t>(feature_dim_),
        0.0F);

    if (frames_.empty() || target_length_ <= 0 || feature_dim_ <= 0) {
        return output;
    }

    if (frames_.size() == 1 || target_length_ == 1) {
        for (int t = 0; t < target_length_; ++t) {
            std::copy(frames_[0].begin(), frames_[0].end(), output.begin() + t * feature_dim_);
        }
        return output;
    }

    const float old_last = static_cast<float>(frames_.size() - 1);
    for (int t = 0; t < target_length_; ++t) {
        const float pos = old_last * static_cast<float>(t) / static_cast<float>(target_length_ - 1);
        const std::size_t left = static_cast<std::size_t>(std::floor(pos));
        const std::size_t right = std::min(left + 1, frames_.size() - 1);
        const float ratio = pos - static_cast<float>(left);

        float* dst = output.data() + static_cast<std::size_t>(t) * static_cast<std::size_t>(feature_dim_);
        for (int d = 0; d < feature_dim_; ++d) {
            dst[d] = frames_[left][d] * (1.0F - ratio) + frames_[right][d] * ratio;
        }
    }

    return output;
}

float KeypointSequenceBuffer::handValidRate() const
{
    if (frames_.empty()) {
        return 0.0F;
    }

    int valid_count = 0;
    for (const std::vector<float>& frame : frames_) {
        if (hasValidHand(frame)) {
            ++valid_count;
        }
    }

    return static_cast<float>(valid_count) / static_cast<float>(frames_.size());
}

std::size_t KeypointSequenceBuffer::size() const
{
    return frames_.size();
}

bool KeypointSequenceBuffer::hasValidHand(const std::vector<float>& frame_feature) const
{
    if (feature_dim_ < kPoseDim + kHandDim * 2 ||
        static_cast<int>(frame_feature.size()) < kPoseDim + kHandDim * 2) {
        return true;
    }

    const int begin = kPoseDim;
    const int end = kPoseDim + kHandDim * 2;
    for (int i = begin; i < end; ++i) {
        if (std::fabs(frame_feature[i]) > 1e-6F) {
            return true;
        }
    }

    return false;
}
