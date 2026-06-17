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
into the TCN input shape `[1, 30, 258]`. Replace the mock feature producer with
real Pose + Hand keypoints after the C++ keypoint stage is connected.

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
