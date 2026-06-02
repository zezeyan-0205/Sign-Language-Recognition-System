#include "pipeline/inc/CameraCapture.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

struct PlaneMapping {
    void* start = nullptr;
    std::size_t length = 0;
};

struct MappedBuffer {
    std::vector<PlaneMapping> planes;
};

int xioctl(int fd, unsigned long request, void* arg)
{
    int ret = 0;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
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

} // namespace

struct V4L2CameraCapture::Impl {
    explicit Impl(std::string path) : device_path(std::move(path)) {}

    ~Impl()
    {
        close();
    }

    bool open()
    {
        fd = ::open(device_path.c_str(), O_RDWR);
        if (fd < 0) {
            std::cerr << "Failed to open " << device_path << ": " << std::strerror(errno) << '\n';
            return false;
        }

        if (!queryCapability() || !configureFormat() || !requestBuffers() || !startStream()) {
            close();
            return false;
        }

        return true;
    }

    bool readFrame(int frame_id, RawFrame& frame)
    {
        v4l2_buffer buffer {};
        v4l2_plane planes[VIDEO_MAX_PLANES] {};
        buffer.type = buffer_type;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (use_multi_plane) {
            buffer.length = VIDEO_MAX_PLANES;
            buffer.m.planes = planes;
        }

        if (xioctl(fd, VIDIOC_DQBUF, &buffer) < 0) {
            std::cerr << "VIDIOC_DQBUF failed at frame " << frame_id << ": " << std::strerror(errno) << '\n';
            return false;
        }

        frame = RawFrame {};
        frame.frame_id = frame_id;
        frame.capture_time = std::chrono::steady_clock::now();
        frame.width = use_multi_plane ? static_cast<int>(format.fmt.pix_mp.width) : static_cast<int>(format.fmt.pix.width);
        frame.height =
            use_multi_plane ? static_cast<int>(format.fmt.pix_mp.height) : static_cast<int>(format.fmt.pix.height);
        frame.pixel_format = use_multi_plane ? format.fmt.pix_mp.pixelformat : format.fmt.pix.pixelformat;

        const MappedBuffer& mapped_buffer = buffers[buffer.index];
        if (use_multi_plane) {
            for (unsigned int plane_index = 0; plane_index < buffer.length; ++plane_index) {
                const auto* plane_start = static_cast<const std::uint8_t*>(mapped_buffer.planes[plane_index].start);
                frame.data.insert(frame.data.end(), plane_start, plane_start + planes[plane_index].bytesused);
            }
        } else {
            const auto* frame_start = static_cast<const std::uint8_t*>(mapped_buffer.planes[0].start);
            frame.data.assign(frame_start, frame_start + buffer.bytesused);
        }

        if (use_multi_plane) {
            buffer.length = static_cast<unsigned int>(mapped_buffer.planes.size());
            buffer.m.planes = planes;
        }
        if (xioctl(fd, VIDIOC_QBUF, &buffer) < 0) {
            std::cerr << "VIDIOC_QBUF failed after frame " << frame_id << ": " << std::strerror(errno) << '\n';
            return false;
        }

        return true;
    }

    void close()
    {
        if (fd >= 0 && streaming) {
            v4l2_buf_type type = buffer_type;
            if (xioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
                std::cerr << "VIDIOC_STREAMOFF failed: " << std::strerror(errno) << '\n';
            }
            streaming = false;
        }

        for (MappedBuffer& buffer : buffers) {
            for (PlaneMapping& plane : buffer.planes) {
                if (plane.start != nullptr && plane.start != MAP_FAILED) {
                    munmap(plane.start, plane.length);
                    plane.start = nullptr;
                }
            }
        }
        buffers.clear();

        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    bool queryCapability()
    {
        v4l2_capability capability {};
        if (xioctl(fd, VIDIOC_QUERYCAP, &capability) < 0) {
            std::cerr << "VIDIOC_QUERYCAP failed: " << std::strerror(errno) << '\n';
            return false;
        }

        const std::uint32_t device_caps =
            (capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0 ? capability.device_caps : capability.capabilities;
        const bool supports_single_plane = (device_caps & V4L2_CAP_VIDEO_CAPTURE) != 0;
        const bool supports_multi_plane = (device_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
        if ((device_caps & V4L2_CAP_STREAMING) == 0 || (!supports_single_plane && !supports_multi_plane)) {
            std::cerr << device_path << " does not support V4L2 streaming capture.\n";
            return false;
        }

        // RKISP/MIPI 摄像头通常使用 multi-plane，UVC 摄像头常见为 single-plane。
        use_multi_plane = supports_multi_plane;
        buffer_type = use_multi_plane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        return true;
    }

    bool configureFormat()
    {
        format = v4l2_format {};
        format.type = buffer_type;
        if (use_multi_plane) {
            format.fmt.pix_mp.width = 640;
            format.fmt.pix_mp.height = 480;
            format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M;
            format.fmt.pix_mp.field = V4L2_FIELD_ANY;
        } else {
            format.fmt.pix.width = 640;
            format.fmt.pix.height = 480;
            format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
            format.fmt.pix.field = V4L2_FIELD_ANY;
        }

        if (xioctl(fd, VIDIOC_S_FMT, &format) < 0) {
            std::cerr << "VIDIOC_S_FMT failed: " << std::strerror(errno) << '\n';
            return false;
        }

        std::cout << "Camera format: "
                  << (use_multi_plane ? std::to_string(format.fmt.pix_mp.width) : std::to_string(format.fmt.pix.width))
                  << "x"
                  << (use_multi_plane ? std::to_string(format.fmt.pix_mp.height) :
                                        std::to_string(format.fmt.pix.height))
                  << ", format="
                  << fourccToString(use_multi_plane ? format.fmt.pix_mp.pixelformat : format.fmt.pix.pixelformat)
                  << ", planes=" << (use_multi_plane ? format.fmt.pix_mp.num_planes : 1) << '\n';
        return true;
    }

    bool requestBuffers()
    {
        v4l2_requestbuffers request {};
        request.count = 4;
        request.type = buffer_type;
        request.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_REQBUFS, &request) < 0 || request.count < 2) {
            std::cerr << "VIDIOC_REQBUFS failed: " << std::strerror(errno) << '\n';
            return false;
        }

        buffers.resize(request.count);
        for (std::size_t index = 0; index < buffers.size(); ++index) {
            if (!mapBuffer(index)) {
                return false;
            }
        }

        for (std::size_t index = 0; index < buffers.size(); ++index) {
            if (!queueBuffer(index)) {
                return false;
            }
        }

        return true;
    }

    bool mapBuffer(std::size_t index)
    {
        v4l2_buffer buffer {};
        v4l2_plane planes[VIDEO_MAX_PLANES] {};
        buffer.type = buffer_type;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = static_cast<unsigned int>(index);
        if (use_multi_plane) {
            buffer.length = VIDEO_MAX_PLANES;
            buffer.m.planes = planes;
        }

        if (xioctl(fd, VIDIOC_QUERYBUF, &buffer) < 0) {
            std::cerr << "VIDIOC_QUERYBUF failed: " << std::strerror(errno) << '\n';
            return false;
        }

        const unsigned int plane_count = use_multi_plane ? buffer.length : 1;
        buffers[index].planes.resize(plane_count);
        for (unsigned int plane_index = 0; plane_index < plane_count; ++plane_index) {
            const std::size_t length = use_multi_plane ? planes[plane_index].length : buffer.length;
            const std::size_t offset = use_multi_plane ? planes[plane_index].m.mem_offset : buffer.m.offset;
            buffers[index].planes[plane_index].length = length;
            buffers[index].planes[plane_index].start =
                mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
            if (buffers[index].planes[plane_index].start == MAP_FAILED) {
                std::cerr << "mmap camera buffer failed: " << std::strerror(errno) << '\n';
                return false;
            }
        }

        return true;
    }

    bool queueBuffer(std::size_t index)
    {
        v4l2_buffer buffer {};
        v4l2_plane planes[VIDEO_MAX_PLANES] {};
        buffer.type = buffer_type;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = static_cast<unsigned int>(index);
        if (use_multi_plane) {
            buffer.length = static_cast<unsigned int>(buffers[index].planes.size());
            buffer.m.planes = planes;
        }

        if (xioctl(fd, VIDIOC_QBUF, &buffer) < 0) {
            std::cerr << "VIDIOC_QBUF failed: " << std::strerror(errno) << '\n';
            return false;
        }
        return true;
    }

    bool startStream()
    {
        v4l2_buf_type type = buffer_type;
        if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            std::cerr << "VIDIOC_STREAMON failed: " << std::strerror(errno) << '\n';
            return false;
        }
        streaming = true;
        return true;
    }

    std::string device_path;
    int fd = -1;
    bool streaming = false;
    bool use_multi_plane = false;
    v4l2_buf_type buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_format format {};
    std::vector<MappedBuffer> buffers;
};

V4L2CameraCapture::V4L2CameraCapture(std::string device_path)
    : impl_(std::make_unique<Impl>(std::move(device_path)))
{
}

V4L2CameraCapture::~V4L2CameraCapture() = default;

bool V4L2CameraCapture::open()
{
    return impl_->open();
}

bool V4L2CameraCapture::readFrame(int frame_id, RawFrame& frame)
{
    return impl_->readFrame(frame_id, frame);
}
