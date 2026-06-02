#ifndef CAMERA_CAPTURE_HPP
#define CAMERA_CAPTURE_HPP

#include "pipeline/inc/FrameTypes.hpp"

#include <memory>
#include <string>

// V4L2 摄像头采集封装：兼容 UVC single-plane 和 RKISP MIPI multi-plane 节点。
class V4L2CameraCapture {
public:
    explicit V4L2CameraCapture(std::string device_path);
    ~V4L2CameraCapture();

    V4L2CameraCapture(const V4L2CameraCapture&) = delete;
    V4L2CameraCapture& operator=(const V4L2CameraCapture&) = delete;

    bool open();
    bool readFrame(int frame_id, RawFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
