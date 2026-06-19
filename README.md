# rkSignLanguage

**中文** | [English](README_EN.md)

面向 RK3588 开发板的医院场景手语交互原型系统。项目把摄像头实时画面、MediaPipe 关键点提取、TCN 手语词汇识别、RKNN NPU 推理、RKLLM 句子整理和 Qt/QML 双屏交互界面串成一条可演示链路。

当前项目重点是帮助听障患者在门诊场景中表达基础主诉：患者通过手语表达词汇，系统识别为词序列，再整理成医生更容易阅读的一句话。

## 当前状态

当前稳定演示主线：

```text
Qt/QML 双屏界面
    -> Qt 采集摄像头并显示患者端实时画面
    -> Qt 按需发送 JPEG 帧到 Python worker
    -> Python MediaPipe Pose + Hand 提取关键点
    -> TCN RKNN 在 RK3588 NPU 上完成词汇识别
    -> RKLLM / 规则兜底整理患者主诉
    -> Qt 患者端和医生端同步显示结果
```

经过 C++ MediaPipe 路线探索后，当前演示版暂定为：

- **Python**：MediaPipe、RKNN 推理、词汇识别、句子整理。
- **Qt/QML**：摄像头预览、患者端触摸操作、医生端电子病例式界面、双屏显示。
- **C++ pipeline**：保留为后续高性能路线和硬件管线探索。

## 功能特点

- 患者端显示实时摄像头画面、ROI 框、当前状态、识别词汇、采集进度和大按钮。
- 医生端显示电子病例风格界面，包括患者主诉、识别词流、症状信息和系统状态。
- 支持 HDMI + DSI 双屏：
  - DSI：患者端，适合触摸操作。
  - HDMI：医生端，适合展示电子病例信息。
- 支持 `开始 / 结束 / 删除 / 清空` 交互：
  - `删除`：删除最后一个识别词，便于误识别后继续表达。
  - `清空`：清空当前整句话。
- 支持 RKLLM 句子整理，异常输出时自动回退到规则整理。
- 支持一键启动脚本，自动启动 RKLLM Flask 服务和 QML UI。

## 项目结构

```text
qt_qml_app/          Qt/QML 双屏界面和本地摄像头预览
scripts/infer/       Python 实时识别 worker，MediaPipe + RKNN + RKLLM
scripts/data/        关键点采集和数据集查看工具
scripts/train/       GRU / TCN 训练脚本
scripts/export/      ONNX / RKNN 导出脚本
scripts/eval/        ONNX / RKNN 评估和对比脚本
scripts/deploy/      板端同步和运行环境检查脚本
scripts/common/      特征处理、模型结构、句子整理通用逻辑
pipeline/            C++ 管线探索：V4L2、RGA、RKNN、MediaPipe wrapper 接口
concurrency/         C++ 线程工具
models/              MediaPipe task 模型
artifacts/           本地生成模型、日志和评估结果，默认不进入 Git
docs/                项目进度和调试记录
```

## 快速运行

板端推荐使用一键启动脚本：

```bash
cd ~/code/rkSignLanguage
./scripts/run_qml_with_ai.sh
```

如果你在家目录里放了自己的启动脚本，例如 `~/game.sh` 或 `~/1.sh`，也可以让它调用上面的脚本。

脚本会：

1. 检查并启动 RKLLM Flask 服务。
2. 设置 Qt Quick 软件渲染环境。
3. 启动 QML 双屏界面。
4. 在退出 UI 时停止本次脚本拉起的 RKLLM 服务。

关键环境变量：

```bash
export DISPLAY=:0.0
export QT_QUICK_BACKEND=software
export QSG_RENDER_LOOP=basic
```

## 构建 QML UI

```bash
cmake -S . -B build
cmake --build build --target SignLanguageQml -j4
```

运行：

```bash
export DISPLAY=:0.0
./build/qt_qml_app/SignLanguageQml
```

如果只修改了 QML 文件但板端看不到变化，可以刷新资源时间戳后重新构建：

```bash
touch qt_qml_app/qml/main.qml qt_qml_app/qml/BigButton.qml qt_qml_app/resources.qrc
cmake --build build --target SignLanguageQml -j4
```

## Python 实时识别

Qt/QML 版本中，Python worker 使用 `--frame-stdin`，不直接打开摄像头。Qt 负责摄像头预览并把缩放后的 JPEG 帧送给 Python：

```bash
python3 scripts/infer/infer_realtime_rknn.py \
  --model artifacts/models/sign_tcn_best.rknn \
  --labels label_map.json \
  --width 1280 \
  --height 720 \
  --pose-interval 4 \
  --hand-interval 2 \
  --frame-stdin \
  --composer rkllm \
  --rkllm-url http://127.0.0.1:8080/rkllm_chat \
  --no-window
```

也可以直接用 Python 打开摄像头调试识别链路：

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

## RKLLM 服务

当前句子整理后端使用 DeepSeek-R1-Distill-Qwen-1.5B 的 RKLLM 模型。服务启动示例：

```bash
cd ~/rkllm_server

python3 flask_server.py \
  --rkllm_model_path /home/elf/models/DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm \
  --target_platform rk3588
```

服务地址：

```text
http://127.0.0.1:8080/rkllm_chat
```

系统会优先调用 RKLLM 整理句子；如果 LLM 超时、输出异常或出现解释型文本，会自动回退到本地规则整理，保证演示流程不中断。

## 数据采集

采集 MediaPipe Pose + Hand 关键点：

```bash
python3 scripts/data/collect_keypoints_tasks.py --label 咳嗽 --camera 21
```

采集数据默认保存为 `.npy` 序列，模型输入契约为：

```text
float32 [1, 30, 258]
```

其中：

- Pose：33 个点，每点 `x, y, z, visibility`
- Left hand：21 个点，每点 `x, y, z`
- Right hand：21 个点，每点 `x, y, z`

## 训练和导出

训练 TCN：

```bash
python3 scripts/train/train_tcn.py \
  --dataset dataset_keypoints \
  --label-map label_map.json \
  --save-best artifacts/models/sign_tcn_best.pth
```

导出 ONNX：

```bash
python3 scripts/export/export_tcn_onnx.py \
  --weights artifacts/models/sign_tcn_best.pth \
  --labels label_map.json \
  --output artifacts/models/sign_tcn_best.onnx
```

转换 RKNN：

```bash
python3 scripts/export/convert_tcn_rknn.py \
  --onnx artifacts/models/sign_tcn_best.onnx \
  --output artifacts/models/sign_tcn_best.rknn \
  --target rk3588
```

评估 ONNX / RKNN：

```bash
python3 scripts/eval/eval_sequence_batch.py \
  --backend onnx \
  --model artifacts/models/sign_tcn_best.onnx \
  --dataset dataset_keypoints \
  --labels label_map.json \
  --output artifacts/eval/tcn_onnx_eval.json

python3 scripts/eval/eval_sequence_batch.py \
  --backend rknn \
  --model artifacts/models/sign_tcn_best.rknn \
  --dataset dataset_keypoints \
  --labels label_map.json \
  --output artifacts/eval/tcn_rknn_eval.json
```

## 当前交付计划

短期目标：

- 打磨医生端 UI 和患者端交互细节。
- 将词汇量扩展到 30 到 50 个常见门诊词。
- 优先保证识别稳定性，而不是盲目增加词汇数量。

建议优先词汇方向：

- 主语/时间：我、今天、昨天、刚才、一直
- 症状：头痛、咳嗽、发烧、胸闷、恶心、腹痛、嗓子痛、头晕
- 程度：轻微、严重、很、没有
- 就诊需求：医生、药、检查、帮助、谢谢

## 备注

- 当前演示主线依赖 RK3588 板端运行环境，普通 PC 上无法完整复现 RKNN / RKLLM 推理。
- C++ MediaPipe wrapper 已做过探索，但完整接入成本较高，当前不作为主演示路线。
- 详细阶段记录见 [docs/current-progress.md](docs/current-progress.md)。
