#ifndef TCN_RECOGNIZER_HPP
#define TCN_RECOGNIZER_HPP

#include <string>
#include <vector>

#ifdef HAVE_RKNN_RUNTIME
#include <rknn_api.h>
#endif

struct TcnRecognitionResult {
    bool ok = false;
    int class_index = -1;
    float confidence = 0.0F;
    float latency_ms = 0.0F;
    std::string label;
    std::vector<float> logits;
    std::vector<float> probabilities;
};

// TCN RKNN 时序识别模块：输入固定为 [1, 30, feature_dim] float32。
class TcnRecognizer {
public:
    TcnRecognizer() = default;
    ~TcnRecognizer();

    TcnRecognizer(const TcnRecognizer&) = delete;
    TcnRecognizer& operator=(const TcnRecognizer&) = delete;

    bool load(const std::string& model_path,
              const std::string& label_path,
              int sequence_length = 30,
              int feature_dim = 258);
    bool isReady() const;
    TcnRecognitionResult infer(const std::vector<float>& sequence);

private:
    void release();

    int sequence_length_ = 30;
    int feature_dim_ = 258;
    int output_count_ = 0;
    bool ready_ = false;
    std::vector<std::string> labels_;
    std::vector<unsigned char> model_data_;

#ifdef HAVE_RKNN_RUNTIME
    rknn_context context_ = 0;
#endif
};

#endif
