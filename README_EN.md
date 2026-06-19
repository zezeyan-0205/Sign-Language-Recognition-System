# rkSignLanguage

[中文](README.md) | **English**

A hospital-oriented sign-language interaction prototype for RK3588 boards. The project connects live camera preview, MediaPipe keypoint extraction, TCN-based sign-word recognition, RKNN NPU inference, RKLLM sentence composition, and a Qt/QML dual-screen user interface into one board-side demo pipeline.

The current goal is to help hearing-impaired patients express basic outpatient complaints. The patient signs a sequence of words, the system recognizes the words, and the result is composed into a short clinical sentence for the doctor.

## Current Status

Current stable demo pipeline:

```text
Qt/QML dual-screen UI
    -> Qt captures camera frames and renders the patient-side preview
    -> Qt sends JPEG frames to a Python worker only when recognition is active
    -> Python MediaPipe Pose + Hand extracts keypoints
    -> TCN RKNN runs word recognition on the RK3588 NPU
    -> RKLLM / rule fallback composes the patient complaint
    -> Qt updates both patient and doctor screens
```

After exploring a C++ MediaPipe path, the current demo is organized as:

- **Python**: MediaPipe, RKNN inference, word recognition, sentence composition.
- **Qt/QML**: camera preview, patient-side touch controls, doctor-side medical-record UI, dual-screen layout.
- **C++ pipeline**: kept as a future high-performance and hardware-pipeline exploration path.

## Features

- Patient screen with live camera preview, ROI box, current status, recognized words, collection progress, and large touch-friendly buttons.
- Doctor screen with an electronic-medical-record style view, including chief complaint, recognized word stream, symptoms, and system status.
- HDMI + DSI dual-screen layout:
  - DSI: patient screen with touch controls.
  - HDMI: doctor screen for medical-record information.
- Patient controls: `Start / Finish / Delete / Clear`.
  - `Delete`: remove the last recognized word and continue the current sentence.
  - `Clear`: clear the whole sentence.
- RKLLM sentence composition with rule-based fallback for invalid or unstable LLM output.
- One-command launch script for RKLLM Flask service and the QML UI.

## Repository Layout

```text
qt_qml_app/          Qt/QML dual-screen UI and local camera preview
scripts/infer/       Python realtime worker, MediaPipe + RKNN + RKLLM
scripts/data/        Keypoint collection and dataset inspection tools
scripts/train/       GRU / TCN training scripts
scripts/export/      ONNX / RKNN export scripts
scripts/eval/        ONNX / RKNN evaluation and comparison scripts
scripts/deploy/      Board sync and runtime check scripts
scripts/common/      Feature processing, model definitions, sentence composer
pipeline/            C++ pipeline exploration: V4L2, RGA, RKNN, MediaPipe wrapper API
concurrency/         C++ threading utilities
models/              MediaPipe task models
artifacts/           Local model, log, and evaluation outputs, ignored by Git
docs/                Progress and debugging notes
```

## Quick Start

Recommended board-side launch command:

```bash
cd ~/code/rkSignLanguage
./scripts/run_qml_with_ai.sh
```

If you keep a shortcut such as `~/game.sh` or `~/1.sh`, let it call the script above.

The script:

1. Checks and starts the RKLLM Flask service.
2. Sets Qt Quick software-rendering environment variables.
3. Starts the QML dual-screen UI.
4. Stops the RKLLM server started by this script when the UI exits.

Important environment variables:

```bash
export DISPLAY=:0.0
export QT_QUICK_BACKEND=software
export QSG_RENDER_LOOP=basic
```

## Build the QML UI

```bash
cmake -S . -B build
cmake --build build --target SignLanguageQml -j4
```

Run:

```bash
export DISPLAY=:0.0
./build/qt_qml_app/SignLanguageQml
```

If QML changes do not appear on the board, refresh the resource timestamps and rebuild:

```bash
touch qt_qml_app/qml/main.qml qt_qml_app/qml/BigButton.qml qt_qml_app/resources.qrc
cmake --build build --target SignLanguageQml -j4
```

## Python Realtime Recognition

In the Qt/QML flow, the Python worker runs with `--frame-stdin` and does not open the camera by itself. Qt owns the camera preview and sends resized JPEG frames to Python:

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

You can also debug the recognition pipeline directly with a camera:

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

## RKLLM Service

The current sentence composer uses a DeepSeek-R1-Distill-Qwen-1.5B RKLLM model:

```bash
cd ~/rkllm_server

python3 flask_server.py \
  --rkllm_model_path /home/elf/models/DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm \
  --target_platform rk3588
```

Service endpoint:

```text
http://127.0.0.1:8080/rkllm_chat
```

The system calls RKLLM first. If the model times out, returns invalid text, or produces explanatory output, the worker falls back to a local rule-based composer so the demo flow can continue.

## Data Collection

Collect MediaPipe Pose + Hand keypoints:

```bash
python3 scripts/data/collect_keypoints_tasks.py --label cough --camera 21
```

The model input contract is:

```text
float32 [1, 30, 258]
```

Feature layout:

- Pose: 33 landmarks, each with `x, y, z, visibility`
- Left hand: 21 landmarks, each with `x, y, z`
- Right hand: 21 landmarks, each with `x, y, z`

## Training and Export

Train TCN:

```bash
python3 scripts/train/train_tcn.py \
  --dataset dataset_keypoints \
  --label-map label_map.json \
  --save-best artifacts/models/sign_tcn_best.pth
```

Export ONNX:

```bash
python3 scripts/export/export_tcn_onnx.py \
  --weights artifacts/models/sign_tcn_best.pth \
  --labels label_map.json \
  --output artifacts/models/sign_tcn_best.onnx
```

Convert to RKNN:

```bash
python3 scripts/export/convert_tcn_rknn.py \
  --onnx artifacts/models/sign_tcn_best.onnx \
  --output artifacts/models/sign_tcn_best.rknn \
  --target rk3588
```

Evaluate ONNX / RKNN:

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

## Delivery Plan

Short-term goals:

- Polish the doctor-side UI and patient-side interaction details.
- Expand the vocabulary to about 30 to 50 common outpatient words.
- Prioritize recognition stability over vocabulary size.

Suggested vocabulary groups:

- Subject/time: I, today, yesterday, just now, continuously
- Symptoms: headache, cough, fever, chest tightness, nausea, abdominal pain, sore throat, dizziness
- Degree: mild, severe, very, no
- Medical needs: doctor, medicine, examination, help, thanks

## Notes

- The full demo depends on the RK3588 board runtime. A normal PC cannot fully reproduce RKNN / RKLLM inference.
- The C++ MediaPipe wrapper has been explored, but it is not the main demo route at the moment.
- See [docs/current-progress.md](docs/current-progress.md) for detailed progress notes.
