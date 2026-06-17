#include "pipeline/inc/Pipeline.hpp"

#include "pipeline/inc/CameraCapture.hpp"
#include "pipeline/inc/KeypointSequenceBuffer.hpp"
#include "pipeline/inc/RgaProcessor.hpp"
#include "pipeline/inc/TcnRecognizer.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <linux/videodev2.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <sstream>
#include <utility>
#include <vector>

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

std::string fourccToString(std::uint32_t fourcc)
{
    std::string value(4, ' ');
    value[0] = static_cast<char>(fourcc & 0xFF);
    value[1] = static_cast<char>((fourcc >> 8) & 0xFF);
    value[2] = static_cast<char>((fourcc >> 16) & 0xFF);
    value[3] = static_cast<char>((fourcc >> 24) & 0xFF);
    return value;
}

void pushCaptureStop(BlockingQueue<RawFrame>& raw_queue)
{
    RawFrame stop_frame;
    stop_frame.stop = true;
    raw_queue.push(std::move(stop_frame));
}

cv::Rect makeMockHandBox(int frame_id, int image_width, int image_height)
{
    const int box_width = image_width / 3;
    const int box_height = image_height / 2;
    const int travel = image_width / 8;
    const int x = image_width / 2 - box_width / 2 + (frame_id % 60 - 30) * travel / 30;
    const int y = image_height / 2 - box_height / 2;
    return cv::Rect(std::max(0, x), std::max(0, y), box_width, box_height) &
        cv::Rect(0, 0, image_width, image_height);
}

std::vector<cv::Point2f> makeMockKeypoints(const cv::Rect& box)
{
    std::vector<cv::Point2f> points;
    points.reserve(21);
    points.emplace_back(box.x + box.width * 0.50F, box.y + box.height * 0.85F);

    for (int finger = 0; finger < 5; ++finger) {
        const float base_x = box.x + box.width * (0.18F + finger * 0.16F);
        for (int joint = 0; joint < 4; ++joint) {
            const float x = base_x + (joint - 1.5F) * box.width * 0.025F;
            const float y = box.y + box.height * (0.70F - joint * 0.17F);
            points.emplace_back(x, y);
        }
    }

    return points;
}

std::string makeMockMedicalText(int frame_id)
{
    static const std::vector<std::string> texts = {
        "Chief: fever and cough",
        "Chief: headache",
        "Chief: chest tightness",
        "Chief: stomach pain"
    };
    return texts[(frame_id / 45) % texts.size()];
}

std::vector<float> makeMockKeypointFeature(int frame_id)
{
    constexpr int feature_dim = 258;
    std::vector<float> feature(feature_dim, 0.0F);

    // 只用于打通 C++ 序列缓存路径。真实接入时替换为 Pose + Hand 的 258 维逐帧特征。
    const float phase = static_cast<float>(frame_id % 30) / 30.0F;
    feature[11 * 4 + 0] = -0.5F;
    feature[11 * 4 + 1] = 0.0F;
    feature[12 * 4 + 0] = 0.5F;
    feature[12 * 4 + 1] = 0.0F;
    feature[132 + 0] = -0.2F + phase * 0.1F;
    feature[132 + 1] = 0.4F;
    feature[132 + 63 + 0] = 0.2F - phase * 0.1F;
    feature[132 + 63 + 1] = 0.4F;

    return feature;
}

void drawStatusBar(cv::Mat& image, const std::string& text, double fps, long latency_ms)
{
    const int bar_height = 86;
    const cv::Rect bar(0, image.rows - bar_height, image.cols, bar_height);
    cv::Mat overlay = image.clone();
    cv::rectangle(overlay, bar, cv::Scalar(0, 0, 0), cv::FILLED);
    cv::addWeighted(overlay, 0.58, image, 0.42, 0.0, image);

    cv::putText(image, text, cv::Point(24, image.rows - 38), cv::FONT_HERSHEY_SIMPLEX,
                1.05, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);

    std::ostringstream oss;
    oss << "FPS: " << std::fixed << std::setprecision(1) << fps << "  Latency: " << latency_ms << " ms";
    cv::putText(image, oss.str(), cv::Point(24, image.rows - 12), cv::FONT_HERSHEY_SIMPLEX,
                0.62, cv::Scalar(200, 255, 200), 1, cv::LINE_AA);
}

} // namespace

SignLanguagePipeline::SignLanguagePipeline(int frame_count,
                                           std::string camera_device,
                                           std::string tcn_model_path,
                                           std::string label_path)
    : frame_count_(frame_count),
      camera_device_(std::move(camera_device)),
      tcn_model_path_(std::move(tcn_model_path)),
      label_path_(std::move(label_path)),
      completed_frames_(0)
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
    V4L2CameraCapture camera(camera_device_);
    if (!camera.open()) {
        pushCaptureStop(raw_queue_);
        return;
    }

    for (int frame_id = 0; frame_id < frame_count_; ++frame_id) {
        RawFrame frame;
        if (!camera.readFrame(frame_id, frame)) {
            break;
        }
        logStage("capture", frame_id,
                 "camera frame acquired, " + std::to_string(frame.width) + "x" + std::to_string(frame.height) +
                     ", format=" + fourccToString(frame.pixel_format) + ", bytes=" + std::to_string(frame.data.size()));
        raw_queue_.push(std::move(frame));
    }

    // 采集结束后发送停止信号，后续 stage 会逐级把停止信号继续向下传递。
    pushCaptureStop(raw_queue_);
}

void SignLanguagePipeline::preprocessLoop()
{
    RgaProcessor rga_processor;
    bool saved_first_frame = false;

    while (true) {
        RawFrame raw = raw_queue_.pop();
        if (raw.stop) {
            // 收到上游停止信号后，通知下游并退出当前线程。
            PreprocessedFrame stop_frame;
            stop_frame.stop = true;
            preprocess_queue_.push(std::move(stop_frame));
            return;
        }

        PreprocessedFrame frame;
        frame.frame_id = raw.frame_id;
        frame.capture_time = raw.capture_time;

        bool converted = false;
        std::string conversion_path;

        if (raw.pixel_format == V4L2_PIX_FMT_NV12M || raw.pixel_format == V4L2_PIX_FMT_NV12) {
            const std::size_t y_size =
                static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height);
            const std::size_t required_size = y_size + y_size / 2;
            if (raw.data.size() >= required_size) {
                uint8_t* y_plane = raw.data.data();
                uint8_t* uv_plane = raw.data.data() + y_size;
                converted = rga_processor.ImgConvertNM12ToBGR(
                    y_plane, uv_plane, raw.width, raw.height, frame.bgr_image);
                conversion_path = "NM12";
            } else {
                logStage("preprocess", frame.frame_id, "NM12 frame is smaller than width*height*3/2");
            }
        } else if (raw.pixel_format == V4L2_PIX_FMT_YUYV) {
            const std::size_t required_size =
                static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 2;
            if (raw.data.size() >= required_size) {
                converted = rga_processor.ImgConvertYUYVToBGR(
                    raw.data.data(), raw.width, raw.height, frame.bgr_image);
                conversion_path = "YUYV";
            } else {
                logStage("preprocess", frame.frame_id, "YUYV frame is smaller than width*height*2");
            }
        } else {
            logStage("preprocess", frame.frame_id,
                     "unsupported RGA input format " + fourccToString(raw.pixel_format));
        }

        if (converted) {
            frame.layout = "BGR888 640x640";
            logStage("preprocess", frame.frame_id, "RGA " + conversion_path + " -> " + frame.layout);

            if (!saved_first_frame) {
                saved_first_frame = cv::imwrite("rga_first_frame.jpg", frame.bgr_image);
                logStage("preprocess", frame.frame_id,
                         saved_first_frame ? "saved rga_first_frame.jpg" : "failed to save rga_first_frame.jpg");
            }
        } else {
            frame.layout = fourccToString(raw.pixel_format) + " " + std::to_string(raw.width) + "x" +
                std::to_string(raw.height);
            logStage("preprocess", frame.frame_id, "RGA conversion failed, keeping " + frame.layout);
        }

        preprocess_queue_.push(std::move(frame));
    }
}

void SignLanguagePipeline::handDetectLoop()
{
    while (true) {
        PreprocessedFrame input = preprocess_queue_.pop();
        if (input.stop) {
            // 保持停止信号只走正常队列路径，避免下游线程永久阻塞在 pop。
            DetectionResult stop_frame;
            stop_frame.stop = true;
            hand_queue_.push(std::move(stop_frame));
            return;
        }

        simulateWork(8);
        DetectionResult result;
        result.frame_id = input.frame_id;
        result.capture_time = input.capture_time;
        result.bgr_image = input.bgr_image;
        if (!result.bgr_image.empty()) {
            result.hand_box = makeMockHandBox(result.frame_id, result.bgr_image.cols, result.bgr_image.rows);
        }
        logStage("hand-detect", result.frame_id, "mock hand box generated");
        hand_queue_.push(std::move(result));
    }
}

void SignLanguagePipeline::keypointLoop()
{
    while (true) {
        DetectionResult result = hand_queue_.pop();
        if (result.stop) {
            // 逐级传递 stop，保证流水线按顺序自然排空和退出。
            DetectionResult stop_frame;
            stop_frame.stop = true;
            keypoint_queue_.push(std::move(stop_frame));
            return;
        }

        simulateWork(6);
        result.keypoints = makeMockKeypoints(result.hand_box);
        result.keypoint_feature = makeMockKeypointFeature(result.frame_id);
        logStage("keypoints", result.frame_id, "mock 21-point topology and 258-D feature generated");
        keypoint_queue_.push(std::move(result));
    }
}

void SignLanguagePipeline::sequenceLoop()
{
    KeypointSequenceBuffer sequence_buffer(30, 258);
    TcnRecognizer recognizer;
    const bool recognizer_ready =
        !tcn_model_path_.empty() && recognizer.load(tcn_model_path_, label_path_, 30, 258);
    if (recognizer_ready) {
        std::cout << "TCN recognizer enabled: " << tcn_model_path_ << '\n';
    } else {
        std::cout << "TCN recognizer disabled; sequenceLoop keeps using mock text.\n";
    }

    while (true) {
        DetectionResult result = keypoint_queue_.pop();
        if (result.stop) {
            // 通知输出线程结束，整个流水线至此开始收尾。
            DetectionResult stop_frame;
            stop_frame.stop = true;
            result_queue_.push(std::move(stop_frame));
            return;
        }

        if (!result.keypoint_feature.empty() && !sequence_buffer.push(result.keypoint_feature)) {
            logStage("sequence", result.frame_id, "ignored invalid keypoint feature size");
        }

        if (recognizer_ready && sequence_buffer.ready() && sequence_buffer.handValidRate() >= 0.6F) {
            const TcnRecognitionResult tcn_result = recognizer.infer(sequence_buffer.makeInput());
            if (tcn_result.ok) {
                std::ostringstream oss;
                oss << tcn_result.label << " (" << std::fixed << std::setprecision(2)
                    << tcn_result.confidence << ", " << std::setprecision(2)
                    << tcn_result.latency_ms << " ms)";
                result.text_result = oss.str();
                logStage("sequence", result.frame_id, "TCN -> " + result.text_result);
            } else {
                result.text_result = makeMockMedicalText(result.frame_id);
                logStage("sequence", result.frame_id, "TCN failed, fallback -> " + result.text_result);
            }
        } else if (recognizer_ready) {
            std::ostringstream oss;
            oss << "Collecting sequence " << sequence_buffer.size() << "/8";
            result.text_result = oss.str();
            logStage("sequence", result.frame_id, result.text_result);
        } else {
            simulateWork(4);
            result.text_result = makeMockMedicalText(result.frame_id);
            logStage("sequence", result.frame_id, "mock text -> " + result.text_result);
        }
        result_queue_.push(std::move(result));
    }
}

void SignLanguagePipeline::outputLoop()
{
    const std::string window_name = "RK3588 Sign Language Demo";
    cv::namedWindow(window_name, cv::WINDOW_NORMAL);
    cv::resizeWindow(window_name, 960, 960);

    auto fps_time = std::chrono::steady_clock::now();
    int fps_frames = 0;
    double display_fps = 0.0;

    while (true) {
        DetectionResult result = result_queue_.pop();
        if (result.stop) {
            // 输出线程收到 stop 后打印统计信息并退出。
            std::cout << "Pipeline completed " << completed_frames_.load() << " frames.\n";
            cv::destroyWindow(window_name);
            return;
        }

        ++completed_frames_;
        ++fps_frames;
        const auto now = std::chrono::steady_clock::now();
        const auto fps_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - fps_time).count();
        if (fps_elapsed >= 1000) {
            display_fps = fps_frames * 1000.0 / static_cast<double>(fps_elapsed);
            fps_frames = 0;
            fps_time = now;
        }

        if (result.bgr_image.empty()) {
            logStage("output", result.frame_id, "empty image, display skipped");
            continue;
        }

        cv::Mat display = result.bgr_image.clone();
        if (result.hand_box.area() > 0) {
            cv::rectangle(display, result.hand_box, cv::Scalar(0, 255, 0), 3, cv::LINE_AA);
        }
        for (const cv::Point2f& point : result.keypoints) {
            cv::circle(display, cv::Point(cvRound(point.x), cvRound(point.y)), 5,
                       cv::Scalar(0, 0, 255), cv::FILLED, cv::LINE_AA);
        }

        const long latency_ms = elapsedMs(result.capture_time);
        drawStatusBar(display, result.text_result, display_fps, latency_ms);
        cv::imshow(window_name, display);

        const int key = cv::waitKey(1);
        if (key == 27 || key == 'q' || key == 'Q') {
            std::cout << "Display stopped by user.\n";
            cv::destroyWindow(window_name);
            return;
        }

        logStage("output", result.frame_id,
                 "text=\"" + result.text_result + "\", latency=" + std::to_string(latency_ms) + "ms");
    }
}
