#!/usr/bin/env bash
set -euo pipefail

SERVER_HOST="${1:-}"
SERVER_DIR="${2:-~/rkSignLanguage_train}"
SSH_PORT="${3:-22}"

if [[ -z "$SERVER_HOST" ]]; then
    echo "Usage:"
    echo "  $0 <user@server_ip> [remote_dir] [ssh_port]"
    echo
    echo "Example:"
    echo "  $0 fzy@192.168.1.100 ~/rkSignLanguage_train 22"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SSH_CMD=(ssh -p "$SSH_PORT")
RSYNC_SSH="ssh -p ${SSH_PORT}"

echo "Sync training files to server"
echo "  local : ${PROJECT_ROOT}"
echo "  remote: ${SERVER_HOST}:${SERVER_DIR}"
echo "  port  : ${SSH_PORT}"
echo

"${SSH_CMD[@]}" "$SERVER_HOST" "mkdir -p ${SERVER_DIR}/dataset_keypoints ${SERVER_DIR}/scripts/common ${SERVER_DIR}/scripts/train ${SERVER_DIR}/scripts/export ${SERVER_DIR}/scripts/eval"

rsync -avz \
  -e "$RSYNC_SSH" \
  --exclude '__pycache__/' \
  --exclude '*.pyc' \
  --exclude '*.pyo' \
  --exclude '.DS_Store' \
  --exclude 'artifacts/logs/' \
  --exclude 'artifacts/records/' \
  --exclude 'artifacts/captures/' \
  --exclude 'artifacts/samples/' \
  --exclude 'artifacts/eval/' \
  --exclude 'artifacts/debug_onnx/' \
  "${PROJECT_ROOT}/dataset_keypoints/" \
  "${SERVER_HOST}:${SERVER_DIR}/dataset_keypoints/"

rsync -avz \
  -e "$RSYNC_SSH" \
  --exclude '__pycache__/' \
  --exclude '*.pyc' \
  --exclude '*.pyo' \
  "${PROJECT_ROOT}/scripts/common/" \
  "${SERVER_HOST}:${SERVER_DIR}/scripts/common/"

rsync -avz \
  -e "$RSYNC_SSH" \
  --exclude '__pycache__/' \
  --exclude '*.pyc' \
  --exclude '*.pyo' \
  "${PROJECT_ROOT}/scripts/train/" \
  "${SERVER_HOST}:${SERVER_DIR}/scripts/train/"

rsync -avz \
  -e "$RSYNC_SSH" \
  --exclude '__pycache__/' \
  --exclude '*.pyc' \
  --exclude '*.pyo' \
  "${PROJECT_ROOT}/scripts/export/" \
  "${SERVER_HOST}:${SERVER_DIR}/scripts/export/"

rsync -avz \
  -e "$RSYNC_SSH" \
  --exclude '__pycache__/' \
  --exclude '*.pyc' \
  --exclude '*.pyo' \
  "${PROJECT_ROOT}/scripts/eval/" \
  "${SERVER_HOST}:${SERVER_DIR}/scripts/eval/"

if [[ -f "${PROJECT_ROOT}/label_map.json" ]]; then
    rsync -avz -e "$RSYNC_SSH" \
      "${PROJECT_ROOT}/label_map.json" \
      "${SERVER_HOST}:${SERVER_DIR}/label_map.json"
fi

echo
echo "Done. Server training example:"
echo "  ssh -p ${SSH_PORT} ${SERVER_HOST}"
echo "  cd ${SERVER_DIR}"
echo "  python3 scripts/train/train_tcn.py --dataset dataset_keypoints --feature-mode full"
