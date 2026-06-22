#!/usr/bin/env bash
set -euo pipefail

MEDIAPIPE_ROOT="${1:-$HOME/mediapipe-0.10.18}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
WRAPPER_SRC="${PROJECT_ROOT}/mediapipe_wrapper"
WRAPPER_DST="${MEDIAPIPE_ROOT}/mediapipe/examples/desktop/sign_wrapper"

if [[ ! -d "${MEDIAPIPE_ROOT}" ]]; then
  echo "MediaPipe root not found: ${MEDIAPIPE_ROOT}" >&2
  exit 1
fi

mkdir -p "${WRAPPER_DST}"
cp "${WRAPPER_SRC}/BUILD" "${WRAPPER_DST}/BUILD"
cp "${WRAPPER_SRC}/sign_mediapipe_extractor.cc" "${WRAPPER_DST}/sign_mediapipe_extractor.cc"

cd "${MEDIAPIPE_ROOT}"

bazel build \
  --repository_cache="$HOME/rk3588_offline_mediapipe/bazel_repo_cache" \
  --distdir="$HOME/rk3588_offline_mediapipe/bazel_distdir" \
  --repo_env=HERMETIC_PYTHON_VERSION=3.10 \
  --repo_env=YARN_CACHE_FOLDER="$HOME/rk3588_offline_mediapipe/yarn_cache" \
  --define MEDIAPIPE_DISABLE_GPU=1 \
  --jobs=1 \
  --local_cpu_resources=1 \
  --local_ram_resources=2048 \
  --discard_analysis_cache \
  --copt=-I/usr/include/opencv4 \
  //mediapipe/examples/desktop/sign_wrapper:libsign_mediapipe_extractor.so

echo
echo "Wrapper built:"
echo "  ${MEDIAPIPE_ROOT}/bazel-bin/mediapipe/examples/desktop/sign_wrapper/libsign_mediapipe_extractor.so"
