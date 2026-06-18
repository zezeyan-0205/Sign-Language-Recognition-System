# rkSignLanguage

RK3588 hospital sign-language interaction prototype.

## Project Layout

```text
src/                  C++ entry point
pipeline/             V4L2 capture, RGA preprocessing, pipeline stages
concurrency/          Threading utilities
models/               Runtime model assets such as MediaPipe task files
scripts/data/         Keypoint collection and dataset inspection scripts
scripts/train/        Training scripts
scripts/infer/        Realtime inference scripts
scripts/export/       ONNX and RKNN export scripts
scripts/eval/         RKNN/ONNX validation and comparison scripts
docs/                 Project progress notes
artifacts/            Local generated files, ignored by Git
```

## Common Commands

Build the C++ demo on the board:

```bash
cmake -S . -B build
cmake --build build
```

Run with a USB camera:

```bash
export DISPLAY=:0.0
./build/SignLanguagePipeline /dev/video21 300
```

Run the C++ pipeline with the TCN RKNN recognizer enabled:

```bash
./build/SignLanguagePipeline \
  /dev/video21 \
  300 \
  artifacts/models/sign_tcn_best.rknn \
  label_map.json
```

The current C++ keypoint stage still creates mock per-frame 258-D features, but
`sequenceLoop()` now uses `KeypointSequenceBuffer` to cache and resample them
into the TCN input shape `[1, 30, 258]`, then applies the same shoulder-center
and shoulder-width normalization used by the Python training pipeline. Replace
the mock feature producer with real C++ MediaPipe Pose + Hand keypoints after
the C++ extractor is connected.

`MediaPipeKeypointExtractor` is now wired into `keypointLoop()` as the keypoint
source interface. If C++ MediaPipe support is not enabled at build time, the
pipeline falls back to mock 258-D features and keeps the C++ demo runnable.

Convert GRU ONNX to RKNN:

```bash
python3 scripts/export/convert_gru_rknn.py \
  --onnx artifacts/models/sign_gru_best.onnx \
  --output artifacts/models/sign_gru_best.rknn \
  --target rk3588
```

Evaluate RKNN on the board:

```bash
python3 scripts/eval/eval_sequence_batch.py \
  --backend rknn \
  --model artifacts/models/sign_gru_best.rknn \
  --dataset dataset_keypoints \
  --labels label_map.json \
  --output artifacts/eval/rknn_eval.json
```

Train the RKNN-friendly TCN baseline:

```bash
python3 scripts/train/train_tcn.py \
  --dataset dataset_keypoints \
  --label-map label_map.json \
  --save-best artifacts/models/sign_tcn_best.pth
```

Train a smaller upper-body + hand-xy TCN variant:

```bash
python3 scripts/train/train_tcn.py \
  --dataset dataset_keypoints \
  --label-map label_map.json \
  --feature-mode upper_body_hands_xy \
  --save-best artifacts/models/sign_tcn_upper_xy_best.pth \
  --save-last artifacts/models/sign_tcn_upper_xy_last.pth
```

Export and convert the TCN model:

```bash
python3 scripts/export/export_tcn_onnx.py \
  --weights artifacts/models/sign_tcn_best.pth \
  --labels label_map.json \
  --output artifacts/models/sign_tcn_best.onnx

python3 scripts/export/convert_tcn_rknn.py \
  --onnx artifacts/models/sign_tcn_best.onnx \
  --output artifacts/models/sign_tcn_best.rknn \
  --target rk3588
```

Export and convert the upper-body + hand-xy model:

```bash
python3 scripts/export/export_tcn_onnx.py \
  --weights artifacts/models/sign_tcn_upper_xy_best.pth \
  --labels label_map.json \
  --feature-mode upper_body_hands_xy \
  --output artifacts/models/sign_tcn_upper_xy_best.onnx

python3 scripts/export/convert_tcn_rknn.py \
  --onnx artifacts/models/sign_tcn_upper_xy_best.onnx \
  --output artifacts/models/sign_tcn_upper_xy_best.rknn \
  --input-size 102 \
  --target rk3588
```

Evaluate the TCN with the same batch evaluator:

```bash
python3 scripts/eval/eval_sequence_batch.py \
  --backend onnx \
  --model artifacts/models/sign_tcn_best.onnx \
  --dataset dataset_keypoints \
  --labels label_map.json \
  --output artifacts/eval/tcn_onnx_eval.json
```

Evaluate a reduced-feature model with the matching feature mode:

```bash
python3 scripts/eval/eval_sequence_batch.py \
  --backend onnx \
  --model artifacts/models/sign_tcn_upper_xy_best.onnx \
  --feature-mode upper_body_hands_xy \
  --dataset dataset_keypoints \
  --labels label_map.json \
  --output artifacts/eval/tcn_upper_xy_onnx_eval.json
```

Evaluate the TCN RKNN model on the board:

```bash
python3 scripts/eval/eval_sequence_batch.py \
  --backend rknn \
  --model artifacts/models/sign_tcn_best.rknn \
  --dataset dataset_keypoints \
  --labels label_map.json \
  --output artifacts/eval/tcn_rknn_eval.json
```

Do not pass `--pass-through` for the TCN RKNN model. On the current RK3588
runtime stack, the TCN model runs correctly without it.

Compare ONNX and RKNN batch results:

```bash
python3 scripts/eval/eval_sequence_batch.py \
  --compare artifacts/eval/tcn_onnx_eval.json \
  --against artifacts/eval/tcn_rknn_eval.json
```

Run realtime camera recognition with the TCN RKNN model on the board:

```bash
cd ~/code/rkSignLanguage
export DISPLAY=:0.0
python3 scripts/infer/infer_realtime_rknn.py
```

Use interval sampling and profiling when tuning performance:

```bash
python3 scripts/infer/infer_realtime_rknn.py \
  --width 640 \
  --height 480 \
  --feature-mode full \
  --pose-interval 5 \
  --hand-interval 2 \
  --profile
```

Current recommended board-side realtime demo command:

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

Run the Python RKNN worker without opening the camera or an OpenCV window for
the Qt frontend. In this mode Qt owns the camera preview and sends resized JPEG
frames to the worker through stdin:

```bash
python3 scripts/infer/infer_realtime_rknn.py \
  --model artifacts/models/sign_tcn_best.rknn \
  --labels label_map.json \
  --width 1280 \
  --height 720 \
  --pose-interval 4 \
  --hand-interval 2 \
  --frame-stdin \
  --no-window
```

Build and run the Qt frontend on the board:

```bash
cmake -S . -B build
cmake --build build -j4
export DISPLAY=:0.0
./build/qt_app/SignLanguageQt
```

`SignLanguageQt` starts the Python worker with `QProcess` and reads JSON Lines
from stdout. Qt owns `/dev/video21`, shows the patient-side preview from a
background OpenCV capture thread, draws the hand ROI on the preview, and sends a
resized JPEG frame to Python about every 30 ms. The worker receives those frames
with `--frame-stdin`, runs MediaPipe + RKNN, and sends recognition state back to
Qt. The patient window shows the live preview, collection progress, and large
recognition text; the doctor window presents an electronic medical-record style
view. The C++ MediaPipe demo path is currently optional; the Qt frontend uses
the validated Python MediaPipe + RKNN pipeline.

The Qt frontend controls sentence collection through JSON commands written to
the worker stdin:

```json
{"cmd":"start_recording"}
{"cmd":"stop_recording"}
{"cmd":"clear"}
```

The worker collects stable recognized tokens between "开始表达" and "结束表达",
then emits a `sentence` JSON event. The current implementation uses a small
offline rule composer; RKLLM or an API LLM can replace this composer later.

For the upper-body + hand-xy model, use the matching feature mode:

```bash
python3 scripts/infer/infer_realtime_rknn.py \
  --model artifacts/models/sign_tcn_upper_xy_best.rknn \
  --feature-mode upper_body_hands_xy \
  --width 640 \
  --height 480 \
  --pose-interval 5 \
  --hand-interval 2 \
  --profile
```
