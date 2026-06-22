#include "pipeline/inc/MediaPipeKeypointExtractor.hpp"

#include "pipeline/inc/KeypointFeatureUtils.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

#if defined(HAVE_MEDIAPIPE_WRAPPER) && !defined(HAVE_MEDIAPIPE_WRAPPER_DIRECT)
#include <dlfcn.h>
#ifndef RTLD_DEEPBIND
#define RTLD_DEEPBIND 0
#endif
#endif

#if defined(HAVE_MEDIAPIPE_WRAPPER) && defined(HAVE_MEDIAPIPE_WRAPPER_DIRECT)
extern "C" void* sign_mp_create(const char* pose_model_path, const char* hand_model_path);
extern "C" int sign_mp_extract(void* handle,
                               const unsigned char* bgr,
                               int width,
                               int height,
                               int stride,
                               float* out_feature_258,
                               float* out_display_points,
                               int* out_display_point_count);
extern "C" void sign_mp_destroy(void* handle);
extern "C" const char* sign_mp_last_error();
#endif

struct MediaPipeKeypointExtractor::Impl {
#ifdef HAVE_MEDIAPIPE_WRAPPER
    using CreateFn = void* (*)(const char*, const char*);
    using ExtractFn = int (*)(void*, const unsigned char*, int, int, int, float*, float*, int*);
    using DestroyFn = void (*)(void*);
    using LastErrorFn = const char* (*)();

    void* library = nullptr;
    void* handle = nullptr;
    CreateFn create = nullptr;
    ExtractFn extract = nullptr;
    DestroyFn destroy = nullptr;
    LastErrorFn last_error = nullptr;
#endif
};

MediaPipeKeypointExtractor::MediaPipeKeypointExtractor() = default;

MediaPipeKeypointExtractor::~MediaPipeKeypointExtractor()
{
#ifdef HAVE_MEDIAPIPE_WRAPPER
    if (impl_) {
        if (impl_->handle && impl_->destroy) {
            impl_->destroy(impl_->handle);
        }
#ifndef HAVE_MEDIAPIPE_WRAPPER_DIRECT
        if (impl_->library) {
            dlclose(impl_->library);
        }
#endif
    }
#endif
}

bool MediaPipeKeypointExtractor::load(const std::string& pose_model_path,
                                      const std::string& hand_model_path)
{
#ifdef HAVE_MEDIAPIPE_WRAPPER
    impl_ = std::make_unique<Impl>();

#ifdef HAVE_MEDIAPIPE_WRAPPER_DIRECT
    impl_->create = sign_mp_create;
    impl_->extract = sign_mp_extract;
    impl_->destroy = sign_mp_destroy;
    impl_->last_error = sign_mp_last_error;
    std::cerr << "Using directly linked MediaPipe wrapper.\n";
#else
#ifdef SIGN_MEDIAPIPE_WRAPPER_PATH
    const char* wrapper_path = SIGN_MEDIAPIPE_WRAPPER_PATH;
#else
    const char* wrapper_path = "libsign_mediapipe_extractor.so";
#endif

    std::cerr << "Loading MediaPipe wrapper: " << wrapper_path << '\n';
    impl_->library = dlopen(wrapper_path, RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
    if (!impl_->library) {
        status_ = std::string("Failed to load MediaPipe wrapper: ") + dlerror();
        ready_ = false;
        impl_.reset();
        return ready_;
    }

    impl_->create = reinterpret_cast<Impl::CreateFn>(dlsym(impl_->library, "sign_mp_create"));
    impl_->extract = reinterpret_cast<Impl::ExtractFn>(dlsym(impl_->library, "sign_mp_extract"));
    impl_->destroy = reinterpret_cast<Impl::DestroyFn>(dlsym(impl_->library, "sign_mp_destroy"));
    impl_->last_error = reinterpret_cast<Impl::LastErrorFn>(dlsym(impl_->library, "sign_mp_last_error"));
#endif

    if (!impl_->create || !impl_->extract || !impl_->destroy) {
        status_ = "MediaPipe wrapper is missing required symbols";
        ready_ = false;
        impl_.reset();
        return ready_;
    }

    std::cerr << "Creating MediaPipe wrapper extractor.\n";
    impl_->handle = impl_->create(pose_model_path.c_str(), hand_model_path.c_str());
    if (!impl_->handle) {
        status_ = impl_->last_error ? impl_->last_error() : "MediaPipe wrapper create failed";
        ready_ = false;
        impl_.reset();
        return ready_;
    }

    status_ = "MediaPipe wrapper extractor is ready";
    ready_ = true;
    std::cerr << status_ << '\n';
#else
    (void)pose_model_path;
    (void)hand_model_path;
    status_ = "MediaPipe wrapper is not enabled at build time";
    ready_ = false;
#endif

    return ready_;
}

bool MediaPipeKeypointExtractor::isReady() const
{
    return ready_;
}

MediaPipeKeypointResult MediaPipeKeypointExtractor::extract(const cv::Mat& bgr_image,
                                                            int64_t timestamp_ms)
{
    (void)timestamp_ms;

    MediaPipeKeypointResult result;
    result.feature.assign(keypoint_feature::kRawFeatureDim, 0.0F);

#ifdef HAVE_MEDIAPIPE_WRAPPER
    if (!ready_ || !impl_ || !impl_->handle || !impl_->extract) {
        result.status = status_.empty() ? "MediaPipe wrapper is not initialized" : status_;
        return result;
    }
    if (bgr_image.empty() || bgr_image.channels() != 3) {
        result.status = "MediaPipe wrapper expects a non-empty BGR image";
        return result;
    }

    cv::Mat continuous = bgr_image.isContinuous() ? bgr_image : bgr_image.clone();
    float display_points[keypoint_feature::kHandLandmarkCount * 2 * 2] = {0.0F};
    int display_point_count = 0;

    const int ret = impl_->extract(
        impl_->handle,
        continuous.data,
        continuous.cols,
        continuous.rows,
        static_cast<int>(continuous.step),
        result.feature.data(),
        display_points,
        &display_point_count);

    if (ret != 0) {
        result.status = impl_->last_error ? impl_->last_error() : "MediaPipe wrapper extract failed";
        return result;
    }

    const int capped_count = std::min(display_point_count, keypoint_feature::kHandLandmarkCount * 2);
    result.display_points.reserve(capped_count);
    for (int i = 0; i < capped_count; ++i) {
        result.display_points.emplace_back(display_points[i * 2], display_points[i * 2 + 1]);
    }

    result.ok = true;
    result.status = "MediaPipe wrapper feature generated";
#else
    (void)bgr_image;
    result.status = status_.empty() ? "MediaPipe wrapper is not initialized" : status_;
#endif

    return result;
}
