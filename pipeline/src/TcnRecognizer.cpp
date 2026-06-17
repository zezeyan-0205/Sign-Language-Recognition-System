#include "pipeline/inc/TcnRecognizer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef HAVE_RKNN_RUNTIME
#include <rknn_api.h>
#endif

namespace {

std::vector<unsigned char> readBinaryFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

std::vector<std::string> loadLabelList(const std::string& path)
{
    std::ifstream file(path);
    if (!file) {
        return {};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    std::vector<std::string> labels;
    bool in_string = false;
    bool escaped = false;
    std::string current;
    for (char ch : content) {
        if (!in_string) {
            if (ch == '"') {
                in_string = true;
                current.clear();
            }
            continue;
        }

        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            labels.push_back(current);
            in_string = false;
            continue;
        }
        current.push_back(ch);
    }

    return labels;
}

std::vector<float> softmax(const float* logits, int count)
{
    std::vector<float> probabilities(count, 0.0F);
    if (count <= 0) {
        return probabilities;
    }

    float max_value = logits[0];
    for (int i = 1; i < count; ++i) {
        max_value = std::max(max_value, logits[i]);
    }

    float sum = 0.0F;
    for (int i = 0; i < count; ++i) {
        probabilities[i] = std::exp(logits[i] - max_value);
        sum += probabilities[i];
    }
    if (sum <= 0.0F) {
        return probabilities;
    }
    for (float& value : probabilities) {
        value /= sum;
    }
    return probabilities;
}

} // namespace

TcnRecognizer::~TcnRecognizer()
{
    release();
}

bool TcnRecognizer::load(const std::string& model_path,
                         const std::string& label_path,
                         int sequence_length,
                         int feature_dim)
{
    release();
    sequence_length_ = sequence_length;
    feature_dim_ = feature_dim;
    labels_ = loadLabelList(label_path);
    if (labels_.empty()) {
        std::cerr << "Failed to load labels from " << label_path << '\n';
        return false;
    }
    output_count_ = static_cast<int>(labels_.size());

#ifndef HAVE_RKNN_RUNTIME
    std::cerr << "RKNN runtime headers/libraries were not found at build time; "
              << "TCN recognizer will stay disabled.\n";
    (void)model_path;
    return false;
#else
    model_data_ = readBinaryFile(model_path);
    if (model_data_.empty()) {
        std::cerr << "Failed to load RKNN model from " << model_path << '\n';
        return false;
    }

    const int ret = rknn_init(&context_, model_data_.data(), model_data_.size(), 0, nullptr);
    if (ret != RKNN_SUCC) {
        std::cerr << "rknn_init failed, ret=" << ret << '\n';
        context_ = 0;
        return false;
    }

    rknn_input_output_num io_num;
    std::memset(&io_num, 0, sizeof(io_num));
    if (rknn_query(context_, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num)) == RKNN_SUCC) {
        std::cout << "TCN RKNN loaded: inputs=" << io_num.n_input
                  << ", outputs=" << io_num.n_output << '\n';
    }

    ready_ = true;
    return true;
#endif
}

bool TcnRecognizer::isReady() const
{
    return ready_;
}

TcnRecognitionResult TcnRecognizer::infer(const std::vector<float>& sequence)
{
    TcnRecognitionResult result;
    const std::size_t expected_size =
        static_cast<std::size_t>(sequence_length_) * static_cast<std::size_t>(feature_dim_);
    if (!ready_ || sequence.size() != expected_size) {
        return result;
    }

#ifndef HAVE_RKNN_RUNTIME
    return result;
#else
    rknn_input input;
    std::memset(&input, 0, sizeof(input));
    input.index = 0;
    input.type = RKNN_TENSOR_FLOAT32;
    input.fmt = RKNN_TENSOR_NHWC;
    input.size = static_cast<uint32_t>(sequence.size() * sizeof(float));
    input.buf = const_cast<float*>(sequence.data());

    const auto start = std::chrono::steady_clock::now();
    int ret = rknn_inputs_set(context_, 1, &input);
    if (ret != RKNN_SUCC) {
        std::cerr << "rknn_inputs_set failed, ret=" << ret << '\n';
        return result;
    }

    ret = rknn_run(context_, nullptr);
    if (ret != RKNN_SUCC) {
        std::cerr << "rknn_run failed, ret=" << ret << '\n';
        return result;
    }

    rknn_output output;
    std::memset(&output, 0, sizeof(output));
    output.want_float = 1;
    ret = rknn_outputs_get(context_, 1, &output, nullptr);
    if (ret != RKNN_SUCC) {
        std::cerr << "rknn_outputs_get failed, ret=" << ret << '\n';
        return result;
    }

    const auto end = std::chrono::steady_clock::now();
    result.latency_ms =
        std::chrono::duration<float, std::milli>(end - start).count();

    const int count = std::min(output_count_, static_cast<int>(output.size / sizeof(float)));
    const float* logits = static_cast<const float*>(output.buf);
    result.logits.assign(logits, logits + count);
    result.probabilities = softmax(logits, count);

    if (!result.probabilities.empty()) {
        const auto best_it = std::max_element(result.probabilities.begin(), result.probabilities.end());
        result.class_index = static_cast<int>(std::distance(result.probabilities.begin(), best_it));
        result.confidence = *best_it;
        if (result.class_index >= 0 && result.class_index < static_cast<int>(labels_.size())) {
            result.label = labels_[result.class_index];
        }
        result.ok = true;
    }

    rknn_outputs_release(context_, 1, &output);
    return result;
#endif
}

void TcnRecognizer::release()
{
#ifdef HAVE_RKNN_RUNTIME
    if (context_ != 0) {
        rknn_destroy(context_);
        context_ = 0;
    }
#endif
    ready_ = false;
}
