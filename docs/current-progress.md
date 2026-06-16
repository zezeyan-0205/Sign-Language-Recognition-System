# RK3588 手语交互系统当前进度记录

日期：2026-06-17

## 项目背景

项目目标是基于 RK3588 开发医院门诊无障碍双向手语交互系统。当前阶段聚焦在第一条基础数据链路：

```text
摄像头输入 -> V4L2 采集 -> RGA 预处理 -> Mock 算法接口 -> MIPI 屏实时显示 -> TCN/RKNN 时序识别接入
```

现阶段已经完成板端摄像头采集、RGA 图像预处理、Mock 算法接口、屏幕显示闭环，以及关键点时序模型从 GRU 到 TCN 的 RKNN 部署验证。后续重点转向 YOLO 手部检测、Hand21 关键点提取和 C++ 端真实模型接入。

## 已完成工作

### 1. 本地工程基础

- 已在 RK3588 Ubuntu 22.04 / ARM64 环境下使用 CMake + G++17 本地编译。
- 工程目标程序为 `SignLanguagePipeline`。
- 当前工程支持通过命令行指定摄像头设备节点和读取帧数。

示例：

```bash
./build/SignLanguagePipeline /dev/video11 120
```

### 2. 摄像头采集路径

当前采集入口选择原生 V4L2，而不是直接依赖 OpenCV `VideoCapture`。原因是 V4L2 更接近 RK3588 后续 RGA / DMA-BUF / RKNN 的底层数据通路，便于保留摄像头原始格式、缓冲区信息和硬件加速扩展空间。

当前已支持两类 V4L2 摄像头节点：

- UVC USB Camera：常见 single-plane V4L2 capture。
- RKISP MIPI Camera：常见 multi-plane V4L2 capture，例如 `NM12` / `NV12M`。

### 3. MIPI 摄像头调试过程

开发板手册中 OV13855 MIPI 摄像头示例节点为：

```text
/dev/video11
```

实际调试中曾遇到：

```text
/dev/video11 does not support V4L2 streaming capture.
```

原因是 `/dev/video11` 是 RKISP multi-plane 节点，不是普通 single-plane capture。代码已升级为自动识别：

- `V4L2_CAP_VIDEO_CAPTURE`
- `V4L2_CAP_VIDEO_CAPTURE_MPLANE`

随后又遇到：

```text
VIDIOC_STREAMON failed: Operation not permitted
```

通过 `v4l2-ctl` 和 `media-ctl` 判断，该问题不是 C++ 程序权限或格式错误，而是当时 MIPI sensor 没有正确挂到 ISP 链路。后续用户重新处理硬件连接后，摄像头采集已跑通。

### 4. 当前代码拆分

为了避免所有逻辑堆在 `Pipeline.cpp`，已经完成初步模块拆分：

```text
pipeline/inc/FrameTypes.hpp
pipeline/inc/CameraCapture.hpp
pipeline/inc/RgaProcessor.hpp
pipeline/inc/Pipeline.hpp
pipeline/src/CameraCapture.cpp
pipeline/src/RgaProcessor.cpp
pipeline/src/Pipeline.cpp
```

各文件职责如下：

- `FrameTypes.hpp`：定义各阶段传递的数据结构，例如 `RawFrame`、`PreprocessedFrame`、`DetectedHand`、`KeypointFeature`、`RecognitionResult`。
- `CameraCapture.hpp/.cpp`：封装 V4L2 摄像头采集逻辑，负责打开设备、设置格式、mmap buffer、启动 stream、读取帧和释放资源。
- `RgaProcessor.hpp/.cpp`：封装 RGA 图像转换逻辑，将 V4L2 采集到的 NM12/NV12M 数据转换并缩放为 `BGR888 640x640`。
- `Pipeline.hpp/.cpp`：保留多线程流水线调度逻辑，负责 capture / preprocess / hand-detect / keypoints / sequence / output 等阶段。
- `main.cpp`：程序入口，解析摄像头设备和帧数参数。

### 5. RGA 预处理验证

已在 RK3588 板端验证：

```text
/dev/rga 存在
/usr/include/rga/RgaApi.h 存在
/usr/include/rga/rga.h 存在
/usr/lib/aarch64-linux-gnu/librga.so 存在
```

RGA 转换模块已接入 `preprocessLoop()`，当前处理链路为：

```text
RawFrame NM12/NV12M
    -> 拆分 Y plane / UV plane 指针
    -> RgaProcessor::ImgConvertNM12ToBGR()
    -> BGR888 640x640 cv::Mat
    -> 保存首帧 rga_first_frame.jpg
```

板端测试已生成：

```text
rga_first_frame.jpg
```

用户已人工确认该图片颜色和比例正常，说明当前 V4L2 到 RGA 再到 OpenCV BGR 图像的基础链路已经跑通。

### 6. Mock 算法接口与实时显示验证

已完成从算法占位到可视化输出的核心闭环：

```text
BGR888 640x640 cv::Mat
    -> handDetectLoop() 伪造手部检测框
    -> keypointLoop() 伪造 21 个手部关键点
    -> sequenceLoop() 周期性生成病历文本
    -> outputLoop() 绘制绿框、红点和状态栏
    -> cv::imshow() 投递到 MIPI 屏幕桌面
```

新增统一的 `DetectionResult` 结构体，用于在 mock 算法链路中携带：

- `cv::Rect hand_box`
- `std::vector<cv::Point2f> keypoints`
- `std::string text_result`
- `cv::Mat bgr_image`

输出线程已实现：

- 实时计算显示 FPS。
- 在画面上绘制绿色手部检测框。
- 在画面上绘制 21 个红色骨骼点。
- 在画面底部绘制半透明黑色状态栏。
- 在状态栏中显示 mock 病历文本、FPS 和端到端延迟。
- 支持通过 `q` 或 `Esc` 退出显示窗口。

板端已经完成测试，MIPI 屏幕能够弹窗实时显示画框、骨骼点和病历文本，说明当前端到端可视化 Demo 骨架已经跑通。

### 7. TCN 时序识别模型部署验证

在 GRU RKNN 部署过程中，曾出现 ONNX 与 RKNN 输出差异较大、批量准确率明显下降的问题。结合论坛资料和板端实测，判断 RKNN 对 GRU/LSTM 等循环网络支持有限，因此新增 RKNN 更友好的 TCN/Conv1D 时序分类路线。

当前 TCN 模型输入契约保持不变：

```text
float32 [1, 30, 258]
```

即仍然使用 30 帧关键点序列，每帧 258 维特征，并沿用训练阶段的关键点归一化策略。

新增脚本结构：

```text
scripts/common/sequence_models.py
scripts/train/train_tcn.py
scripts/export/export_tcn_onnx.py
scripts/export/convert_tcn_rknn.py
scripts/eval/eval_sequence_batch.py
```

其中：

- `sequence_models.py`：定义 RKNN 友好的 `TCNClassifier`。
- `train_tcn.py`：训练 TCN 关键点时序分类模型。
- `export_tcn_onnx.py`：导出 TCN ONNX，并验证 PyTorch/ONNX 数值一致性。
- `convert_tcn_rknn.py`：使用 RKNN Toolkit2 2.1.0 转换 `rk3588` 目标模型。
- `eval_sequence_batch.py`：通用关键点序列批量评估脚本，支持 ONNX、RKNN 和两份评估结果对比。

板端验证环境：

```text
rknn-toolkit-lite2: 2.1.0
librknnrt: 2.1.0
rknn_server: 2.1.0
NPU driver: 0.9.8
target platform: rk3588
```

TCN 批量评估结果：

```text
样本数：180
ONNX accuracy：0.9833
RKNN accuracy：0.9833
ONNX/RKNN Top1 agreement：1.0000
Mean cosine logits：1.000000
Mean max prob diff：0.000034
Mismatches：0
```

测试中确认：TCN RKNN 推理不应添加 `--pass-through` 参数。添加该参数会导致板端推理阶段段错误；去掉后模型可正常运行并与 ONNX 输出高度一致。

当前结论：GRU 保留为 PC/ONNX 对照基线，板端 RKNN 部署主路线切换为 TCN。

## 当前流水线状态

当前 pipeline 已具备完整的线程和队列传递结构：

```text
captureLoop
    -> raw_queue_
preprocessLoop
    -> preprocess_queue_
handDetectLoop
    -> hand_queue_
keypointLoop
    -> keypoint_queue_
sequenceLoop
    -> result_queue_
outputLoop
```

目前采集和预处理阶段已经使用真实硬件链路，算法阶段为标准 mock 接口，可视化输出已在板端显示验证：

- `captureLoop`：通过 V4L2 从摄像头读取 NM12/NV12M 原始帧。
- `preprocessLoop`：通过 RGA 将 NM12/NV12M 转换并缩放为 `BGR888 640x640`，并保存第一帧用于验证。
- `handDetectLoop`：伪造手部检测框，作为后续 YOLO/RKNN 输出接口占位。
- `keypointLoop`：伪造 21 个手部关键点，作为后续 Hand21 输出接口占位。
- `sequenceLoop`：周期性生成 mock 病历文本，后续替换为 TCN/RKNN 时序识别结果和状态机文本拼接。
- `outputLoop`：绘制检测框、关键点、状态栏、FPS 和端到端延迟，并通过 OpenCV 实时显示。

## 当前程序行为

程序现在会读取摄像头帧，通过 RGA 生成 `BGR888 640x640` 的 `cv::Mat`，并在 MIPI 屏桌面上实时显示 mock 可视化结果。

程序第一次成功完成 RGA 转换时会生成：

```text
rga_first_frame.jpg
```

如果要直接看画面，当前推荐使用手册中的 GStreamer 命令：

```bash
gst-launch-1.0 v4l2src device=/dev/video11 ! \
  video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! \
  autovideosink
```

如果通过 SSH 调试，没有接显示屏，可先用 GStreamer 保存一张图：

```bash
gst-launch-1.0 v4l2src device=/dev/video11 num-buffers=1 ! \
  video/x-raw,format=NV12,width=640,height=480 ! \
  mppjpegenc ! filesink location=pic.jpg
```

如果要让 OpenCV 窗口显示到板子的 MIPI 屏幕，运行程序前需要在终端配置显示环境：

```bash
export DISPLAY=:0.0
```

如果远程 SSH 运行时遇到显示权限问题，可在板子的图形桌面终端执行：

```bash
xhost +SI:localuser:elf
```

## 编译和运行

板端重新生成 build：

```bash
cd ~/code/rkSignLanguage
rm -rf build
cmake -S . -B build
cmake --build build
```

运行 MIPI 摄像头示例：

```bash
./build/SignLanguagePipeline /dev/video11 120
```

运行后检查 RGA 首帧输出：

```bash
ls -lh rga_first_frame.jpg
```

实时窗口打开后，可按 `q` 或 `Esc` 退出显示。

运行 USB 摄像头示例：

```bash
v4l2-ctl --list-devices
./build/SignLanguagePipeline /dev/videoX 120
```

其中 `/dev/videoX` 替换成实际 UVC 摄像头节点。

## 当前风险和注意事项

1. MIPI 摄像头不能只看 `/dev/video11` 是否存在，还要确认 sensor 是否真正挂到 media pipeline。
2. 如果出现 `Not active sensor` 或 `check rkisp_mainpath link or isp input`，优先检查 MIPI 排线、摄像头型号、设备树和系统镜像。
3. 当前 `RawFrame::data` 会把 mmap buffer 内容复制到 `std::vector`，并且 RGA 输入前会临时拼接连续 NV12 缓冲，这有利于先验证功能，但不是最终高性能零拷贝方案。
4. 当前实时显示依赖桌面环境和 `DISPLAY`，纯 SSH 无显示权限时需要先配置 X11/桌面权限。
5. TCN/RKNN 时序模型已经完成 Python 侧部署验证，但尚未接入 C++ pipeline。
6. 当前还没有接入 YOLO、Hand21 和 C++ 端真实时序模型调用。

## 下一步建议

1. 把摄像头参数做成配置项，例如 width、height、format、frame_count。
2. 将 `handDetectLoop()` 的 mock 逻辑替换为 YOLO RKNN 手部检测模型，先跑单帧检测。
3. 将 `keypointLoop()` 的 mock 逻辑替换为 Hand21 关键点提取，形成手部拓扑特征。
4. 将 `sequenceLoop()` 的 mock 文本替换为 TCN/RKNN 时序模型和状态机文本拼接逻辑。
5. 在 C++ 端接入 TCN RKNN 推理接口，完成关键点序列输入到识别文本输出的真实闭环。
6. 在真实模型链路跑通后再评估 DMA-BUF / ION fd 零拷贝，减少 `RawFrame::data` 和连续 NV12 临时缓冲带来的 CPU 拷贝。

## 当前阶段结论

当前项目已经从“空流水线 demo”推进到“板端 V4L2 摄像头采集 + RGA 硬件预处理 + Mock 算法接口 + MIPI 屏实时可视化 + TCN/RKNN 时序模型部署验证”阶段。摄像头采集、RGA 图像转换、端到端显示闭环和关键点时序模型 RKNN 部署均已实测成立，后续可以围绕 YOLO 手部检测、Hand21 关键点提取和 C++ 端 TCN RKNN 推理继续扩展。

当前阶段已提交：

```text
939fea5 feat(pipeline): add camera capture and RGA preprocessing
```

本次显示闭环提交：

```text
feat(pipeline): add mock visualization output
```
