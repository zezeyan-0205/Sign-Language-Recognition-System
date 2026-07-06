# 医疗手语识别系统技术文档

本文档说明当前项目从数据采集、关键点特征处理、TCN 模型训练、ONNX/RKNN 转换，到 RK3588 板端实时推理、RKLLM 句子整理和 Qt/QML 医患双屏交互的完整技术链路。

## 1. 系统目标

本项目面向门诊场景，目标是让听障患者通过手语表达基础病情，系统将手语动作识别为词汇，再整理成自然中文主诉，并同步到医生端电子病历工作台。

当前主线采用：

```text
Qt/QML 双屏界面
    -> Qt 采集摄像头并显示预览
    -> Qt 在“开始”后通过 stdin 发送 JPEG 帧给 Python worker
    -> Python MediaPipe Pose + Hand 提取关键点
    -> TCN RKNN 模型在 RK3588 NPU 上识别单词
    -> RKLLM / 规则兜底整理为患者主诉
    -> Qt/QML 同步显示到患者端和医生端
    -> 医生确认后写入、保存、导出病历
```

核心分工：

| 模块 | 主要文件 | 责任 |
| --- | --- | --- |
| 数据采集 | `scripts/data/collect_keypoints_tasks.py` | 采集每个词汇动作的 Pose/Hand 关键点，保存 `.npy` |
| 特征处理 | `scripts/common/keypoint_features.py` | 统一序列长度、归一化、选择特征模式 |
| 模型结构 | `scripts/common/sequence_models.py` | 定义 RKNN 友好的 TCN 时序分类网络 |
| 模型训练 | `scripts/train/train_tcn.py` | 训练 TCN，生成 `.pth`、`label_map.json`、评估报告 |
| 模型导出 | `scripts/export/export_tcn_onnx.py` | 将 PyTorch 权重导出为 ONNX |
| RKNN 转换 | `scripts/export/convert_tcn_rknn.py` | 将 ONNX 转换为 RK3588 可运行的 `.rknn` |
| 实时推理 | `scripts/infer/infer_realtime_rknn.py` | MediaPipe + RKNNLite + 词汇状态机 + 句子整理 |
| 句子整理 | `scripts/common/sentence_composer.py` | 规则组句、RKLLM 调用、非法 LLM 输出过滤 |
| Qt 控制层 | `qt_qml_app/SignController.cpp` | 摄像头采集、Python worker 生命周期、JSON 通信、病历落盘 |
| QML 界面 | `qt_qml_app/qml/main.qml` | 患者端、医生端、档案页、设置页等 UI |
| 一键启动 | `scripts/run_qml_with_ai.sh` | 启动 RKLLM 服务、输入法、亮度、QML 程序 |

## 2. 数据采集链路

### 2.1 采集目标

每个手语词汇被采集为一个固定长度关键点序列。系统不直接用 RGB 图片训练 TCN，而是先用 MediaPipe 提取人体和手部关键点，再训练轻量时序模型。

这样做的优点是：

1. 输入维度小，适合 RK3588 板端实时推理。
2. 模型不需要学习背景、光照、衣服颜色等无关信息。
3. 更容易在小数据集上训练出可用效果。

### 2.2 采集脚本

采集入口：

```bash
python3 scripts/data/collect_keypoints_tasks.py \
  --label 头 \
  --camera 21 \
  --save_root dataset_keypoints \
  --record_sec 1.5 \
  --seq_len 30 \
  --require_hand
```

关键参数：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--label` | 必填 | 当前采集词汇名称，也是保存目录名 |
| `--camera` | `0` | OpenCV 摄像头编号，板端常用 `/dev/video21` 对应 `21` |
| `--save_root` | `dataset_keypoints` | 数据集根目录 |
| `--record_sec` | `1.5` | 每段动作采集时长 |
| `--seq_len` | `30` | 保存前重采样后的序列长度 |
| `--require_hand` | 关闭 | 开启后要求开始采集时至少检测到一只手 |

采集脚本固定打开摄像头并设置：

```text
1280x720
30 FPS
手动曝光
不做镜像翻转
```

不做镜像翻转很重要，因为左右手标签要和 MediaPipe 的 `Left` / `Right` 保持一致。

### 2.3 MediaPipe 关键点

采集脚本同时运行：

```text
PoseLandmarker
HandLandmarker
```

Pose 使用 33 个关键点，每个点保存：

```text
x, y, z, visibility
```

因此 Pose 维度为：

```text
33 * 4 = 132
```

Hand 使用左右手各 21 个关键点，每个点保存：

```text
x, y, z
```

单手维度：

```text
21 * 3 = 63
```

左右手合计：

```text
63 * 2 = 126
```

所以原始单帧特征维度为：

```text
132 + 126 = 258
```

### 2.4 ROI 规则

采集阶段只保留画面中间区域内的手：

```python
ROI_X_MIN = 0.15
ROI_X_MAX = 0.85
ROI_Y_MIN = 0.0
ROI_Y_MAX = 1.0
```

也就是说，手部中心点必须位于画面横向 15% 到 85% 之间。这个规则用于降低背景人员或画面边缘误检对数据集的影响。

### 2.5 样本保存格式

采集过程先保存原始时间长度的关键点序列 `raw_sequence`，当采集时间达到 `record_sec` 后，将其重采样到固定 30 帧：

```text
raw_sequence: N x 258
resample_sequence -> 30 x 258
```

最终每段样本保存为：

```text
dataset_keypoints/<label>/<index>.npy
```

例如：

```text
dataset_keypoints/头/001.npy
dataset_keypoints/头/002.npy
dataset_keypoints/咳嗽/001.npy
```

每个 `.npy` 的 shape 应为：

```text
(30, 258)
```

采集保存后会输出左右手有效帧比例：

```text
left_valid=0.xx, right_valid=0.xx
```

这可以帮助判断样本是否真正采到了手部动作。

## 3. 特征处理链路

统一特征处理位于：

```text
scripts/common/keypoint_features.py
```

### 3.1 固定输入契约

当前 TCN 主模型默认输入契约为：

```text
float32 [1, 30, 258]
```

含义：

| 维度 | 含义 |
| --- | --- |
| `1` | batch size |
| `30` | 时序帧数 |
| `258` | 每帧 Pose + 左手 + 右手关键点特征 |

### 3.2 序列补齐与截断

训练、导出、推理都通过 `prepare_sequence_array()` 或 `prepare_sequence_file()` 统一处理输入：

1. 如果序列超过 30 帧，截取前 30 帧。
2. 如果序列少于 30 帧，用全 0 帧补齐。
3. 再做归一化和特征选择。

这保证了采集、训练、推理三阶段的输入一致。

### 3.3 关键点归一化

归一化逻辑：

1. 取左右肩关键点。
2. 用左右肩中心作为人体中心。
3. 用肩宽作为尺度。
4. Pose 的 `x,y,z` 减去肩中心后除以肩宽。
5. 左右手关键点也用同样的人体中心和肩宽归一化。

这样可以降低患者站位远近、画面缩放对模型的影响。

### 3.4 特征模式

当前支持多种特征模式：

| 模式 | 维度 | 说明 |
| --- | --- | --- |
| `full` | `258` | 完整 Pose + 左右手，当前默认模式 |
| `full_face_relative` | `258 + FACE_REL_DIM` | 在 full 基础上追加手指到脸部参考点的相对距离，用于区分头/鼻等脸部指向动作 |
| `upper_body_hands_xy` | 上半身 + 手部 xy | 更轻量，但信息更少 |
| `hands_xy` | 仅手部 xy | 最轻量，但容易丢身体/脸部语义 |

Qt 启动 Python worker 时可通过环境变量切换：

```bash
export SIGN_TCN_MODEL=artifacts/models/sign_tcn_face_best.rknn
export SIGN_FEATURE_MODE=full_face_relative
~/1.sh
```

如果不设置，默认使用：

```text
SIGN_TCN_MODEL=artifacts/models/sign_tcn_best.rknn
SIGN_FEATURE_MODE=full
```

## 4. TCN 模型训练

### 4.1 模型结构

模型定义位于：

```text
scripts/common/sequence_models.py
```

当前使用 `TCNClassifier`，结构为：

```text
输入 [B, 30, feature_dim]
    -> transpose 成 [B, feature_dim, 30]
    -> 多层 TemporalBlock
    -> AvgPool1d 聚合时间维
    -> Linear 分类头
    -> logits [B, num_classes]
```

默认通道配置：

```text
128,128,128
```

每个 TemporalBlock 包含：

```text
Conv1d
BatchNorm1d
ReLU
Dropout
Conv1d
BatchNorm1d
ReLU
Dropout
Residual connection
```

使用 TCN 的原因：

1. Conv1d 对 RKNN 更友好。
2. 比 RNN/GRU 更容易导出和部署。
3. 能通过 dilation 捕获短时动作变化。
4. 模型小，适合 RK3588 实时运行。

### 4.2 训练命令

默认训练：

```bash
python3 scripts/train/train_tcn.py \
  --dataset dataset_keypoints \
  --label-map label_map.json \
  --save-best artifacts/models/sign_tcn_best.pth \
  --save-last artifacts/models/sign_tcn_last.pth \
  --epochs 120 \
  --batch-size 16 \
  --feature-mode full
```

训练脚本会：

1. 扫描 `dataset_keypoints` 下的类别文件夹。
2. 按类别名排序生成 `label_map.json`。
3. 使用 `train_test_split(..., stratify=labels)` 划分训练/验证集。
4. 使用 `CrossEntropyLoss` 和 `AdamW` 训练。
5. 使用 `ReduceLROnPlateau` 根据验证准确率降低学习率。
6. 保存最佳模型和最后模型。
7. 输出混淆矩阵和分类报告。

训练输出：

```text
artifacts/models/sign_tcn_best.pth
artifacts/models/sign_tcn_last.pth
label_map.json
artifacts/models/sign_tcn_confusion_matrix.npy
artifacts/models/sign_tcn_classification_report.txt
```

### 4.3 标签映射

`label_map.json` 是类别索引和中文词汇之间的映射，训练、推理、Qt 展示都依赖它。

它的顺序由数据集目录排序得到，例如：

```json
[
  "发烧",
  "咳嗽",
  "头",
  "我",
  "昨天",
  "疼痛"
]
```

注意：换新模型时必须同步更新 `.rknn` 和对应的 `label_map.json`，否则模型输出索引会映射到错误词汇。

## 5. ONNX 与 RKNN 部署链路

### 5.1 导出 ONNX

入口：

```text
scripts/export/export_tcn_onnx.py
```

命令：

```bash
python3 scripts/export/export_tcn_onnx.py \
  --weights artifacts/models/sign_tcn_best.pth \
  --labels label_map.json \
  --output artifacts/models/sign_tcn_best.onnx \
  --feature-mode full \
  --opset 13
```

导出后会检查：

1. ONNX 模型结构合法。
2. 如果安装了 `onnxruntime`，会比较 PyTorch 和 ONNX 输出误差。

ONNX 输入输出：

```text
Input:  keypoints float32 [1, 30, 258]
Output: logits float32 [1, num_classes]
```

### 5.2 转换 RKNN

入口：

```text
scripts/export/convert_tcn_rknn.py
```

命令：

```bash
python3 scripts/export/convert_tcn_rknn.py \
  --onnx artifacts/models/sign_tcn_best.onnx \
  --output artifacts/models/sign_tcn_best.rknn \
  --feature-mode full \
  --target rk3588
```

当前转换策略：

```text
do_quantization=False
target_platform=rk3588
optimization_level=3
```

该模型属于固定 shape 模型，输入尺寸在转换阶段指定：

```text
[[1, 30, input_size]]
```

转换脚本会检查 `onnx.mapping` 是否存在。这个检查是为了避免 RKNN Toolkit2 2.1.0 与过高版本 ONNX 不兼容。已验证较稳的组合是：

```text
rknn-toolkit2 2.1.0
onnx 1.14.1
opset 13
```

## 6. 板端实时推理链路

### 6.1 运行入口

实时推理入口：

```text
scripts/infer/infer_realtime_rknn.py
```

Qt 不让 Python 直接打开摄像头，而是使用：

```text
--frame-stdin
```

即 Qt 负责采集摄像头、预览显示和 JPEG 编码，Python worker 从 stdin 接收 JSON 帧。

Qt 当前启动参数由 `SignController::workerArguments()` 生成：

```text
python3 scripts/infer/infer_realtime_rknn.py
  --model artifacts/models/sign_tcn_best.rknn
  --labels label_map.json
  --feature-mode full
  --camera 21
  --width 1280
  --height 720
  --pose-interval 4
  --hand-interval 2
  --json-interval 0.05
  --frame-stdin
  --composer rkllm
  --rkllm-url http://127.0.0.1:8080/rkllm_chat
  --llm-timeout 8
  --no-window
```

### 6.2 Qt 摄像头采集

Qt 侧采集位于：

```text
SignController::capturePreviewFrames()
```

当前摄像头配置：

```text
/dev/video21
1280x720
30 FPS
V4L2 backend
buffer size = 1
```

Qt 每路帧有两个用途：

1. 缩放到约 `960` 宽用于患者端和医生端预览。
2. 在“开始识别”后，按约 50 ms 间隔编码 JPEG 发给 Python worker。

为了避免 Python 或 stdin 堵塞，Qt 会检查：

```text
worker_.bytesToWrite() <= 512 KB
```

如果积压过多，就跳过本帧，避免 UI 被拖死。

### 6.3 stdin 帧协议

Qt 发给 Python 的帧是 JSON 行：

```json
{
  "cmd": "frame",
  "image": "<base64 encoded jpeg>"
}
```

Python 中 `StdinCommandReader` 独立线程读取 stdin：

1. 解析 JSON 行。
2. base64 解码 JPEG。
3. `cv2.imdecode()` 还原为 BGR 图像。
4. 只保存最新帧，推理主循环总是取最新帧，避免处理过期帧。

Qt 也通过 stdin 发送控制命令：

```json
{"cmd":"start_recording"}
{"cmd":"stop_recording"}
{"cmd":"delete_last"}
{"cmd":"clear"}
```

### 6.4 MediaPipe 分频运行

MediaPipe Pose 和 Hand 是实时链路的主要 CPU 开销，因此当前使用分频策略：

```text
pose-interval = 4
hand-interval = 2
```

含义：

1. Pose 不每帧跑，而是每 4 帧运行一次，其余帧复用上一次结果。
2. Hand 每 2 帧运行一次，其余帧复用上一次结果。

这样可以保持摄像头预览流畅，同时保证关键点更新频率仍可支撑手语识别。

### 6.5 时序窗口与进度

实时推理同样使用：

```text
SEQ_LEN = 30
WINDOW_SEC = 1.5
```

用户点击“开始”后才进入 recording 状态。系统在检测到有效手部后开始累计关键点序列。

当前还加入了短暂丢手容忍：

```text
HAND_LOST_GRACE_SEC = 0.35
```

效果：

1. MediaPipe 偶发丢手不会立刻清空序列。
2. 短暂丢手只暂停追加关键点帧。
3. 超过容忍时间仍无有效手，才清空序列。

采集进度由 `sequence_frames` / `window_sec` 反映，当稳定识别到词汇后，Qt 视觉上会将进度置为 100%，让患者知道当前词已经完成。

### 6.6 RKNN 推理

Python worker 加载：

```text
artifacts/models/sign_tcn_best.rknn
```

推理流程：

```text
关键点序列
    -> prepare_sequence_array()
    -> float32 [1, 30, 258]
    -> RKNNLite inference
    -> logits [1, num_classes]
    -> softmax
    -> top class + confidence
```

当前稳定输出策略：

```text
CONF_THRESHOLD = 0.72
SMOOTH_WINDOW = 6
MIN_VOTE_COUNT = 4
TOKEN_ADD_INTERVAL = 0.8
MIN_HAND_ACTIVE_FRAMES = 12
```

作用：

1. 置信度低于阈值时显示“识别中...”，不立刻加入词汇。
2. 最近多次预测做投票，减少单帧抖动。
3. 同一词汇加入有最小间隔，避免一个动作重复刷入多个词。
4. 需要足够多有效手部帧后才允许稳定识别。

### 6.7 JSON 输出协议

Python worker 输出 JSON Lines 到 stdout，Qt 解析后更新 UI。

主要事件：

| type | 说明 |
| --- | --- |
| `ready` | worker 初始化完成 |
| `recording` | 开始/停止采集状态变化 |
| `result` | 实时识别结果、置信度、延迟、进度、tokens |
| `tokens` | 当前已收集词汇变化 |
| `sentence` | 停止后整理出的句子 |
| `warning` | worker 警告信息 |

`result` 中常用字段：

```json
{
  "type": "result",
  "text": "咳嗽",
  "confidence": 0.91,
  "fps": 12.3,
  "tokens": ["我", "咳嗽"],
  "sequence_frames": 18,
  "window_sec": 1.2,
  "hands_valid": true,
  "tracking_status": "追踪中"
}
```

Qt 侧在 `SignController::consumeJson()` 中消费这些事件，并更新：

```text
currentWord
confidenceText
fpsText
latencyText
tokensText
sentenceText
composerText
collectProgress
statusText
```

## 7. 句子整理链路

实时识别得到的是词汇序列，例如：

```text
我 / 昨天 / 头 / 疼痛
```

停止后需要整理为自然主诉：

```text
我昨天头疼。
```

### 7.1 规则兜底

规则整理位于：

```text
scripts/common/sentence_composer.py
```

规则会提取：

1. 时间：今天、昨天、现在、明天、从 X 到 Y。
2. 人称：默认“我”。
3. 症状：发烧、咳嗽、拉肚子、流鼻涕等。
4. 复合症状：头 + 疼痛、鼻 + 流血、鼻 + 疼痛等。
5. 程度/否定：不、经常、不经常。
6. 持续时间：1 到 9 + 小时/天。

规则整理更可控，不会把“鼻流血”改成“头痛”，所以它是当前系统的安全兜底。

### 7.2 RKLLM 润色

当前 RKLLM 使用 DeepSeek-R1-Distill-Qwen-1.5B 的 RKLLM 版本，通过本地 Flask 服务提供接口：

```text
http://127.0.0.1:8080/rkllm_chat
```

启动脚本：

```bash
cd ~/rkllm_server
python3 flask_server.py \
  --rkllm_model_path /home/elf/models/DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm \
  --target_platform rk3588
```

实际运行时 `scripts/run_qml_with_ai.sh` 会自动检查端口，如果 RKLLM 服务未启动，就启动它。

### 7.3 LLM 输出过滤

板端 1.5B LLM 对极短词汇输入不总是稳定，可能输出解释、英文、模板符号或错误症状。

因此当前策略是：

```text
先尝试 RKLLM
    -> 清理特殊 token 和 think 输出
    -> 校验是否包含英文、解释性词、医生/用户/系统等词
    -> 校验时间、症状、部位、持续时间是否仍匹配输入 tokens
    -> 合格则使用 RKLLM
    -> 不合格则退回规则句子
```

Qt 日志中会显示整理来源：

```text
sentence composer: rkllm
sentence composer: rule_fallback
```

如果看到 `rule_fallback`，说明 RKLLM 输出被过滤，系统使用了规则兜底结果。

## 8. Qt/QML 医患交互链路

### 8.1 双屏结构

QML 主界面位于：

```text
qt_qml_app/qml/main.qml
```

当前面向 HDMI + DSI 双屏：

| 屏幕 | 用途 |
| --- | --- |
| DSI | 患者端，显示摄像头画面、识别状态、医生回复 |
| HDMI | 医生端，显示电子病历工作台、档案页、设置页 |

`qt_qml_app/main.cpp` 创建 `SignController`，并将它注入 QML：

```cpp
engine.rootContext()->setContextProperty("signController", &controller);
```

QML 通过 `signController` 调用 C++：

```text
startSystem()
stopSystem()
startRecording()
stopRecording()
deleteLastToken()
clearExpression()
saveMedicalRecord()
exportMedicalRecord()
loadMedicalRecords()
deleteMedicalRecord()
```

### 8.2 患者端

患者端负责：

1. 显示实时摄像头画面。
2. 显示识别引导框。
3. 显示当前状态：等待、识别中、识别完成。
4. 显示当前识别词和整理结果。
5. 显示采集进度条。
6. 显示医生端发送的文字回复。

患者主要操作：

```text
开始
结束
删除
清空
```

### 8.3 医生端首页

医生端首页已经调整为电子病历工作台，而不是 AI 调试控制台。

主要区域：

1. 当前患者主诉确认区。
2. 写入病历主按钮。
3. 重新整理、新建病历、清空内容辅助按钮。
4. 医生回复输入框。
5. A4 病历纸预览。

医生点击“写入病历”后，当前主诉会追加到病历纸“患者主诉”区域，不覆盖之前内容。

### 8.4 病历保存与导出

病历相关逻辑位于：

```text
SignController::saveMedicalRecord()
SignController::exportMedicalRecord()
SignController::loadMedicalRecords()
SignController::deleteMedicalRecord()
```

保存目录：

```text
artifacts/records
```

保存病历时生成：

```text
record_yyyyMMdd_HHmmss.json
record_yyyyMMdd_HHmmss.txt
```

导出病历时生成：

```text
record_yyyyMMdd_HHmmss_export.txt
```

同时会更新原 JSON 记录状态为：

```text
已导出
```

如果病历尚未保存，导出会被拒绝，并提示先保存。

### 8.5 医生回复患者

医生端首页左下角提供“医生回复”输入区。

流程：

```text
医生输入文字
    -> 点击发送或按 Enter
    -> QML 追加到 doctorReplyList
    -> 患者端显示最近医生回复
```

该功能用于听障患者读取医生反馈，例如：

```text
请您再重复一遍刚才的手语。
请把手放在摄像头画面中间。
```

## 9. 一键启动链路

入口脚本：

```text
scripts/run_qml_with_ai.sh
```

常用启动方式：

```bash
~/1.sh
```

或直接：

```bash
cd ~/code/rkSignLanguage
./scripts/run_qml_with_ai.sh
```

脚本会做：

1. 检查 QML 程序是否存在，不存在则编译 `SignLanguageQml`。
2. 设置 DSI 背光亮度。
3. 设置并启动 `fcitx5` 中文输入法。
4. 检查 RKLLM Flask 服务端口。
5. 未启动 RKLLM 时自动启动服务。
6. 设置 Qt Quick 软件渲染环境：

```text
QT_QUICK_BACKEND=software
QSG_RENDER_LOOP=basic
```

7. 启动 QML 应用。

如果 QML 改动后发现板端界面没有变化，建议全量重建：

```bash
cd ~/code/rkSignLanguage
rm -rf build
cmake -S . -B build
cmake --build build --target SignLanguageQml -j4
~/1.sh
```

## 10. 扩充新词汇的标准流程

新增词汇建议按以下顺序：

### 10.1 采集数据

```bash
python3 scripts/data/collect_keypoints_tasks.py \
  --label 新词汇 \
  --camera 21 \
  --save_root dataset_keypoints \
  --record_sec 1.5 \
  --seq_len 30 \
  --require_hand
```

建议每个词汇采集多个样本，并覆盖：

1. 动作快慢变化。
2. 手部位置轻微变化。
3. 不同距离和站姿。
4. 容易混淆动作的对比样本，例如“头”和“鼻”。

### 10.2 训练

```bash
python3 scripts/train/train_tcn.py \
  --dataset dataset_keypoints \
  --feature-mode full \
  --epochs 120 \
  --batch-size 16
```

检查输出的：

```text
sign_tcn_classification_report.txt
sign_tcn_confusion_matrix.npy
```

重点看相似动作是否混淆。

### 10.3 导出 ONNX

```bash
python3 scripts/export/export_tcn_onnx.py \
  --weights artifacts/models/sign_tcn_best.pth \
  --labels label_map.json \
  --output artifacts/models/sign_tcn_best.onnx \
  --feature-mode full \
  --opset 13
```

### 10.4 转换 RKNN

```bash
python3 scripts/export/convert_tcn_rknn.py \
  --onnx artifacts/models/sign_tcn_best.onnx \
  --output artifacts/models/sign_tcn_best.rknn \
  --feature-mode full \
  --target rk3588
```

### 10.5 同步到板端

至少同步：

```text
artifacts/models/sign_tcn_best.rknn
label_map.json
scripts/
qt_qml_app/
```

如果只是换模型和标签，理论上只需同步 `.rknn` 与 `label_map.json`；如果同时改了推理或 UI 代码，则需要同步代码并重新编译 QML。

## 11. 常见问题与排查

### 11.1 代码同步后界面没变化

原因通常是 QML 资源被编译进二进制，旧 build 没有完全更新。

处理：

```bash
cd ~/code/rkSignLanguage
rm -rf build
cmake -S . -B build
cmake --build build --target SignLanguageQml -j4
~/1.sh
```

### 11.2 中文输入法不能用

先检查 `fcitx5` 是否启动：

```bash
pgrep -a fcitx5
```

启动脚本已设置：

```text
GTK_IM_MODULE=fcitx
QT_IM_MODULE=fcitx
XMODIFIERS=@im=fcitx
```

并会尝试自动执行：

```bash
fcitx5 -d
```

### 11.3 摄像头变亮但变卡

手动曝光值过高会让单帧曝光时间变长，导致实际帧率下降。当前摄像头可通过 `v4l2-ctl` 查看范围：

```bash
v4l2-ctl -d /dev/video21 --list-ctrls
```

当前设备曾显示：

```text
exposure_absolute min=1 max=5000 default=157
```

建议优先在 200 到 600 附近测试，不要盲目拉到 1600 以上。

### 11.4 精度突然下降

优先排查：

1. 当前 `.rknn` 是否和 `label_map.json` 匹配。
2. `SIGN_FEATURE_MODE` 是否和训练/转换时一致。
3. 采集时是否不翻转，推理时是否也不翻转。
4. 输入给 Python 的识别帧分辨率是否变化太大。
5. `sequence_frames` 是否经常只有十几帧，动作窗口是否不足。
6. 类似动作是否缺少对比样本，例如“头”和“鼻”都靠手指指部位。

### 11.5 RKLLM 经常 rule_fallback

这是正常保护机制。说明 RKLLM 输出没有通过安全校验，系统自动使用规则句子。

如果要提高 RKLLM 使用率，需要：

1. 降低 prompt 难度。
2. 增加规则生成的“保底句”质量。
3. 不允许 LLM 改症状、改部位、删时间。
4. 保留规则兜底，避免医疗语义错误。

## 12. 当前交付状态

当前项目已经形成可演示闭环：

```text
数据采集
    -> 关键点样本
    -> TCN 训练
    -> ONNX/RKNN 部署
    -> RK3588 实时词汇识别
    -> RKLLM/规则主诉整理
    -> 患者端显示
    -> 医生端写入病历
    -> 保存/导出/档案管理
```

后续主要工作不再是打通链路，而是：

1. 扩充词汇量到 30 到 50 个常用门诊词。
2. 对相似词做专项采集和混淆矩阵分析。
3. 打磨医生端病历导出格式。
4. 提升多人场景鲁棒性，但建议单独分支实验，避免影响稳定演示版本。
