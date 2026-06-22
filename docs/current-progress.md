# RK3588 手语交互系统当前进度记录

日期：2026-06-19

## 当前阶段结论

当前项目已经形成一条稳定的板端演示主线：

```text
Qt/QML 双屏界面
    -> Qt 采集摄像头并显示患者端实时画面
    -> Qt 按需发送 JPEG 帧到 Python worker
    -> Python MediaPipe Pose + Hand 提取关键点
    -> TCN RKNN 在 RK3588 NPU 上完成词汇识别
    -> RKLLM / 规则兜底整理患者主诉
    -> Qt 患者端和医生端同步显示结果
```

经过 C++ MediaPipe 路线探索后，当前演示主线暂定为：

```text
Python 负责 MediaPipe + RKNN + 句子整理
Qt/QML 负责双屏 UI、摄像头预览、操作按钮和状态显示
```

C++ MediaPipe 相关代码和 wrapper 实验仍保留在工作区，但不是当前演示主线。

## 当前可演示能力

- 患者端显示实时摄像头画面、ROI 框、当前识别词、采集进度和大按钮。
- 医生端显示电子病历风格界面，包括实时识别、患者主诉、识别词、症状和时间。
- 支持 HDMI + DSI 双屏布局：
  - DSI：患者端，保留触摸操作入口。
  - HDMI：医生端，显示电子病历式信息面板。
- 支持一键启动脚本：

```bash
~/game.sh
```

该脚本会进入项目目录并调用：

```bash
./scripts/run_qml_with_ai.sh
```

脚本会自动检查 / 启动 RKLLM Flask 服务，然后启动 QML 界面。

## 当前交互逻辑

系统启动后先进入待机状态：

```text
点击开始按钮开始识别
```

此时 Qt 只负责摄像头预览，不向 Python worker 发送识别帧，降低空闲负载。

点击开始按钮后进入识别：

```text
开始 -> 等待中 / 识别中 / 稳定词汇
```

点击结束后：

- 如果已经识别到词汇，则调用句子整理逻辑。
- 如果还没有识别到词汇，则提示：

```text
请先进行手语识别后再按结束按钮
```

当前按钮文案已简化为：

```text
开始
结束
删除
清空
启动系统
停止系统
```

其中“删除”用于移除最后一个识别词，适合误识别后继续完成当前句子；“清空”用于清空整句话并重新开始。

## 当前运行参数

Qt 端摄像头采集：

```text
1280x720
```

患者端预览显示：

```text
960x540
```

发送给 Python / MediaPipe 的识别帧：

```text
640x360 JPEG
```

选择 `640x360` 的原因是它在当前 RK3588 板端速度更稳定。`1280x720` 精度略好，但 MediaPipe 处理明显变慢；`960x540` 是折中方案，已经测试过，后续仍可按现场效果切换。

Python worker 当前由 Qt 启动，核心参数为：

```bash
python3 scripts/infer/infer_realtime_rknn.py \
  --model artifacts/models/sign_tcn_best.rknn \
  --labels label_map.json \
  --camera 21 \
  --width 1280 \
  --height 720 \
  --pose-interval 4 \
  --hand-interval 2 \
  --json-interval 0.2 \
  --frame-stdin \
  --composer rkllm \
  --rkllm-url http://127.0.0.1:8080/rkllm_chat \
  --llm-timeout 8 \
  --no-window
```

注意：`--frame-stdin` 模式下，Python 不再直接打开摄像头，实际识别帧来自 Qt stdin。

## RKLLM 后台服务

当前使用 DeepSeek-R1-Distill-Qwen-1.5B 的 RKLLM 模型作为句子整理后端：

```bash
cd ~/rkllm_server

python3 flask_server.py \
  --rkllm_model_path /home/elf/models/DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm \
  --target_platform rk3588
```

启动成功后服务地址为：

```text
http://127.0.0.1:8080/rkllm_chat
```

`scripts/run_qml_with_ai.sh` 会在需要时自动启动该服务，并将日志写入：

```text
artifacts/logs/rkllm_server.log
```

## 句子整理策略

当前句子整理采用：

```text
RKLLM 优先 + 规则兜底
```

由于板端 1.5B LLM 对极短词序输入不够稳定，容易出现解释型输出或中英混杂，例如：

```text
提供了两个词组：“我”和“头”，以及“yesterday”和“pain”
```

因此已加入后处理过滤：

- 包含英文字符时判定为非法输出。
- 包含“医生、用户、助手、系统、词汇、词组、主诉、提供、以下、分别、以及”等解释型词时判定为非法输出。
- 非法输出会自动回退到规则句子。

当前更稳的方向是：短词主诉以规则槽位为主，LLM 只做辅助润色。

## 手部丢失容错

之前实时识别中只要某一帧 `has_valid_hand == false` 就会清空序列，导致用户明明手还在 ROI 框内，采集进度却突然归零。

现在新增短暂丢手容忍：

```text
HAND_LOST_GRACE_SEC = 0.35
```

效果：

- 短暂丢手 0.35 秒内不清空进度，只暂停追加关键点帧。
- 超过 0.35 秒仍未检测到手，才清空序列。
- Qt 状态栏会显示 `lost N`，用于判断是否存在 MediaPipe 偶发丢手。

## QML 稳定性优化

QML 版本曾出现“界面像卡死，但后台诊断仍在增长”的现象。诊断日志显示：

```text
ui=alive
capture 持续增长
preview_ok 持续增长
sent 持续增长
results 持续增长
backpressure=0
```

说明 Python / MediaPipe / RKNN 链路没有卡死，问题更像 Qt Quick 渲染层或窗口显示刷新问题。

已做优化：

- 去掉 `ImageProvider + image://` 预览刷新方式。
- 新增 `CameraView`，使用 `QQuickPaintedItem` 直接绘制 QImage。
- 去掉 `QQuickPaintedItem::FramebufferObject` 渲染目标，避免 RK3588 + Mali + Qt5 下可能的 FBO 渲染不稳定。
- 默认设置：

```text
QT_QUICK_BACKEND=software
QSG_RENDER_LOOP=basic
```

- QML 界面不再实时追加所有日志到 ListView，诊断日志只输出到终端。

## 诊断日志

Qt 会周期性输出诊断信息，例如：

```text
diag ui=alive running=1 recording=1 worker_state=2 stdin_bytes=0 capture=134 preview_ok=134 preview_drop=0 encoded=131 sent=130 backpressure=0 worker_latest=131 worker_sent_id=130 results=44 recog_size=640x360
```

字段含义：

- `ui=alive`：Qt 主线程定时器仍在运行。
- `capture`：Qt 摄像头采集帧计数。
- `preview_ok`：成功投递到 QML 预览的帧数。
- `preview_drop`：QML 来不及消费时主动丢弃的预览帧数。
- `encoded`：编码为 JPEG 的识别帧数。
- `sent`：发送给 Python worker 的帧数。
- `backpressure`：Python stdin 积压次数。
- `results`：Python 返回识别结果次数。
- `recog_size`：当前送入 Python / MediaPipe 的识别帧尺寸。

如果界面出现异常，应优先根据该日志判断卡点。

## 最近提交

上一稳定版里程碑提交：

```text
6c839d71 fix(qt): stabilize QML recognition workflow
```

该提交包含：

- QML 预览改为 `CameraView`。
- 删除旧 `PreviewImageProvider`。
- 增加一键启动脚本 `scripts/run_qml_with_ai.sh`。
- 启动后待机，点击开始后才识别。
- 结束按钮空词汇保护。
- 删除最后一个识别词和清空整句话的患者端操作。
- RKLLM 输出过滤与规则兜底。
- MediaPipe 短暂丢手容错。
- 诊断日志。

## 当前遗留问题

- QML 在 RK3588 板端仍需继续观察长期稳定性；如果再次出现显示层假死，可考虑退回 Qt Widgets 作为演示 UI。
- 词序混乱时，RKLLM 不一定能稳定重组成句，建议后续完善规则槽位抽取。
- DSI 触摸可用于患者端按钮操作；触摸按压动画在当前 Qt Quick software 渲染路径下不稳定，暂不作为演示重点。
- C++ MediaPipe wrapper 已经验证基础 smoke test，但完整接入主程序时遇到 protobuf / 动态库 / 构建复杂度问题，当前暂停。

## 下一步建议

1. 固定当前 QML + Python + RKNN + RKLLM 演示流程，减少临时改动。
2. 根据实际演示词汇表补充规则槽位：
   - 主语：我 / 患者
   - 时间：昨天 / 今天
   - 部位：头
   - 症状：疼痛 / 咳嗽 / 发烧
3. 如果要继续提升准确率，优先从数据和识别帧尺寸平衡入手，而不是继续依赖 LLM 猜句。
4. 演示前统一使用：

```bash
~/game.sh
```

---

## 历史记录：2026-06-18

## 项目背景

项目目标是基于 RK3588 开发医院门诊无障碍双向手语交互系统。当前阶段聚焦在第一条基础数据链路：

```text
摄像头输入 -> Python MediaPipe 关键点 -> TCN RKNN 时序识别 -> Qt 交互界面
```

现阶段已经完成板端摄像头采集、RGA 图像预处理、Mock 算法接口、屏幕显示闭环，以及关键点时序模型从 GRU 到 TCN 的 RKNN 部署验证。经过 C++ MediaPipe 编译和集成尝试后，当前主线调整为：Python 负责 MediaPipe Pose + Hand 关键点提取和 TCN RKNN 推理，C++/Qt 负责上层交互界面。

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
pipeline/inc/KeypointFeatureUtils.hpp
pipeline/inc/KeypointSequenceBuffer.hpp
pipeline/inc/MediaPipeKeypointExtractor.hpp
pipeline/inc/RgaProcessor.hpp
pipeline/inc/Pipeline.hpp
pipeline/src/CameraCapture.cpp
pipeline/src/KeypointFeatureUtils.cpp
pipeline/src/KeypointSequenceBuffer.cpp
pipeline/src/MediaPipeKeypointExtractor.cpp
pipeline/src/RgaProcessor.cpp
pipeline/src/Pipeline.cpp
```

各文件职责如下：

- `FrameTypes.hpp`：定义各阶段传递的数据结构，例如 `RawFrame`、`PreprocessedFrame`、`DetectedHand`、`KeypointFeature`、`RecognitionResult`。
- `CameraCapture.hpp/.cpp`：封装 V4L2 摄像头采集逻辑，负责打开设备、设置格式、mmap buffer、启动 stream、读取帧和释放资源。
- `KeypointFeatureUtils.hpp/.cpp`：封装关键点特征常量和 C++ 版归一化逻辑，与 Python 训练阶段保持一致。
- `KeypointSequenceBuffer.hpp/.cpp`：封装逐帧关键点缓存、时间维重采样和手部有效帧比例统计，输出 TCN 所需的 `[30, 258]` 输入。
- `MediaPipeKeypointExtractor.hpp/.cpp`：封装 C++ MediaPipe Pose + Hand 关键点提取接口；当前未启用 MediaPipe C++ 支持时会明确回退到 mock 特征。
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

### 8. Python RKNN 实时摄像头验证与性能调优

新增 Python 端 RKNN 实时摄像头测试脚本：

```text
scripts/infer/infer_realtime_rknn.py
```

该脚本复用 ONNX 实时脚本中的 MediaPipe Pose / Hand 关键点提取、关键点归一化、30 帧时间窗口和投票平滑逻辑，只将时序分类后端替换为 TCN RKNN：

```text
摄像头帧
    -> MediaPipe Pose + Hand
    -> 258 维关键点序列
    -> TCN RKNN
    -> 文本显示
```

当前实时脚本已支持：

- 全屏窗口显示。
- 采集分辨率和显示分辨率分离。
- Pose / Hand 分频执行，降低 MediaPipe 压力。
- 分段 profiler，显示采集、Pose、Hand、RKNN 和绘制耗时。
- `full`、`upper_body_hands_xy`、`hands_xy` 三种特征模式实验。

当前板端可接受的实时 Demo 参数为：

```bash
python3 scripts/infer/infer_realtime_rknn.py \
  --model artifacts/models/sign_tcn_best.rknn \
  --labels label_map.json \
  --camera 21 \
  --width 1280 \
  --height 720 \
  --display-width 1920 \
  --display-height 1080 \
  --pose-interval 8 \
  --hand-interval 4 \
  --profile
```

板端 profiler 观察到的主要耗时大致为：

```text
采集：约 30 ms
Pose：约 40 ms
Hand：约 70 ms
绘制：约 15 ms
TCN RKNN：不是主要瓶颈
```

当前结论：实时帧率瓶颈主要来自 MediaPipe Pose / Hand 和摄像头采集，TCN RKNN 本身占用很低。因此现阶段继续以 `full` 258 维 TCN 作为主线，`upper_body_hands_xy` 等降维特征作为后续消融实验保留。

### 9. C++ TCN RKNN 推理模块接入

新增 C++ 端 TCN RKNN 推理封装：

```text
pipeline/inc/TcnRecognizer.hpp
pipeline/src/TcnRecognizer.cpp
```

该模块使用 RKNN C API 加载 `.rknn` 模型，输入固定为：

```text
float32 [1, 30, 258]
```

并读取 `label_map.json`，输出 top1 标签、置信度和单次 RKNN 推理延迟。

当前 C++ pipeline 已支持通过命令行传入 TCN 模型和标签文件：

```bash
./build/SignLanguagePipeline \
  /dev/video21 \
  300 \
  artifacts/models/sign_tcn_best.rknn \
  label_map.json
```

当前阶段 `keypointLoop()` 仍然使用 mock 258 维逐帧特征，只用于打通 C++ RKNN API 调用路径。后续需要在 C++ 端接入真实 Pose + Hand 关键点后，将 mock 特征生产器替换为真实关键点输出。

板端已完成运行验证，说明 C++ 到 RKNN C API 再到 TCN 模型推理的调用链路已经打通。

### 10. C++ 关键点序列缓存模块

新增 C++ 端关键点序列缓冲模块：

```text
pipeline/inc/KeypointSequenceBuffer.hpp
pipeline/src/KeypointSequenceBuffer.cpp
```

该模块负责把逐帧 258 维关键点特征整理为 TCN 固定输入：

```text
单帧 keypoint_feature[258]
    -> KeypointSequenceBuffer 缓存最近若干帧
    -> 时间维线性重采样
    -> float32 [30, 258]
    -> KeypointFeatureUtils 关键点归一化
    -> TcnRecognizer::infer()
```

当前 C++ `keypointLoop()` 仍然使用 mock 关键点，但已经从“一次性生成整段 mock TCN 序列”改为“逐帧生成 258 维特征，再由序列缓冲器重采样”。这样后续接入真实 C++ MediaPipe Pose + Hand 输出时，只需要替换单帧 `keypoint_feature` 的来源，TCN 输入接口不需要再重构。

### 11. C++ 关键点归一化对齐

新增 C++ 版关键点归一化模块：

```text
pipeline/inc/KeypointFeatureUtils.hpp
pipeline/src/KeypointFeatureUtils.cpp
```

该模块复刻 Python 训练和评估阶段的 `normalize_keypoints()` 逻辑：

- 使用左肩和右肩中心作为身体中心。
- 使用左右肩宽作为尺度。
- 对 pose 的 `x/y/z` 做归一化，保留 visibility。
- 对非空的 left hand / right hand `x/y/z` 使用同一中心和尺度归一化。

当前 `sequenceLoop()` 在调用 `TcnRecognizer::infer()` 前，会先对 `KeypointSequenceBuffer` 输出的 `[30, 258]` 序列做归一化。这样后续 C++ MediaPipe 输出原始 landmark 后，进入 TCN 的数据契约会与 Python 训练阶段保持一致。

### 12. C++ MediaPipe 关键点接口

新增 C++ MediaPipe 关键点提取接口：

```text
pipeline/inc/MediaPipeKeypointExtractor.hpp
pipeline/src/MediaPipeKeypointExtractor.cpp
```

当前 `keypointLoop()` 已经改为优先调用该 extractor：

```text
BGR cv::Mat
    -> MediaPipeKeypointExtractor::extract()
    -> feature[258] + display_points
    -> KeypointSequenceBuffer
```

如果当前构建没有启用 C++ MediaPipe 支持，extractor 会保持 disabled，pipeline 自动回退到 mock 258 维特征，保证 V4L2/RGA/TCN RKNN 演示仍然可以运行。下一步需要在板端确认 MediaPipe C++ / MediaPipe Tasks 的编译和链接方式，再补齐 extractor 内部真实实现。

### 13. Python RKNN Worker 与 Qt 前端验证

在 C++ MediaPipe 集成过程中，已经验证 MediaPipe C++ smoke test 能分别跑通 Hand、Pose 和组合 258 维特征输出，但将 Bazel 产物作为动态库接入当前 CMake pipeline 时，遇到 protobuf descriptor 初始化阶段段错误。为保证项目 Demo 进度，当前暂时不继续强推 C++ MediaPipe 主线，改为 Python + Qt 混合架构。

当前稳定主线为：

```text
USB 摄像头
    -> Python OpenCV 读帧
    -> MediaPipe Pose + Hand
    -> 258 维关键点序列
    -> TCN RKNN
    -> JSON Lines
    -> Qt 前端 QProcess 读取并显示
```

`scripts/infer/infer_realtime_rknn.py` 已新增 worker 模式：

- `--json-output`：向 stdout 输出 JSON Lines，供 Qt/QProcess 读取。
- `--no-window`：关闭 OpenCV 预览窗口，作为后台识别进程运行。
- `--json-interval`：控制状态输出频率。

新增 Qt Widgets 前端：

```text
qt_app/CMakeLists.txt
qt_app/main.cpp
qt_app/MainWindow.hpp
qt_app/MainWindow.cpp
```

Qt 前端当前支持：

- 启动和停止 Python RKNN worker。
- 逐行读取 stdout，并解析 JSON Lines。
- 显示识别文本、置信度、FPS、窗口帧数、关键阶段延迟和运行状态。
- 通过 UI 配置模型路径、标签路径、摄像头编号、分辨率和 Pose/Hand 分频参数。

板端已经完成验证：Python RKNN worker 可运行，Qt 前端可启动 worker 并显示识别结果。当前项目演示主线正式切换为 Python MediaPipe/RKNN + C++ Qt UI。

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
- `handDetectLoop`：伪造手部检测框，主要用于当前可视化占位；后续真实关键点主线不依赖 YOLO 手框。
- `keypointLoop`：优先调用 `MediaPipeKeypointExtractor`，未启用 C++ MediaPipe 时回退到 mock 21 点和单帧 258 维特征。
- `sequenceLoop`：使用 `KeypointSequenceBuffer` 将逐帧特征重采样为 `[30, 258]`，再按训练逻辑归一化，TCN 可用时调用 RKNN 推理，否则继续输出 mock 病历文本。
- `outputLoop`：绘制检测框、关键点、状态栏、FPS 和端到端延迟，并通过 OpenCV 实时显示。

## 当前程序行为

程序现在会读取摄像头帧，通过 RGA 生成 `BGR888 640x640` 的 `cv::Mat`，并在 MIPI 屏桌面上实时显示 mock 可视化结果。

Python 实时 RKNN Demo 当前可以直接从 USB 摄像头读取画面，调用 MediaPipe 提取关键点，并通过 TCN RKNN 在板端 NPU 上输出识别文本。Qt 前端已经可以通过 `QProcess` 启动该 Python worker，并实时读取 JSON Lines 更新界面。

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

运行 Python RKNN worker 无窗口模式：

```bash
python3 scripts/infer/infer_realtime_rknn.py \
  --model artifacts/models/sign_tcn_best.rknn \
  --labels label_map.json \
  --camera 21 \
  --width 1280 \
  --height 720 \
  --pose-interval 8 \
  --hand-interval 4 \
  --no-window
```

运行 Qt 前端：

```bash
cmake -S . -B build
cmake --build build -j4
export DISPLAY=:0.0
./build/qt_app/SignLanguageQt
```

## 当前风险和注意事项

1. MIPI 摄像头不能只看 `/dev/video11` 是否存在，还要确认 sensor 是否真正挂到 media pipeline。
2. 如果出现 `Not active sensor` 或 `check rkisp_mainpath link or isp input`，优先检查 MIPI 排线、摄像头型号、设备树和系统镜像。
3. 当前 `RawFrame::data` 会把 mmap buffer 内容复制到 `std::vector`，并且 RGA 输入前会临时拼接连续 NV12 缓冲，这有利于先验证功能，但不是最终高性能零拷贝方案。
4. 当前实时显示依赖桌面环境和 `DISPLAY`，纯 SSH 无显示权限时需要先配置 X11/桌面权限。
5. TCN/RKNN 时序模型已经完成 Python 侧部署验证，并新增 C++ RKNN 推理封装、关键点序列缓冲、关键点归一化和 MediaPipe 关键点接口；C++ 端 RKNN 调用链路已跑通，C++ MediaPipe 作为备选探索保留。
6. Python 实时链路的主要瓶颈是 MediaPipe Pose / Hand，当前通过分频执行和低/中分辨率采集缓解。
7. 当前不优先接入 YOLO/Hand21，也不继续强推 C++ MediaPipe Demo；下一阶段重点是完善 Qt 交互界面和就诊业务流程。

## 下一步建议

1. 完善 Qt 医院场景 UI，例如患者端识别文本、医生端常用问诊短句、确认/删除/清空/播报按钮。
2. 将 Python worker 输出从单词级结果扩展为状态机短句，例如“我 发烧”“昨天 头 疼痛”。
3. 增加 Qt 与 Python worker 的异常提示，例如摄像头打开失败、模型缺失、MediaPipe 模型缺失。
4. 根据实际演示效果继续调节 `pose-interval`、`hand-interval`、采集分辨率和显示布局。
5. C++ MediaPipe 作为后续优化路线保留，只有在 Qt/Python Demo 稳定后再继续处理 protobuf/动态库冲突。
6. 在真实交互链路跑稳后再评估 DMA-BUF / ION fd 零拷贝，减少 `RawFrame::data` 和连续 NV12 临时缓冲带来的 CPU 拷贝。

## 当前阶段结论

当前项目已经从“空流水线 demo”推进到“板端 V4L2 摄像头采集 + RGA 硬件预处理 + Mock 算法接口 + MIPI 屏实时可视化 + TCN/RKNN 时序模型部署验证 + Python RKNN 实时摄像头 Demo + Qt 交互前端”阶段。摄像头采集、RGA 图像转换、端到端显示闭环、关键点时序模型 RKNN 部署、Python 实时识别和 Qt 前端通信均已实测成立，后续主线是完善 Python worker + Qt 的医院问诊交互 Demo。

当前阶段已提交：

```text
939fea5 feat(pipeline): add camera capture and RGA preprocessing
```

本次显示闭环提交：

```text
feat(pipeline): add mock visualization output
```

本次 Python/Qt 主线提交：

```text
feat(qt): add Python RKNN worker frontend
```
