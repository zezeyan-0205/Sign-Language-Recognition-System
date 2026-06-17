#ifndef KEYPOINT_SEQUENCE_BUFFER_HPP
#define KEYPOINT_SEQUENCE_BUFFER_HPP

#include <cstddef>
#include <vector>

// 将逐帧关键点特征缓存并重采样成 TCN 固定输入 [seq_len, feature_dim]。
class KeypointSequenceBuffer {
public:
    explicit KeypointSequenceBuffer(int target_length = 30,
                                    int feature_dim = 258,
                                    std::size_t max_raw_frames = 30,
                                    std::size_t min_ready_frames = 8);

    void clear();
    bool push(const std::vector<float>& frame_feature);
    bool ready() const;
    std::vector<float> makeInput() const;
    float handValidRate() const;
    std::size_t size() const;

private:
    bool hasValidHand(const std::vector<float>& frame_feature) const;

    int target_length_;
    int feature_dim_;
    std::size_t max_raw_frames_;
    std::size_t min_ready_frames_;
    std::vector<std::vector<float>> frames_;
};

#endif
