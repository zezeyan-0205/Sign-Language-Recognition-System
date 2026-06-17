#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include "pipeline/inc/BlockingQueue.hpp"
#include "pipeline/inc/FrameTypes.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

// 端侧手语识别流水线骨架：每个 stage 独立运行在线程中，通过阻塞队列串联。
class SignLanguagePipeline {
public:
    explicit SignLanguagePipeline(int frame_count,
                                  std::string camera_device = "/dev/video0",
                                  std::string tcn_model_path = "",
                                  std::string label_path = "label_map.json");
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
    std::string camera_device_;
    std::string tcn_model_path_;
    std::string label_path_;
    std::atomic<int> completed_frames_;
    std::vector<std::thread> workers_;

    // 相邻 stage 之间的消息队列：上游 push，下游 pop。
    BlockingQueue<RawFrame> raw_queue_;
    BlockingQueue<PreprocessedFrame> preprocess_queue_;
    BlockingQueue<DetectionResult> hand_queue_;
    BlockingQueue<DetectionResult> keypoint_queue_;
    BlockingQueue<DetectionResult> result_queue_;
};

#endif
