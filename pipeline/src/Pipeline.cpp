#include "pipeline/inc/Pipeline.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

// 当前 demo 用 sleep 模拟不同硬件/模型阶段的耗时。
// 后续接入 OpenCV、RGA、RKNN 时，可以把这些占位耗时替换为真实计算。
void simulateWork(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

// 计算一帧从采集到输出的端到端延迟。
long elapsedMs(const std::chrono::steady_clock::time_point& start)
{
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
}

// 统一打印各 stage 的日志，方便观察流水线是否并行推进。
void logStage(const std::string& stage, int frame_id, const std::string& detail)
{
    std::ostringstream oss;
    oss << "[frame " << std::setw(2) << frame_id << "] "
        << std::left << std::setw(12) << stage << " " << detail << '\n';
    std::cout << oss.str();
}

} // namespace

SignLanguagePipeline::SignLanguagePipeline(int frame_count)
    : frame_count_(frame_count), completed_frames_(0)
{
}

SignLanguagePipeline::~SignLanguagePipeline()
{
    join();
}

void SignLanguagePipeline::run()
{
    // run 是对外入口：启动所有 stage，然后等待它们处理完停止信号。
    start();
    join();
}

void SignLanguagePipeline::start()
{
    // 每个 stage 一个线程，便于后续把采集、预处理、NPU 推理和输出解耦。
    workers_.emplace_back(&SignLanguagePipeline::captureLoop, this);
    workers_.emplace_back(&SignLanguagePipeline::preprocessLoop, this);
    workers_.emplace_back(&SignLanguagePipeline::handDetectLoop, this);
    workers_.emplace_back(&SignLanguagePipeline::keypointLoop, this);
    workers_.emplace_back(&SignLanguagePipeline::sequenceLoop, this);
    workers_.emplace_back(&SignLanguagePipeline::outputLoop, this);
}

void SignLanguagePipeline::join()
{
    // 析构和 run 都可能调用 join，因此这里先判断 joinable，保证重复调用安全。
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void SignLanguagePipeline::captureLoop()
{
    for (int frame_id = 0; frame_id < frame_count_; ++frame_id) {
        // 这里暂时模拟  60 FPS 左右的摄像头输入。
        simulateWork(16);
        RawFrame frame;
        frame.frame_id = frame_id;
        frame.capture_time = std::chrono::steady_clock::now();
        logStage("capture", frame_id, "camera frame acquired");
        raw_queue_.push(std::move(frame));
    }

    // 采集结束后发送停止信号，后续 stage 会逐级把停止信号继续向下传递。
    RawFrame stop_frame;
    stop_frame.stop = true;
    raw_queue_.push(std::move(stop_frame));
}

void SignLanguagePipeline::preprocessLoop()
{
    while (true) {
        RawFrame raw = raw_queue_.pop();
        if (raw.stop) {
            // 收到上游停止信号后，通知下游并退出当前线程。
            PreprocessedFrame stop_frame;
            stop_frame.stop = true;
            preprocess_queue_.push(std::move(stop_frame));
            return;
        }

        simulateWork(5);
        PreprocessedFrame frame;
        frame.frame_id = raw.frame_id;
        frame.capture_time = raw.capture_time;
        // 后续这里会替换为 RGA resize/color space conversion 的真实输出格式。
        frame.layout = "RGB888 640x640";
        logStage("preprocess", frame.frame_id, "resize/csc placeholder -> " + frame.layout);
        preprocess_queue_.push(std::move(frame));
    }
}

void SignLanguagePipeline::handDetectLoop()
{
    while (true) {
        PreprocessedFrame input = preprocess_queue_.pop();
        if (input.stop) {
            // 保持停止信号只走正常队列路径，避免下游线程永久阻塞在 pop。
            DetectedHand stop_frame;
            stop_frame.stop = true;
            hand_queue_.push(std::move(stop_frame));
            return;
        }

        simulateWork(8);
        DetectedHand hand;
        hand.frame_id = input.frame_id;
        hand.capture_time = input.capture_time;
        // 这里暂时用固定置信度模拟 YOLO/RKNN 手部检测结果。
        hand.confidence = 0.92F;
        logStage("hand-detect", hand.frame_id, "YOLO/RKNN placeholder, confidence=0.92");
        hand_queue_.push(std::move(hand));
    }
}

void SignLanguagePipeline::keypointLoop()
{
    while (true) {
        DetectedHand hand = hand_queue_.pop();
        if (hand.stop) {
            // 逐级传递 stop，保证流水线按顺序自然排空和退出。
            KeypointFeature stop_frame;
            stop_frame.stop = true;
            keypoint_queue_.push(std::move(stop_frame));
            return;
        }

        simulateWork(6);
        KeypointFeature feature;
        feature.frame_id = hand.frame_id;
        feature.capture_time = hand.capture_time;
        // 手部拓扑模型固定输出 21 个关键点，当前 demo 只保留数量字段。
        feature.keypoint_count = 21;
        logStage("keypoints", feature.frame_id, "21-point topology feature extracted");
        keypoint_queue_.push(std::move(feature));
    }
}

void SignLanguagePipeline::sequenceLoop()
{
    while (true) {
        KeypointFeature feature = keypoint_queue_.pop();
        if (feature.stop) {
            // 通知输出线程结束，整个流水线至此开始收尾。
            RecognitionResult stop_frame;
            stop_frame.stop = true;
            result_queue_.push(std::move(stop_frame));
            return;
        }

        simulateWork(4);
        RecognitionResult result;
        result.frame_id = feature.frame_id;
        result.capture_time = feature.capture_time;
        // 暂时用交替文本模拟 GRU/LSTM 动态手语识别输出。
        result.text = feature.frame_id % 2 == 0 ? "hello" : "thanks";
        logStage("sequence", result.frame_id, "GRU/LSTM placeholder -> " + result.text);
        result_queue_.push(std::move(result));
    }
}

void SignLanguagePipeline::outputLoop()
{
    while (true) {
        RecognitionResult result = result_queue_.pop();
        if (result.stop) {
            // 输出线程收到 stop 后打印统计信息并退出。
            std::cout << "Pipeline completed " << completed_frames_.load() << " frames.\n";
            return;
        }

        ++completed_frames_;
        logStage("output", result.frame_id,
                 "text=\"" + result.text + "\", latency=" + std::to_string(elapsedMs(result.capture_time)) + "ms");
    }
}
