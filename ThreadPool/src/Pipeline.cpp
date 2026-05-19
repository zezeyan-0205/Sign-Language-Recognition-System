#include "Pipeline.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

void simulateWork(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

long elapsedMs(const std::chrono::steady_clock::time_point& start)
{
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
}

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
    start();
    join();
}

void SignLanguagePipeline::start()
{
    workers_.emplace_back(&SignLanguagePipeline::captureLoop, this);
    workers_.emplace_back(&SignLanguagePipeline::preprocessLoop, this);
    workers_.emplace_back(&SignLanguagePipeline::handDetectLoop, this);
    workers_.emplace_back(&SignLanguagePipeline::keypointLoop, this);
    workers_.emplace_back(&SignLanguagePipeline::sequenceLoop, this);
    workers_.emplace_back(&SignLanguagePipeline::outputLoop, this);
}

void SignLanguagePipeline::join()
{
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void SignLanguagePipeline::captureLoop()
{
    for (int frame_id = 0; frame_id < frame_count_; ++frame_id) {
        simulateWork(16);
        RawFrame frame;
        frame.frame_id = frame_id;
        frame.capture_time = std::chrono::steady_clock::now();
        logStage("capture", frame_id, "camera frame acquired");
        raw_queue_.push(std::move(frame));
    }

    RawFrame stop_frame;
    stop_frame.stop = true;
    raw_queue_.push(std::move(stop_frame));
}

void SignLanguagePipeline::preprocessLoop()
{
    while (true) {
        RawFrame raw = raw_queue_.pop();
        if (raw.stop) {
            PreprocessedFrame stop_frame;
            stop_frame.stop = true;
            preprocess_queue_.push(std::move(stop_frame));
            return;
        }

        simulateWork(5);
        PreprocessedFrame frame;
        frame.frame_id = raw.frame_id;
        frame.capture_time = raw.capture_time;
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
            DetectedHand stop_frame;
            stop_frame.stop = true;
            hand_queue_.push(std::move(stop_frame));
            return;
        }

        simulateWork(8);
        DetectedHand hand;
        hand.frame_id = input.frame_id;
        hand.capture_time = input.capture_time;
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
            KeypointFeature stop_frame;
            stop_frame.stop = true;
            keypoint_queue_.push(std::move(stop_frame));
            return;
        }

        simulateWork(6);
        KeypointFeature feature;
        feature.frame_id = hand.frame_id;
        feature.capture_time = hand.capture_time;
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
            RecognitionResult stop_frame;
            stop_frame.stop = true;
            result_queue_.push(std::move(stop_frame));
            return;
        }

        simulateWork(4);
        RecognitionResult result;
        result.frame_id = feature.frame_id;
        result.capture_time = feature.capture_time;
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
            std::cout << "Pipeline completed " << completed_frames_.load() << " frames.\n";
            return;
        }

        ++completed_frames_;
        logStage("output", result.frame_id,
                 "text=\"" + result.text + "\", latency=" + std::to_string(elapsedMs(result.capture_time)) + "ms");
    }
}
