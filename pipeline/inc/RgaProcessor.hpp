#ifndef RGA_PROCESSOR_HPP
#define RGA_PROCESSOR_HPP

#include <opencv2/core.hpp>
#include <rga/RgaApi.h>
#include <rga/rga.h>

#include <cstdint>

// RGA 图像转换模块：把 V4L2 多平面 NM12/NV12M 帧转换并缩放为 640x640 BGR Mat。
class RgaProcessor {
public:
    bool ImgConvertNM12ToBGR(uint8_t* plane0_y_ptr,
                             uint8_t* plane1_uv_ptr,
                             int src_w,
                             int src_h,
                             cv::Mat& dst_bgr_640);
};

#endif
