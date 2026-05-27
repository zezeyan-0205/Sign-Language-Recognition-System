#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include "pipeline/inc/BlockingQueue.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

// 摄像头采集阶段产生的原始帧。
struct RawFrame {
    int frame_id = -1;
    // stop 是流水线内部的结束信号，用于通知下游线程退出。
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
};

// 图像预处理后的帧，后续会替换为 OpenCV/RGA 输出的真实图像数据。
struct PreprocessedFrame {
    int frame_id = -1;
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
    std::string layout;
};

// 手部检测阶段输出的结果，后续会承载手部 ROI 和检测置信度。
struct DetectedHand {
    int frame_id = -1;
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
    float confidence = 0.0F;
};

// 关键点阶段输出的拓扑特征，目标是表示 21 个手部骨骼点。
struct KeypointFeature {
    int frame_id = -1;
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
    std::size_t keypoint_count = 0;
};

// 时序模型阶段输出的识别文本。
struct RecognitionResult {
    int frame_id = -1;
    bool stop = false;
    std::chrono::steady_clock::time_point capture_time;
    std::string text;
};

// 端侧手语识别流水线骨架：每个 stage 独立运行在线程中，通过阻塞队列串联。
class SignLanguagePipeline {
public:
    explicit SignLanguagePipeline(int frame_count);
    ~SignLanguagePipeline();

    // 流水线拥有线程资源，不允许拷贝，避免多个对象重复 join 同一组线程。
    SignLanguagePipeline(const SignLanguagePipeline&) = delete;
    SignLanguagePipeline& operator=(const SignLanguagePipeline&) = delete;

    // 启动全部 stage，并阻塞直到所有 stage 正常退出。
    void run();

private:
    void start();
    void join();

    // 各阶段线程主循环，命名与算法流水线一一对应。
    void captureLoop();
    void preprocessLoop();
    void handDetectLoop();
    void keypointLoop();
    void sequenceLoop();
    void outputLoop();

    int frame_count_;
    std::atomic<int> completed_frames_;
    std::vector<std::thread> workers_;

    // 相邻 stage 之间的消息队列：上游 push，下游 pop。
    BlockingQueue<RawFrame> raw_queue_;
    BlockingQueue<PreprocessedFrame> preprocess_queue_;
    BlockingQueue<DetectedHand> hand_queue_;
    BlockingQueue<KeypointFeature> keypoint_queue_;
    BlockingQueue<RecognitionResult> result_queue_;
};

#endif


