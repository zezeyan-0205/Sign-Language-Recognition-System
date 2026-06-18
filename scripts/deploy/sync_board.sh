#!/usr/bin/env bash
set -euo pipefail

BOARD_HOST="${1:-elf@192.168.1.10}"
BOARD_DIR="${2:-~/code/rkSignLanguage}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "Sync project to RK3588 board"
echo "  local : ${PROJECT_ROOT}"
echo "  remote: ${BOARD_HOST}:${BOARD_DIR}"

rsync -avz --delete \
  --exclude '.git/' \
  --exclude '.codex/' \
  --exclude '.agents/' \
  --exclude '.vscode/' \
  --exclude '.VSCodeCounter/' \
  --exclude 'build/' \
  --exclude '__pycache__/' \
  --exclude '*.pyc' \
  --exclude 'venv_sign/' \
  --exclude 'dataset_keypoints/' \
  --exclude 'artifacts/debug_onnx/' \
  --exclude 'artifacts/eval/' \
  --exclude 'artifacts/samples/' \
  --exclude 'artifacts/captures/' \
  --exclude 'artifacts/models/*.pth' \
  --exclude 'artifacts/models/*.onnx' \
  --exclude 'artifacts/models/sign_gru_*' \
  --include 'artifacts/' \
  --include 'artifacts/models/' \
  --include 'artifacts/models/sign_tcn_best.rknn' \
  --include 'models/' \
  --include 'models/hand_landmarker.task' \
  --include 'models/pose_landmarker.task' \
  "${PROJECT_ROOT}/" \
  "${BOARD_HOST}:${BOARD_DIR}/"

echo
echo "Done. Board build example:"
echo "  ssh ${BOARD_HOST}"
echo "  cd ${BOARD_DIR}"
echo "  cmake -S . -B build -DMEDIAPIPE_ROOT=\$HOME/mediapipe-0.10.18"
echo "  cmake --build build -j4"
