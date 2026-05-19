#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

template <typename T>
class BlockingQueue {
public:
    void push(T item)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(item));
        }
        condition_.notify_one();
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return !queue_.empty(); });

        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable condition_;
};

struct RawFrame {
    int frame_id = -1;
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
};

struct PreprocessedFrame {
    int frame_id = -1;
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
    std::string layout;
};

struct DetectedHand {
    int frame_id = -1;
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
    float confidence = 0.0F;
};

struct KeypointFeature {
    int frame_id = -1;
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
    std::size_t keypoint_count = 0;
};

struct RecognitionResult {
    int frame_id = -1;
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
    std::string text;
};

class SignLanguagePipeline {
public:
    explicit SignLanguagePipeline(int frame_count);
    ~SignLanguagePipeline();

    SignLanguagePipeline(const SignLanguagePipeline&) = delete;
    SignLanguagePipeline& operator=(const SignLanguagePipeline&) = delete;

    void run();

private:
    void start();
    void join();

    void captureLoop();
    void preprocessLoop();
    void handDetectLoop();
    void keypointLoop();
    void sequenceLoop();
    void outputLoop();

    int frame_count_;
    std::atomic<int> completed_frames_;
    std::vector<std::thread> workers_;

    BlockingQueue<RawFrame> raw_queue_;
    BlockingQueue<PreprocessedFrame> preprocess_queue_;
    BlockingQueue<DetectedHand> hand_queue_;
    BlockingQueue<KeypointFeature> keypoint_queue_;
    BlockingQueue<RecognitionResult> result_queue_;
};

#endif
