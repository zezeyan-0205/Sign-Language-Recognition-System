#include "pipeline/inc/RgaProcessor.hpp"

#include <cstring>
#include <iostream>
#include <vector>

bool RgaProcessor::ImgConvertNM12ToBGR(uint8_t* plane0_y_ptr,
                                       uint8_t* plane1_uv_ptr,
                                       int src_w,
                                       int src_h,
                                       cv::Mat& dst_bgr_640)
{
    if (plane0_y_ptr == nullptr || plane1_uv_ptr == nullptr || src_w <= 0 || src_h <= 0) {
        std::cerr << "Invalid NM12 input for RGA conversion.\n";
        return false;
    }

    if (dst_bgr_640.empty() || dst_bgr_640.rows != 640 || dst_bgr_640.cols != 640 ||
        dst_bgr_640.type() != CV_8UC3 || !dst_bgr_640.isContinuous()) {
        dst_bgr_640.create(640, 640, CV_8UC3);
    }

    rga_info_t src;
    rga_info_t dst;
    std::memset(&src, 0, sizeof(src));
    std::memset(&dst, 0, sizeof(dst));

    // 当前板端 rga_info_t 没有 uvAddr 字段，因此先拼成标准连续 NV12: Y + UV。
    const std::size_t y_size = static_cast<std::size_t>(src_w) * static_cast<std::size_t>(src_h);
    const std::size_t uv_size = y_size / 2;
    std::vector<uint8_t> contiguous_nv12(y_size + uv_size);
    std::memcpy(contiguous_nv12.data(), plane0_y_ptr, y_size);
    std::memcpy(contiguous_nv12.data() + y_size, plane1_uv_ptr, uv_size);

    src.mmuFlag = 1;
    src.virAddr = contiguous_nv12.data();
    rga_set_rect(&src.rect, 0, 0, src_w, src_h, src_w, src_h, RK_FORMAT_YCbCr_420_SP);

    dst.mmuFlag = 1;
    dst.virAddr = dst_bgr_640.data;
    rga_set_rect(&dst.rect, 0, 0, 640, 640, 640, 640, RK_FORMAT_BGR_888);

    const int ret = c_RkRgaBlit(&src, &dst, nullptr);
    if (ret != 0) {
        std::cerr << "c_RkRgaBlit NM12 -> BGR failed, ret=" << ret << '\n';
        return false;
    }

    return true;
}
