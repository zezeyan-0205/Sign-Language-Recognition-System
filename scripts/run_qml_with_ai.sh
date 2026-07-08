#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
QML_APP="${QML_APP:-$BUILD_DIR/qt_qml_app/SignLanguageQml}"

RKLLM_SERVER_DIR="${RKLLM_SERVER_DIR:-$HOME/rkllm_server}"
RKLLM_MODEL_PATH="${RKLLM_MODEL_PATH:-/home/elf/models/DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm}"
RKLLM_PLATFORM="${RKLLM_PLATFORM:-rk3588}"
RKLLM_HOST="${RKLLM_HOST:-127.0.0.1}"
RKLLM_PORT="${RKLLM_PORT:-8080}"
DISPLAY="${DISPLAY:-:0.0}"
QT_QUICK_BACKEND="${QT_QUICK_BACKEND:-software}"
QSG_RENDER_LOOP="${QSG_RENDER_LOOP:-basic}"
GTK_IM_MODULE="${GTK_IM_MODULE:-fcitx}"
QT_IM_MODULE="${QT_IM_MODULE:-fcitx}"
XMODIFIERS="${XMODIFIERS:-@im=fcitx}"
DSI_BACKLIGHT_PATH="${DSI_BACKLIGHT_PATH:-/sys/class/backlight/backlight-dsi0/brightness}"
DSI_BRIGHTNESS="${DSI_BRIGHTNESS:-250}"
TOUCH_DEVICE="${TOUCH_DEVICE:-generic ft5x06 (79)}"
TOUCH_OUTPUT="${TOUCH_OUTPUT:-DSI-1}"

LOG_DIR="${LOG_DIR:-$ROOT_DIR/artifacts/logs}"
RKLLM_LOG="$LOG_DIR/rkllm_server.log"
RKLLM_PID=""
STARTED_RKLLM=0

mkdir -p "$LOG_DIR"

cleanup() {
    if [[ "$STARTED_RKLLM" == "1" && -n "$RKLLM_PID" ]]; then
        echo "[run] stopping rkllm server pid=$RKLLM_PID"
        kill "$RKLLM_PID" 2>/dev/null || true
        wait "$RKLLM_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

port_open() {
    python3 - "$RKLLM_HOST" "$RKLLM_PORT" <<'PY'
import socket
import sys

host = sys.argv[1]
port = int(sys.argv[2])
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(0.4)
try:
    sock.connect((host, port))
except OSError:
    sys.exit(1)
finally:
    sock.close()
PY
}

wait_for_rkllm() {
    echo "[run] waiting for rkllm server at http://$RKLLM_HOST:$RKLLM_PORT ..."
    for _ in $(seq 1 180); do
        if port_open; then
            echo "[run] rkllm server is ready"
            return 0
        fi
        sleep 1
    done

    echo "[run] rkllm server did not become ready. Recent log:"
    tail -80 "$RKLLM_LOG" 2>/dev/null || true
    return 1
}

set_dsi_brightness() {
    if [[ ! -e "$DSI_BACKLIGHT_PATH" ]]; then
        echo "[run] DSI backlight path not found: $DSI_BACKLIGHT_PATH"
        return 0
    fi

    echo "[run] setting DSI brightness: $DSI_BRIGHTNESS"
    if [[ -w "$DSI_BACKLIGHT_PATH" ]]; then
        printf '%s\n' "$DSI_BRIGHTNESS" >"$DSI_BACKLIGHT_PATH" || true
    elif command -v sudo >/dev/null 2>&1; then
        printf '%s\n' "$DSI_BRIGHTNESS" | sudo tee "$DSI_BACKLIGHT_PATH" >/dev/null || true
    else
        echo "[run] cannot set DSI brightness without write permission or sudo"
    fi
}

start_input_method() {
    export GTK_IM_MODULE QT_IM_MODULE XMODIFIERS

    if ! command -v fcitx5 >/dev/null 2>&1; then
        echo "[run] fcitx5 not found, Chinese input method will be skipped"
        return 0
    fi

    if pgrep -x fcitx5 >/dev/null 2>&1; then
        echo "[run] fcitx5 input method already running"
        return 0
    fi

    echo "[run] starting fcitx5 input method"
    fcitx5 -d >/dev/null 2>&1 || echo "[run] failed to start fcitx5"
}

map_touchscreen() {
    if ! command -v xinput >/dev/null 2>&1; then
        echo "[run] xinput not found, touchscreen mapping will be skipped"
        return 0
    fi

    echo "[run] mapping touchscreen '$TOUCH_DEVICE' to $TOUCH_OUTPUT"
    DISPLAY="$DISPLAY" xinput map-to-output "$TOUCH_DEVICE" "$TOUCH_OUTPUT" \
        || echo "[run] touchscreen mapping failed; check xinput list and xrandr --listmonitors"
}

if [[ ! -x "$QML_APP" ]]; then
    echo "[run] QML app not found, building SignLanguageQml ..."
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR"
    cmake --build "$BUILD_DIR" --target SignLanguageQml -j"$(nproc)"
fi

if [[ ! -f "$RKLLM_MODEL_PATH" ]]; then
    echo "[run] rkllm model not found: $RKLLM_MODEL_PATH" >&2
    exit 1
fi

if [[ ! -f "$RKLLM_SERVER_DIR/flask_server.py" ]]; then
    echo "[run] rkllm flask server not found: $RKLLM_SERVER_DIR/flask_server.py" >&2
    exit 1
fi

set_dsi_brightness
start_input_method
map_touchscreen

if port_open; then
    echo "[run] rkllm server already running at http://$RKLLM_HOST:$RKLLM_PORT"
else
    if command -v sudo >/dev/null 2>&1; then
        echo "[run] refreshing sudo credential for rkllm performance setup"
        sudo -v || true
    fi

    echo "[run] starting rkllm server, log: $RKLLM_LOG"
    (
        cd "$RKLLM_SERVER_DIR"
        exec python3 flask_server.py \
            --rkllm_model_path "$RKLLM_MODEL_PATH" \
            --target_platform "$RKLLM_PLATFORM"
    ) >"$RKLLM_LOG" 2>&1 &
    RKLLM_PID=$!
    STARTED_RKLLM=1
    wait_for_rkllm
fi

echo "[run] starting QML UI"
echo "[run] DISPLAY=$DISPLAY"
echo "[run] QT_QUICK_BACKEND=$QT_QUICK_BACKEND"
echo "[run] QSG_RENDER_LOOP=$QSG_RENDER_LOOP"
echo "[run] QT_IM_MODULE=$QT_IM_MODULE"
export DISPLAY QT_QUICK_BACKEND QSG_RENDER_LOOP GTK_IM_MODULE QT_IM_MODULE XMODIFIERS
cd "$ROOT_DIR"
"$QML_APP"
