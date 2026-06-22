#!/usr/bin/env bash
set -euo pipefail

echo "RKNN headers:"
find /usr /usr/local "$HOME/AItools" -name "rknn_api.h" -print 2>/dev/null || true

echo
echo "RKNN libraries:"
find /usr /usr/local "$HOME/AItools" -name "librknnrt.so*" -print 2>/dev/null || true

echo
echo "RGA:"
ls -l /dev/rga 2>/dev/null || true
find /usr /usr/local -name "RgaApi.h" -o -name "librga.so*" 2>/dev/null || true

echo
echo "Camera devices:"
v4l2-ctl --list-devices 2>/dev/null || true

echo
echo "Display:"
echo "DISPLAY=${DISPLAY:-}"
