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

Evaluate the TCN with the same batch evaluator:

```bash
python3 scripts/eval/eval_sequence_batch.py \
  --backend onnx \
  --model artifacts/models/sign_tcn_best.onnx \
  --dataset dataset_keypoints \
  --labels label_map.json \
  --output artifacts/eval/tcn_onnx_eval.json
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
