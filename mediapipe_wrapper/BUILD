cc_binary(
    name = "libsign_mediapipe_extractor.so",
    srcs = ["sign_mediapipe_extractor.cc"],
    copts = ["-I/usr/include/opencv4"],
    linkopts = [
        "-lopencv_core",
        "-lopencv_imgproc",
    ],
    linkshared = True,
    linkstatic = True,
    deps = [
        "//mediapipe/framework/formats:image",
        "//mediapipe/framework/formats:image_frame",
        "//mediapipe/tasks/cc/vision/core:running_mode",
        "//mediapipe/tasks/cc/vision/hand_landmarker:hand_landmarker",
        "//mediapipe/tasks/cc/vision/pose_landmarker:pose_landmarker",
    ],
)
