# RK3588 手语交互系统当前进度记录

日期：2026-06-03

## 项目背景

项目目标是基于 RK3588 开发医院门诊无障碍双向手语交互系统。当前阶段聚焦在第一条基础数据链路：

```text
摄像头输入 -> V4L2 采集 -> 多线程流水线 -> 后续 RGA/RKNN/GRU 接入
```

现阶段尚未接入真实 YOLO、Hand21、GRU 模型，主要目标是把板端摄像头采集和流水线框架跑稳，为后续硬件加速和算法模块预留结构。

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
pipeline/inc/Pipeline.hpp
pipeline/src/CameraCapture.cpp
pipeline/src/Pipeline.cpp
```

各文件职责如下：

- `FrameTypes.hpp`：定义各阶段传递的数据结构，例如 `RawFrame`、`PreprocessedFrame`、`DetectedHand`、`KeypointFeature`、`RecognitionResult`。
- `CameraCapture.hpp/.cpp`：封装 V4L2 摄像头采集逻辑，负责打开设备、设置格式、mmap buffer、启动 stream、读取帧和释放资源。
- `Pipeline.hpp/.cpp`：保留多线程流水线调度逻辑，负责 capture / preprocess / hand-detect / keypoints / sequence / output 等阶段。
- `main.cpp`：程序入口，解析摄像头设备和帧数参数。

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

目前除采集阶段外，其余算法阶段仍是占位模拟：

- `preprocessLoop`：打印输入帧格式，未来替换为 RGA resize / color conversion。
- `handDetectLoop`：固定模拟 YOLO/RKNN 检测置信度。
- `keypointLoop`：固定模拟 21 个手部关键点。
- `sequenceLoop`：固定模拟 GRU/LSTM 输出文本。
- `outputLoop`：打印识别文本和端到端延迟。

## 当前程序行为

程序现在会读取摄像头帧，但不会显示画面。原因是当前程序只完成了采集和流水线传递，没有创建图像窗口，也没有把 `NV12/NM12` 转成可显示的 RGB/BGR 图像。

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

运行 USB 摄像头示例：

```bash
v4l2-ctl --list-devices
./build/SignLanguagePipeline /dev/videoX 120
```

其中 `/dev/videoX` 替换成实际 UVC 摄像头节点。

## 当前风险和注意事项

1. MIPI 摄像头不能只看 `/dev/video11` 是否存在，还要确认 sensor 是否真正挂到 media pipeline。
2. 如果出现 `Not active sensor` 或 `check rkisp_mainpath link or isp input`，优先检查 MIPI 排线、摄像头型号、设备树和系统镜像。
3. 当前 `RawFrame::data` 会把 mmap buffer 内容复制到 `std::vector`，这有利于先验证功能，但不是最终高性能零拷贝方案。
4. 当前还没有显示模块，也没有保存首帧功能。
5. 当前还没有接入 RGA、RKNN、OpenCV 图像预处理和真实模型。

## 下一步建议

1. 增加一个调试功能：保存首帧为文件，方便 SSH 下确认摄像头画面。
2. 把摄像头参数做成配置项，例如 width、height、format、frame_count。
3. 引入 RGA 预处理模块，先完成 `NM12/NV12 -> RGB/BGR 640x640` 的 resize 和颜色转换。
4. 接入 YOLO RKNN 手部检测模型，先跑单帧检测。
5. 接入 Hand21 关键点提取，形成手部拓扑特征。
6. 接入 GRU 时序模型和状态机文本拼接逻辑。

## 当前阶段结论

当前项目已经从“空流水线 demo”推进到“板端 V4L2 摄像头采集 + 多线程流水线骨架”阶段。摄像头采集方向选择正确，代码结构也已经拆出独立采集模块，后续可以围绕 RGA 预处理和 RKNN 模型推理继续扩展。
