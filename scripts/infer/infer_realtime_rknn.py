import argparse
import os
import sys
import time
import json
import threading
import queue
import base64
import cv2
import numpy as np
import mediapipe as mp
from collections import deque, Counter
from PIL import Image, ImageDraw, ImageFont

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

from scripts.common.keypoint_features import (
    FEATURE_MODE_FULL,
    FEATURE_MODES,
    get_feature_dim,
    prepare_sequence_array,
)
from scripts.common.sentence_composer import compose_sentence_with_source

try:
    from rknnlite.api import RKNNLite
except ImportError as exc:
    raise SystemExit(
        "未找到 rknn-toolkit-lite2，请在 RK3588 板端安装对应版本后再运行。"
    ) from exc


# ================= 配置 =================
SEQ_LEN = 30
WINDOW_SEC = 1.5          # 与采集脚本的 --record_sec 保持一致
# PREDICT_INTERVAL = 0.10   # 每隔多少秒预测一次，避免每帧都跑模型导致卡顿
PREDICT_INTERVAL = 0.05
INPUT_SIZE = 258
MIN_HAND_ACTIVE_FRAMES = 8
HAND_LOST_GRACE_SEC = 0.35
MODEL_PATH = "artifacts/models/sign_tcn_best.rknn"
LABEL_MAP_PATH = "label_map.json"
WINDOW_NAME = "Realtime Sign Recognition"

POSE_MODEL = "models/pose_landmarker.task"
HAND_MODEL = "models/hand_landmarker.task"

CAMERA_ID = 0

# 只保留中间区域的手，和你采集数据时保持一致
ROI_X_MIN = 0.15
ROI_X_MAX = 0.85
ROI_Y_MIN = 0
ROI_Y_MAX = 1

POSE_DIM = 33 * 4
HAND_DIM = 21 * 3

# -------------------------- 完整人体动态 ROI 参数 --------------------------
# MediaPipe Pose 坐标是归一化坐标，所以这里的宽高比例都是 0~1。
MIN_POSE_CONFIDENCE = 0.30
POSE_TARGET_MAX_POSES = 4
POSE_TARGET_CENTER_WEIGHT = 1.2
POSE_TARGET_AREA_WEIGHT = 1.0
POSE_TARGET_CONF_WEIGHT = 0.8

BODY_ROI_EXPAND_X = 0.12
BODY_ROI_EXPAND_TOP = 0.08
BODY_ROI_EXPAND_BOTTOM = 0.10
MIN_BODY_ROI_WIDTH_RATIO = 0.22
MIN_BODY_ROI_HEIGHT_RATIO = 0.25
MAX_BODY_ROI_WIDTH_RATIO = 0.90
MAX_BODY_ROI_HEIGHT_RATIO = 0.98

HAND_TARGET_MAX_HANDS = 2
HAND_WRIST_DISTANCE_SCALE = 1.2
HAND_BODY_CENTER_DISTANCE_SCALE = 1.35
HAND_PREVIOUS_CENTER_DISTANCE = 0.18

ROI_ALPHA_EXPAND = 0.75
ROI_ALPHA_SHRINK = 0.35
ROI_HAND_HOLD_FRAMES = 4

ROI_MODE_BODY_ONLY = "BODY_ONLY"
ROI_MODE_BODY_HAND = "BODY_HAND"
ROI_MODE_POSE_LOST = "POSE_LOST"

# 预测稳定参数
CONF_THRESHOLD = 0.60      # 低于这个置信度，显示“识别中...”
SMOOTH_WINDOW = 6          # 最近8次预测做投票
MIN_VOTE_COUNT = 3         # 至少4票一致才更新显示
TOKEN_ADD_INTERVAL = 0.8
DELETE_READD_GRACE = 0.35
# ================= MediaPipe =================
BaseOptions = mp.tasks.BaseOptions
VisionRunningMode = mp.tasks.vision.RunningMode
PoseLandmarker = mp.tasks.vision.PoseLandmarker
PoseLandmarkerOptions = mp.tasks.vision.PoseLandmarkerOptions
HandLandmarker = mp.tasks.vision.HandLandmarker
HandLandmarkerOptions = mp.tasks.vision.HandLandmarkerOptions


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run realtime TCN RKNN sign recognition from a camera."
    )
    parser.add_argument("--model", default=MODEL_PATH, help="TCN RKNN model path.")
    parser.add_argument("--labels", default=LABEL_MAP_PATH, help="Label map JSON path.")
    parser.add_argument("--feature-mode", choices=FEATURE_MODES, default=FEATURE_MODE_FULL)
    parser.add_argument("--camera", type=int, default=CAMERA_ID, help="OpenCV camera id.")
    parser.add_argument("--width", type=int, default=640, help="Camera capture width.")
    parser.add_argument("--height", type=int, default=480, help="Camera capture height.")
    parser.add_argument("--display-width", type=int, default=1280, help="Fullscreen display width.")
    parser.add_argument("--display-height", type=int, default=720, help="Fullscreen display height.")
    parser.add_argument(
        "--profile",
        action="store_true",
        help="Show per-stage latency on the video overlay.",
    )
    parser.add_argument(
        "--pose-interval",
        type=int,
        default=5,
        help="Run PoseLandmarker every N frames and reuse the last pose between runs.",
    )
    parser.add_argument(
        "--hand-interval",
        type=int,
        default=2,
        help="Run HandLandmarker every N frames and reuse the last hands between runs.",
    )
    parser.add_argument(
        "--json-output",
        action="store_true",
        help="Emit JSON Lines to stdout for a Qt/QProcess frontend.",
    )
    parser.add_argument(
        "--no-window",
        action="store_true",
        help="Disable OpenCV preview window for headless worker mode.",
    )
    parser.add_argument(
        "--json-interval",
        type=float,
        default=0.2,
        help="Minimum seconds between JSON status messages.",
    )
    parser.add_argument(
        "--preview-output",
        action="store_true",
        help="Emit low-rate JPEG preview frames as base64 JSON events.",
    )
    parser.add_argument(
        "--preview-interval",
        type=float,
        default=0.2,
        help="Minimum seconds between preview JPEG messages.",
    )
    parser.add_argument(
        "--preview-width",
        type=int,
        default=640,
        help="Width of emitted preview JPEG frames.",
    )
    parser.add_argument(
        "--frame-stdin",
        action="store_true",
        help="Read JPEG frames from stdin JSON commands instead of opening a camera.",
    )
    parser.add_argument(
        "--composer",
        choices=("rule", "rkllm"),
        default="rule",
        help="Sentence composer used when recording stops.",
    )
    parser.add_argument(
        "--rkllm-url",
        default="http://127.0.0.1:8080/rkllm_chat",
        help="RKLLM Flask endpoint used when --composer=rkllm.",
    )
    parser.add_argument(
        "--llm-timeout",
        type=float,
        default=8.0,
        help="Seconds to wait for the LLM composer before falling back to rules.",
    )
    return parser.parse_args()


def softmax(logits):
    shifted = logits - np.max(logits, axis=1, keepdims=True)
    exp_values = np.exp(shifted)
    return exp_values / np.sum(exp_values, axis=1, keepdims=True)

def normalize_keypoints(data):
    """
    data shape: (30, 258)

    结构：
    pose: 33 * 4 = 132
    left_hand: 21 * 3 = 63
    right_hand: 21 * 3 = 63

    归一化思路：
    1. 用左右肩中心作为身体中心
    2. 用肩宽作为尺度
    3. pose 的 x,y,z 做归一化
    4. hand 的 x,y,z 也按身体中心和肩宽归一化
    """

    data = data.copy().astype(np.float32)

    pose_dim = 33 * 4
    hand_dim = 21 * 3

    for t in range(data.shape[0]):
        frame = data[t]

        pose = frame[:pose_dim].reshape(33, 4)

        left_shoulder = pose[11, :3]
        right_shoulder = pose[12, :3]

        # 如果肩膀没检测到，就跳过这一帧
        if np.all(left_shoulder == 0) or np.all(right_shoulder == 0):
            continue

        center = (left_shoulder + right_shoulder) / 2.0
        shoulder_width = np.linalg.norm(left_shoulder[:2] - right_shoulder[:2])

        if shoulder_width < 1e-6:
            continue

        # 归一化 pose
        pose[:, 0:3] = (pose[:, 0:3] - center) / shoulder_width
        frame[:pose_dim] = pose.reshape(-1)

        # 归一化 left hand
        left_start = pose_dim
        left_end = pose_dim + hand_dim
        left_hand = frame[left_start:left_end].reshape(21, 3)

        if not np.all(left_hand == 0):
            left_hand = (left_hand - center) / shoulder_width
            frame[left_start:left_end] = left_hand.reshape(-1)

        # 归一化 right hand
        right_start = pose_dim + hand_dim
        right_end = pose_dim + hand_dim * 2
        right_hand = frame[right_start:right_end].reshape(21, 3)

        if not np.all(right_hand == 0):
            right_hand = (right_hand - center) / shoulder_width
            frame[right_start:right_end] = right_hand.reshape(-1)

        data[t] = frame

    return data


def load_chinese_font(size=36):
    font_paths = [
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/arphic/ukai.ttc",
        "/System/Library/Fonts/PingFang.ttc",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
    ]

    for path in font_paths:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)

    print("警告：没有找到中文字体，中文可能无法显示")
    return ImageFont.load_default()


def draw_chinese_items(frame, items):
    img_pil = Image.fromarray(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))
    draw = ImageDraw.Draw(img_pil)
    for text, pos, font, color in items:
        draw.text(pos, text, font=font, fill=color)
    return cv2.cvtColor(np.array(img_pil), cv2.COLOR_RGB2BGR)


def emit_json(event_type, **payload):
    payload["type"] = event_type
    payload["time"] = time.time()
    print(json.dumps(payload, ensure_ascii=False), flush=True)


def emit_preview(frame, preview_width):
    h, w = frame.shape[:2]
    if preview_width > 0 and w > preview_width:
        preview_height = max(1, int(h * preview_width / w))
        frame = cv2.resize(frame, (preview_width, preview_height), interpolation=cv2.INTER_AREA)

    ok, encoded = cv2.imencode(".jpg", frame, [int(cv2.IMWRITE_JPEG_QUALITY), 70])
    if not ok:
        return

    emit_json(
        "preview",
        image=base64.b64encode(encoded.tobytes()).decode("ascii"),
        width=int(frame.shape[1]),
        height=int(frame.shape[0]),
    )


class StdinCommandReader:
    """Read JSON command/frame lines from Qt without blocking inference."""

    def __init__(self):
        self.commands = queue.Queue()
        self.frame_lock = threading.Lock()
        self.frame = None
        self.frame_id = 0
        self.frame_time = 0.0
        self.decode_ms = 0.0
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.thread.start()

    def stop(self):
        self.stop_event.set()

    def drain(self):
        items = []
        while True:
            try:
                items.append(self.commands.get_nowait())
            except queue.Empty:
                return items

    def latest_frame(self):
        with self.frame_lock:
            return self.frame, self.frame_id, self.frame_time, self.decode_ms

    def _run(self):
        while not self.stop_event.is_set():
            line = sys.stdin.readline()
            if line == "":
                time.sleep(0.05)
                continue
            line = line.strip()
            if not line:
                continue
            try:
                command = json.loads(line)
            except json.JSONDecodeError:
                emit_json("warning", message=f"invalid command: {line}")
                continue
            if isinstance(command, dict):
                if command.get("cmd") == "frame":
                    self._store_frame(command)
                    continue
                self.commands.put(command)

    def _store_frame(self, command):
        image_base64 = command.get("image")
        if not image_base64:
            return
        start = time.perf_counter()
        try:
            encoded = np.frombuffer(base64.b64decode(image_base64), dtype=np.uint8)
            frame = cv2.imdecode(encoded, cv2.IMREAD_COLOR)
        except Exception as exc:
            emit_json("warning", message=f"frame decode failed: {exc}")
            return
        if frame is None or frame.size == 0:
            return
        decode_ms = (time.perf_counter() - start) * 1000.0
        with self.frame_lock:
            self.frame = frame
            self.frame_id += 1
            self.frame_time = time.time()
            self.decode_ms = update_ema(self.decode_ms, decode_ms)


# 绘制进度条：current/total 可以是帧数，也可以是秒数
def draw_progress_bar(frame, current, total, bar_x, bar_y, bar_w, bar_h):
    # 背景条（灰色）
    cv2.rectangle(frame, (bar_x, bar_y), (bar_x + bar_w, bar_y + bar_h), (80, 80, 80), -1)
    # 进度填充（青色）
    ratio = current / total if total > 0 else 0.0
    ratio = max(0.0, min(1.0, ratio))
    fill_w = int(bar_w * ratio)
    cv2.rectangle(frame, (bar_x, bar_y), (bar_x + fill_w, bar_y + bar_h), (255, 255, 0), -1)
    # 边框
    cv2.rectangle(frame, (bar_x, bar_y), (bar_x + bar_w, bar_y + bar_h), (255, 255, 255), 1)
    # 百分比文字
    percent_text = f"{int(ratio * 100)}%"
    text_x = bar_x + bar_w // 2 - 15
    text_y = bar_y + bar_h // 2 + 8
    cv2.putText(frame, percent_text, (text_x, text_y), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 1)


def hand_in_center_roi(landmarks):
    cx, cy = hand_center(landmarks)
    return ROI_X_MIN <= cx <= ROI_X_MAX and ROI_Y_MIN <= cy <= ROI_Y_MAX


def hand_center(landmarks):
    xs = [lm.x for lm in landmarks]
    ys = [lm.y for lm in landmarks]
    return sum(xs) / len(xs), sum(ys) / len(ys)


def clamp(value, low=0.0, high=1.0):
    return max(low, min(high, value))


def complete_bbox(x1, y1, x2, y2, clamp_to_roi=True):
    if clamp_to_roi:
        x1 = clamp(x1, ROI_X_MIN, ROI_X_MAX)
        x2 = clamp(x2, ROI_X_MIN, ROI_X_MAX)
        y1 = clamp(y1, ROI_Y_MIN, ROI_Y_MAX)
        y2 = clamp(y2, ROI_Y_MIN, ROI_Y_MAX)
    else:
        x1 = clamp(x1)
        x2 = clamp(x2)
        y1 = clamp(y1)
        y2 = clamp(y2)

    if x2 < x1:
        x1, x2 = x2, x1
    if y2 < y1:
        y1, y2 = y2, y1

    return {
        "x_min": x1,
        "y_min": y1,
        "x_max": x2,
        "y_max": y2,
        "width": max(1e-6, x2 - x1),
        "height": max(1e-6, y2 - y1),
        "cx": (x1 + x2) / 2.0,
        "cy": (y1 + y2) / 2.0,
    }


def union_bboxes(*boxes):
    valid = [box for box in boxes if box is not None]
    if not valid:
        return None
    return complete_bbox(
        min(box["x_min"] for box in valid),
        min(box["y_min"] for box in valid),
        max(box["x_max"] for box in valid),
        max(box["y_max"] for box in valid),
    )


def expand_bbox(box, expand_x, expand_top, expand_bottom):
    if box is None:
        return None
    return complete_bbox(
        box["x_min"] - box["width"] * expand_x,
        box["y_min"] - box["height"] * expand_top,
        box["x_max"] + box["width"] * expand_x,
        box["y_max"] + box["height"] * expand_bottom,
    )


def enforce_bbox_size(box, min_w, min_h, max_w, max_h):
    if box is None:
        return None

    width = min(max(box["width"], min_w), max_w)
    height = min(max(box["height"], min_h), max_h)
    cx = clamp(box["cx"], ROI_X_MIN, ROI_X_MAX)
    cy = clamp(box["cy"], ROI_Y_MIN, ROI_Y_MAX)
    x1 = cx - width / 2.0
    x2 = cx + width / 2.0
    y1 = cy - height / 2.0
    y2 = cy + height / 2.0

    if x1 < ROI_X_MIN:
        x2 += ROI_X_MIN - x1
        x1 = ROI_X_MIN
    if x2 > ROI_X_MAX:
        x1 -= x2 - ROI_X_MAX
        x2 = ROI_X_MAX
    if y1 < ROI_Y_MIN:
        y2 += ROI_Y_MIN - y1
        y1 = ROI_Y_MIN
    if y2 > ROI_Y_MAX:
        y1 -= y2 - ROI_Y_MAX
        y2 = ROI_Y_MAX

    return complete_bbox(x1, y1, x2, y2)


def bbox_area(box):
    return box["width"] * box["height"] if box is not None else 0.0


def lerp_bbox(current, target, alpha):
    if current is None:
        return target
    if target is None:
        return current
    return complete_bbox(
        current["x_min"] * (1.0 - alpha) + target["x_min"] * alpha,
        current["y_min"] * (1.0 - alpha) + target["y_min"] * alpha,
        current["x_max"] * (1.0 - alpha) + target["x_max"] * alpha,
        current["y_max"] * (1.0 - alpha) + target["y_max"] * alpha,
    )


def point_in_bbox(point, box):
    if point is None or box is None:
        return False
    return box["x_min"] <= point[0] <= box["x_max"] and box["y_min"] <= point[1] <= box["y_max"]


def visible_pose_point(pose, index):
    if pose is None or index >= len(pose):
        return None
    lm = pose[index]
    if getattr(lm, "visibility", 0.0) < MIN_POSE_CONFIDENCE:
        return None
    return np.array([lm.x, lm.y], dtype=np.float32)


def collect_visible_pose_points(pose, indices):
    points = []
    for index in indices:
        point = visible_pose_point(pose, index)
        if point is not None:
            points.append(point)
    return points


def bbox_from_points(points, clamp_to_roi=True):
    if not points:
        return None
    arr = np.vstack(points)
    return complete_bbox(
        float(np.min(arr[:, 0])),
        float(np.min(arr[:, 1])),
        float(np.max(arr[:, 0])),
        float(np.max(arr[:, 1])),
        clamp_to_roi=clamp_to_roi,
    )


BODY_BBOX_POSE_INDICES = (
    0, 2, 5, 7, 8,
    11, 12, 13, 14, 15, 16,
    23, 24, 25, 26, 27, 28,
)
LOWER_BODY_POSE_INDICES = (25, 26, 27, 28)


def estimate_full_body_bbox_from_upper_body(pose):
    left_shoulder = visible_pose_point(pose, 11)
    right_shoulder = visible_pose_point(pose, 12)
    if left_shoulder is None or right_shoulder is None:
        return None

    shoulder_center = (left_shoulder + right_shoulder) / 2.0
    shoulder_width = float(np.linalg.norm(left_shoulder - right_shoulder))
    if shoulder_width < 1e-6:
        return None

    left_hip = visible_pose_point(pose, 23)
    right_hip = visible_pose_point(pose, 24)
    nose = visible_pose_point(pose, 0)
    top_from_head = float(nose[1] - shoulder_width * 0.6) if nose is not None else float(shoulder_center[1] - shoulder_width * 1.2)

    if left_hip is not None and right_hip is not None:
        hip_center = (left_hip + right_hip) / 2.0
        torso_height = max(float(hip_center[1] - shoulder_center[1]), shoulder_width * 0.8)
        top = min(top_from_head, float(shoulder_center[1] - shoulder_width * 1.0))
        bottom = float(hip_center[1] + torso_height * 1.8)
        center_x = float((shoulder_center[0] + hip_center[0]) / 2.0)
    else:
        top = float(shoulder_center[1] - shoulder_width * 1.2)
        bottom = float(shoulder_center[1] + shoulder_width * 4.0)
        center_x = float(shoulder_center[0])

    half_width = shoulder_width * 1.6
    return complete_bbox(center_x - half_width, top, center_x + half_width, bottom, clamp_to_roi=False)


def compute_full_body_bbox_from_pose(target_pose):
    pose_points = collect_visible_pose_points(target_pose, BODY_BBOX_POSE_INDICES)
    direct_bbox = bbox_from_points(pose_points, clamp_to_roi=False)
    estimated_bbox = estimate_full_body_bbox_from_upper_body(target_pose) if direct_bbox is None else None
    person_bbox = direct_bbox if direct_bbox is not None else estimated_bbox

    if person_bbox is None:
        return None

    # person_bbox 贴合当前可见骨骼点；只做轻微扩展，让 ROI 不再覆盖大半个画面。
    body_roi = expand_bbox(person_bbox, BODY_ROI_EXPAND_X, BODY_ROI_EXPAND_TOP, BODY_ROI_EXPAND_BOTTOM)
    body_roi = enforce_bbox_size(
        body_roi,
        MIN_BODY_ROI_WIDTH_RATIO,
        MIN_BODY_ROI_HEIGHT_RATIO,
        MAX_BODY_ROI_WIDTH_RATIO,
        MAX_BODY_ROI_HEIGHT_RATIO,
    )
    return person_bbox, body_roi


def pose_confidence(pose):
    visible = [getattr(lm, "visibility", 0.0) for lm in pose]
    return float(np.mean(visible)) if visible else 0.0


def select_target_pose(pose_result):
    if pose_result is None or not pose_result.pose_landmarks:
        return None, None, None

    roi_cx = (ROI_X_MIN + ROI_X_MAX) / 2.0
    roi_cy = (ROI_Y_MIN + ROI_Y_MAX) / 2.0
    best_pose = None
    best_person_bbox = None
    best_body_roi = None
    best_score = -1e9

    for pose in pose_result.pose_landmarks:
        computed = compute_full_body_bbox_from_pose(pose)
        if computed is None:
            continue
        person_bbox, body_roi = computed
        if not point_in_bbox((person_bbox["cx"], person_bbox["cy"]), {
            "x_min": ROI_X_MIN,
            "x_max": ROI_X_MAX,
            "y_min": ROI_Y_MIN,
            "y_max": ROI_Y_MAX,
        }):
            continue

        center_distance = float(np.hypot(person_bbox["cx"] - roi_cx, person_bbox["cy"] - roi_cy))
        center_score = 1.0 - min(1.0, center_distance / 0.75)
        area_score = min(1.0, bbox_area(person_bbox) / 0.55)
        score = (
            center_score * POSE_TARGET_CENTER_WEIGHT +
            area_score * POSE_TARGET_AREA_WEIGHT +
            pose_confidence(pose) * POSE_TARGET_CONF_WEIGHT
        )
        if score > best_score:
            best_pose = pose
            best_person_bbox = person_bbox
            best_body_roi = body_roi
            best_score = score

    return best_pose, best_person_bbox, best_body_roi


def hand_bbox(landmarks):
    return bbox_from_points([np.array([lm.x, lm.y], dtype=np.float32) for lm in landmarks])


def hand_match_to_target_pose(landmarks, target_pose, body_roi, previous_centers):
    if target_pose is None or body_roi is None:
        return False, 1e9

    center = hand_center(landmarks)
    hand_point = np.array(center, dtype=np.float32)
    wrist_distances = []
    for index in (15, 16):
        wrist = visible_pose_point(target_pose, index)
        if wrist is not None:
            wrist_distances.append(float(np.linalg.norm(hand_point - wrist)))
    if wrist_distances:
        min_wrist_distance = min(wrist_distances)
        threshold = max(body_roi["width"], body_roi["height"]) * 0.18 * HAND_WRIST_DISTANCE_SCALE
        if min_wrist_distance <= threshold:
            return True, min_wrist_distance

    body_center = np.array([body_roi["cx"], body_roi["cy"]], dtype=np.float32)
    body_distance = float(np.linalg.norm(hand_point - body_center))
    search_roi = expand_bbox(body_roi, 0.18, 0.12, 0.10)
    if point_in_bbox(center, search_roi) and body_distance <= max(body_roi["width"], body_roi["height"]) * HAND_BODY_CENTER_DISTANCE_SCALE:
        return True, body_distance

    for prev_center in previous_centers:
        if float(np.linalg.norm(hand_point - np.array(prev_center, dtype=np.float32))) <= HAND_PREVIOUS_CENTER_DISTANCE:
            return True, body_distance

    return False, 1e9


class DynamicRoiTracker:
    def __init__(self):
        self.roi = None
        self.mode = ROI_MODE_POSE_LOST
        self.missed_hand_frames = 0
        self.previous_hand_centers = []

    def update(self, person_bbox, body_roi, valid_hands):
        if body_roi is None:
            self.roi = None
            self.mode = ROI_MODE_POSE_LOST
            self.previous_hand_centers = []
            self.missed_hand_frames += 1
            return None, self.mode

        target_roi = body_roi
        if valid_hands:
            hand_boxes = [hand_bbox(hand) for hand in valid_hands]
            target_roi = union_bboxes(person_bbox, body_roi, *hand_boxes)
            target_roi = enforce_bbox_size(
                target_roi,
                MIN_BODY_ROI_WIDTH_RATIO,
                MIN_BODY_ROI_HEIGHT_RATIO,
                MAX_BODY_ROI_WIDTH_RATIO,
                MAX_BODY_ROI_HEIGHT_RATIO,
            )
            self.mode = ROI_MODE_BODY_HAND
            self.missed_hand_frames = 0
            self.previous_hand_centers = [hand_center(hand) for hand in valid_hands]
        else:
            self.missed_hand_frames += 1
            if self.missed_hand_frames >= ROI_HAND_HOLD_FRAMES:
                self.mode = ROI_MODE_BODY_ONLY
                self.previous_hand_centers = []

        if self.roi is None:
            self.roi = target_roi
        else:
            alpha = ROI_ALPHA_EXPAND if bbox_area(target_roi) > bbox_area(self.roi) else ROI_ALPHA_SHRINK
            self.roi = lerp_bbox(self.roi, target_roi, alpha)
            if valid_hands:
                self.roi = union_bboxes(self.roi, *[hand_bbox(hand) for hand in valid_hands])

        if self.mode == ROI_MODE_POSE_LOST:
            self.mode = ROI_MODE_BODY_ONLY
        return self.roi, self.mode


def extract_pose(target_pose):
    if target_pose is None:
        return np.zeros(POSE_DIM, dtype=np.float32)

    data = []
    for lm in target_pose:
        data.extend([lm.x, lm.y, lm.z, lm.visibility])

    return np.array(data, dtype=np.float32)


def extract_hands(hand_result, target_pose, body_roi, dynamic_roi_tracker):
    left_hand = np.zeros(HAND_DIM, dtype=np.float32)
    right_hand = np.zeros(HAND_DIM, dtype=np.float32)
    debug_hands = []
    valid_candidates = []

    if hand_result is not None and hand_result.hand_landmarks:
        for idx, landmarks in enumerate(hand_result.hand_landmarks):
            center = hand_center(landmarks)
            debug_item = {"center": center, "landmarks": landmarks, "valid": False}
            debug_hands.append(debug_item)

            if not hand_in_center_roi(landmarks):
                continue

            matched, distance = hand_match_to_target_pose(
                landmarks,
                target_pose,
                body_roi,
                dynamic_roi_tracker.previous_hand_centers,
            )
            if not matched:
                continue

            handedness = ""
            if idx < len(hand_result.handedness) and hand_result.handedness[idx]:
                handedness = hand_result.handedness[idx][0].category_name
            valid_candidates.append({
                "landmarks": landmarks,
                "handedness": handedness,
                "distance": distance,
                "debug": debug_item,
            })

    return left_hand, right_hand, valid_candidates, debug_hands


def extract_original_hands(hand_result):
    left_hand = np.zeros(HAND_DIM, dtype=np.float32)
    right_hand = np.zeros(HAND_DIM, dtype=np.float32)

    if not hand_result.hand_landmarks:
        return left_hand, right_hand

    for idx, landmarks in enumerate(hand_result.hand_landmarks):
        if not hand_in_center_roi(landmarks):
            continue

        handedness = hand_result.handedness[idx][0].category_name
        data = []
        for lm in landmarks:
            data.extend([lm.x, lm.y, lm.z])
        data = np.array(data, dtype=np.float32)

        if handedness == "Left":
            left_hand = data
        elif handedness == "Right":
            right_hand = data

    return left_hand, right_hand


def fill_hands_from_candidates(candidates, dynamic_roi):
    left_hand = np.zeros(HAND_DIM, dtype=np.float32)
    right_hand = np.zeros(HAND_DIM, dtype=np.float32)
    kept = []

    filtered = []
    for item in candidates:
        center = item["debug"]["center"]
        if dynamic_roi is not None and not point_in_bbox(center, dynamic_roi):
            continue
        filtered.append(item)

    filtered.sort(key=lambda item: item["distance"])
    for item in filtered[:2]:
        data = []
        for lm in item["landmarks"]:
            data.extend([lm.x, lm.y, lm.z])
        data = np.array(data, dtype=np.float32)
        item["debug"]["valid"] = True
        kept.append(item["landmarks"])

        if item["handedness"] == "Left" and not np.any(left_hand != 0):
            left_hand = data
        elif item["handedness"] == "Right" and not np.any(right_hand != 0):
            right_hand = data
        elif not np.any(left_hand != 0):
            left_hand = data
        elif not np.any(right_hand != 0):
            right_hand = data

    return left_hand, right_hand, kept

def has_valid_hand(keypoints):
    left = keypoints[POSE_DIM:POSE_DIM + HAND_DIM]
    right = keypoints[POSE_DIM + HAND_DIM:POSE_DIM + HAND_DIM * 2]

    return np.any(left != 0) or np.any(right != 0)


def hand_valid_rate(seq):
    """统计一个序列中手部有效帧比例。seq shape: (N, 258)"""
    if len(seq) == 0:
        return 0.0
    arr = np.asarray(seq, dtype=np.float32)
    left = arr[:, POSE_DIM:POSE_DIM + HAND_DIM]
    right = arr[:, POSE_DIM + HAND_DIM:POSE_DIM + HAND_DIM * 2]
    valid = np.any(np.abs(left) > 1e-6, axis=1) | np.any(np.abs(right) > 1e-6, axis=1)
    return float(np.mean(valid))


def resample_sequence(seq, target_len=SEQ_LEN):
    """
    把任意帧数的关键点序列重采样成固定 target_len 帧。
    采集端也是“固定时长 + 重采样到 30 帧”，推理端要保持一致。
    """
    seq = np.asarray(seq, dtype=np.float32)

    if seq.ndim != 2 or seq.shape[1] != INPUT_SIZE:
        raise ValueError(f"序列形状错误: {seq.shape}, 期望 (N, {INPUT_SIZE})")

    if len(seq) == 0:
        return np.zeros((target_len, INPUT_SIZE), dtype=np.float32)

    if len(seq) == 1:
        return np.repeat(seq, target_len, axis=0).astype(np.float32)

    old_idx = np.linspace(0, len(seq) - 1, len(seq))
    new_idx = np.linspace(0, len(seq) - 1, target_len)

    out = np.empty((target_len, seq.shape[1]), dtype=np.float32)
    for d in range(seq.shape[1]):
        out[:, d] = np.interp(new_idx, old_idx, seq[:, d])

    return out.astype(np.float32)



def extract_keypoints(pose_result, hand_result, dynamic_roi_tracker):
    target_pose, person_bbox, body_roi = select_target_pose(pose_result)
    # 识别输入尽量保持和原始版本一致：TCN 使用 MediaPipe 返回的第一组 pose。
    # target_pose / dynamic ROI 只给医生端调试框使用，暂不参与关键点特征过滤。
    original_pose = pose_result.pose_landmarks[0] if pose_result and pose_result.pose_landmarks else None
    pose = extract_pose(original_pose)
    original_left_hand, original_right_hand = extract_original_hands(hand_result)
    _, _, candidates, debug_hands = extract_hands(hand_result, target_pose, body_roi, dynamic_roi_tracker)
    dynamic_roi, roi_mode = dynamic_roi_tracker.update(
        person_bbox,
        body_roi,
        [item["landmarks"] for item in candidates],
    )
    # 动态 ROI 暂时只用于医生端调试显示，不参与识别过滤。
    # 识别仍走原来的中心 ROI 手部逻辑，方便对比精度是否恢复。
    left_hand, right_hand = original_left_hand, original_right_hand
    kept_hands = []
    debug = {
        "target_pose": target_pose,
        "person_bbox": person_bbox,
        "body_roi": body_roi,
        "dynamic_roi": dynamic_roi,
        "roi_mode": roi_mode,
        "hands": debug_hands,
        "valid_hands": len(kept_hands),
    }
    return np.concatenate([pose, left_hand, right_hand]).astype(np.float32), debug


def draw_box(frame, box, color, thickness=2):
    if box is None:
        return
    h, w = frame.shape[:2]
    x1 = int(box["x_min"] * w)
    y1 = int(box["y_min"] * h)
    x2 = int(box["x_max"] * w)
    y2 = int(box["y_max"] * h)
    cv2.rectangle(frame, (x1, y1), (x2, y2), color, thickness)


def draw_roi(frame, debug=None):
    h, w = frame.shape[:2]
    x1 = int(ROI_X_MIN * w)
    y1 = int(ROI_Y_MIN * h)
    x2 = int(ROI_X_MAX * w)
    y2 = int(ROI_Y_MAX * h)
    cv2.rectangle(frame, (x1, y1), (x2, y2), (255, 255, 0), 2)
    if not debug:
        return

    draw_box(frame, debug.get("person_bbox"), (0, 180, 0), 1)
    draw_box(frame, debug.get("body_roi"), (255, 190, 80), 2)
    draw_box(frame, debug.get("dynamic_roi"), (255, 0, 0), 3)
    for hand in debug.get("hands", []):
        landmarks = hand.get("landmarks")
        if not landmarks:
            continue
        color = (0, 220, 0) if hand.get("valid") else (120, 120, 120)
        for lm in landmarks[::4]:
            cv2.circle(frame, (int(lm.x * w), int(lm.y * h)), 2, color, -1)

    cv2.putText(
        frame,
        f"{debug.get('roi_mode', ROI_MODE_POSE_LOST)} valid_hands={debug.get('valid_hands', 0)}",
        (20, max(24, h - 20)),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.6,
        (255, 255, 255),
        2,
    )


def update_ema(previous, value, alpha=0.15):
    if previous <= 0:
        return value
    return previous * (1.0 - alpha) + value * alpha


class LatestFrameReader:
    """Background camera reader that keeps only the newest frame."""

    def __init__(self, cap):
        self.cap = cap
        self.lock = threading.Lock()
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.frame = None
        self.frame_id = 0
        self.frame_time = 0.0
        self.read_ms = 0.0
        self.failed = False

    def start(self):
        self.thread.start()

    def stop(self):
        self.stop_event.set()
        self.thread.join(timeout=1.0)

    def latest(self):
        with self.lock:
            return self.frame, self.frame_id, self.frame_time, self.read_ms, self.failed

    def _run(self):
        while not self.stop_event.is_set():
            start = time.perf_counter()
            ret, frame = self.cap.read()
            elapsed_ms = (time.perf_counter() - start) * 1000.0
            if not ret:
                with self.lock:
                    self.failed = True
                    self.read_ms = update_ema(self.read_ms, elapsed_ms)
                time.sleep(0.01)
                continue

            with self.lock:
                self.frame = frame
                self.frame_id += 1
                self.frame_time = time.time()
                self.read_ms = update_ema(self.read_ms, elapsed_ms)
                self.failed = False


def main():
    args = parse_args()
    if args.pose_interval <= 0 or args.hand_interval <= 0:
        raise ValueError("--pose-interval 和 --hand-interval 必须大于 0")
    if args.no_window:
        args.json_output = True

    input_size = get_feature_dim(args.feature_mode)
    font_big = load_chinese_font(42)
    font_mid = load_chinese_font(30)

    with open(args.labels, "r", encoding="utf-8") as f:
        labels = json.load(f)

    print("类别:", labels)

    print("加载 RKNN 模型:", args.model)
    rknn = RKNNLite(verbose=False)
    ret = rknn.load_rknn(args.model)
    if ret != 0:
        raise RuntimeError(f"load_rknn 失败，返回值: {ret}")

    ret = rknn.init_runtime(core_mask=RKNNLite.NPU_CORE_0)
    if ret != 0:
        raise RuntimeError(f"init_runtime 失败，返回值: {ret}")

    # TCN 模型在当前 RK3588 runtime 上不要使用 inputs_pass_through。
    dummy = np.zeros((1, SEQ_LEN, input_size), dtype=np.float32)
    warmup_outputs = rknn.inference(inputs=[dummy])
    if not warmup_outputs:
        raise RuntimeError("RKNN 预热推理没有返回输出")
    if np.asarray(warmup_outputs[0]).reshape(-1).shape[0] != len(labels):
        raise ValueError(
            f"RKNN输出类别数不匹配: 实际为 {np.asarray(warmup_outputs[0]).reshape(-1).shape[0]}, "
            f"期望为 {len(labels)}"
        )

    print("推理后端: RKNNLite NPU")
    print(f"特征模式: {args.feature_mode}")
    print(f"模型输入: float32 [1, {SEQ_LEN}, {input_size}]")
    print(f"模型输出: logits [1, {len(labels)}]")
    if args.json_output:
        emit_json(
            "ready",
            backend="rknn",
            model=args.model,
            labels=labels,
            feature_mode=args.feature_mode,
            input_shape=[1, SEQ_LEN, input_size],
        )

    pose_options = PoseLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=POSE_MODEL),
        running_mode=VisionRunningMode.VIDEO,
        num_poses=POSE_TARGET_MAX_POSES,
    )

    hand_options = HandLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=HAND_MODEL),
        running_mode=VisionRunningMode.VIDEO,
        num_hands=HAND_TARGET_MAX_HANDS,
    )

    # 按时间缓存，而不是按固定帧数缓存。每项为 (timestamp, keypoints)
    sequence = deque()
    pred_history = deque(maxlen=SMOOTH_WINDOW)
    last_predict_time = 0.0

    display_pred = "等待中..."
    display_conf = 0.0
    last_json_time = 0.0
    last_preview_time = 0.0
    recording = False
    token_buffer = []
    last_added_token = ""
    last_added_time = 0.0
    deleted_token = ""
    deleted_until = 0.0
    last_sentence = ""
    last_sentence_source = ""
    last_valid_hand_time = 0.0
    hand_lost_frames = 0
    command_reader = StdinCommandReader()
    command_reader.start()

    cap = None
    frame_reader = None
    if args.frame_stdin:
        print("视频输入: Qt stdin JPEG frames")
    else:
        device_path = f"/dev/video{args.camera}"
        cap = cv2.VideoCapture(args.camera, cv2.CAP_V4L2)

        if not cap.isOpened():
            print(f"摄像头打开失败: {device_path}，可以通过 --camera 指定设备编号")
            command_reader.stop()
            rknn.release()
            return

        cap.set(cv2.CAP_PROP_FRAME_WIDTH, args.width)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, args.height)
        cap.set(cv2.CAP_PROP_FPS, 30)
        frame_reader = LatestFrameReader(cap)
        frame_reader.start()

    fps_time = time.time()
    fps = 0.0
    capture_ms = 0.0
    pose_ms = 0.0
    hand_ms = 0.0
    rknn_ms = 0.0
    draw_ms = 0.0
    frame_age_ms = 0.0
    frame_index = 0
    last_frame_id = 0
    last_pose_result = None
    last_hand_result = None
    dynamic_roi_tracker = DynamicRoiTracker()
    tracking_debug = {}

    with PoseLandmarker.create_from_options(pose_options) as pose_landmarker, \
         HandLandmarker.create_from_options(hand_options) as hand_landmarker:

        if not args.no_window:
            cv2.namedWindow(WINDOW_NAME, cv2.WINDOW_NORMAL)
            cv2.setWindowProperty(WINDOW_NAME, cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)

        while True:
            for command in command_reader.drain():
                cmd = command.get("cmd")
                if cmd == "start_recording":
                    recording = True
                    token_buffer = []
                    last_added_token = ""
                    last_added_time = 0.0
                    deleted_token = ""
                    deleted_until = 0.0
                    last_sentence = ""
                    last_sentence_source = ""
                    emit_json("recording", active=True, tokens=token_buffer)
                elif cmd == "stop_recording":
                    recording = False
                    last_sentence, last_sentence_source = compose_sentence_with_source(
                        token_buffer,
                        backend=args.composer,
                        rkllm_url=args.rkllm_url,
                        timeout=args.llm_timeout,
                    )
                    emit_json("recording", active=False, tokens=token_buffer)
                    emit_json(
                        "sentence",
                        tokens=token_buffer,
                        sentence=last_sentence,
                        composer=last_sentence_source,
                    )
                elif cmd == "clear":
                    recording = False
                    token_buffer = []
                    last_added_token = ""
                    last_added_time = 0.0
                    deleted_token = ""
                    deleted_until = 0.0
                    last_sentence = ""
                    last_sentence_source = ""
                    emit_json("recording", active=False, tokens=token_buffer)
                    emit_json("sentence", tokens=token_buffer, sentence=last_sentence, composer=last_sentence_source)
                elif cmd == "delete_last":
                    removed_token = token_buffer.pop() if token_buffer else ""
                    # 删除后只短暂防抖，允许用户重新做同一个词。
                    deleted_token = removed_token
                    deleted_until = time.time() + DELETE_READD_GRACE if removed_token else 0.0
                    last_added_token = token_buffer[-1] if token_buffer else ""
                    last_added_time = 0.0
                    last_sentence = ""
                    last_sentence_source = ""
                    emit_json("tokens", tokens=token_buffer)
                    emit_json("sentence", tokens=token_buffer, sentence=last_sentence, composer=last_sentence_source)
                else:
                    emit_json("warning", message=f"unknown command: {cmd}")

            if args.frame_stdin:
                frame, frame_id, frame_time, decode_ms = command_reader.latest_frame()
                capture_ms = decode_ms
                capture_failed = False
            else:
                frame, frame_id, frame_time, read_ms, capture_failed = frame_reader.latest()
                capture_ms = read_ms
                if capture_failed and frame is None:
                    break

            if frame is None or frame_id == last_frame_id:
                if not args.no_window:
                    key = cv2.waitKey(1) & 0xFF
                    if key == ord("q"):
                        break
                time.sleep(0.001)
                continue

            last_frame_id = frame_id
            frame_index += 1
            frame_age_ms = update_ema(frame_age_ms, (time.time() - frame_time) * 1000.0)

            now = time.time()
            fps = 1.0 / (now - fps_time + 1e-6)
            fps_time = now

            # frame = cv2.flip(frame, 1)
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            rgb.flags.writeable = False

            mp_image = mp.Image(
                image_format=mp.ImageFormat.SRGB,
                data=rgb
            )

            timestamp_ms = int(time.time() * 1000)

            if last_pose_result is None or frame_index % args.pose_interval == 0:
                pose_start = time.perf_counter()
                last_pose_result = pose_landmarker.detect_for_video(mp_image, timestamp_ms)
                pose_ms = update_ema(pose_ms, (time.perf_counter() - pose_start) * 1000.0)

            if last_hand_result is None or frame_index % args.hand_interval == 0:
                hand_start = time.perf_counter()
                last_hand_result = hand_landmarker.detect_for_video(mp_image, timestamp_ms)
                hand_ms = update_ema(hand_ms, (time.perf_counter() - hand_start) * 1000.0)

            rgb.flags.writeable = True

            keypoints, tracking_debug = extract_keypoints(last_pose_result, last_hand_result, dynamic_roi_tracker)
            target_pose_valid = tracking_debug.get("target_pose") is not None
            valid_hand_count = int(tracking_debug.get("valid_hands", 0))

            hands_valid = has_valid_hand(keypoints)
            if hands_valid:
                # 有手时，把当前帧加入“最近 WINDOW_SEC 秒”的时间窗口
                sequence.append((now, keypoints))
                last_valid_hand_time = now
                hand_lost_frames = 0
            else:
                hand_lost_frames += 1
                # MediaPipe 会偶发丢手一两帧，短暂丢失时保留当前窗口，避免进度条反复归零。
                if last_valid_hand_time <= 0 or now - last_valid_hand_time > HAND_LOST_GRACE_SEC:
                    sequence.clear()
                    pred_history.clear()
                    display_pred = "等待中..."
                    display_conf = 0.0
            if not target_pose_valid:
                display_pred = "请患者站在识别框内"
                display_conf = 0.0
                pred_history.clear()
            elif valid_hand_count == 0 and not hands_valid:
                display_pred = "等待手部进入识别区"
                display_conf = 0.0

            # 只保留最近 WINDOW_SEC 秒的数据
            while sequence and now - sequence[0][0] > WINDOW_SEC:
                sequence.popleft()

            # ========== 绘制时间窗口进度条 ==========
            if sequence:
                window_duration = sequence[-1][0] - sequence[0][0]
                raw_frame_count = len(sequence)
            else:
                window_duration = 0.0
                raw_frame_count = 0

            # 最近窗口里至少要有一定帧数，并且窗口接近 WINDOW_SEC，才开始预测
            ready_to_predict = (
                raw_frame_count >= MIN_HAND_ACTIVE_FRAMES and
                window_duration >= WINDOW_SEC * 0.80 and
                now - last_predict_time >= PREDICT_INTERVAL
            )

            if ready_to_predict:
                raw_seq = [kp for _, kp in sequence]
                valid_rate = hand_valid_rate(raw_seq)

                # 有手帧比例太低时，不预测，避免把残缺动作送进模型
                if valid_rate < 0.60:
                    display_pred = "识别中..."
                    display_conf = 0.0
                else:
                    x = resample_sequence(raw_seq, SEQ_LEN)
                    x = prepare_sequence_array(x, args.feature_mode, SEQ_LEN)
                    x = np.expand_dims(x, axis=0).astype(np.float32)

                    rknn_start = time.perf_counter()
                    outputs = rknn.inference(inputs=[x])
                    rknn_ms = update_ema(rknn_ms, (time.perf_counter() - rknn_start) * 1000.0)
                    if not outputs:
                        raise RuntimeError("RKNN 推理没有返回输出")
                    logits = np.asarray(outputs[0], dtype=np.float32).reshape(1, -1)
                    probs = softmax(logits)
                    pred_idx = int(np.argmax(probs, axis=1)[0])
                    raw_pred = labels[pred_idx]
                    raw_conf = float(probs[0, pred_idx])
                    last_predict_time = now

                    if raw_conf >= CONF_THRESHOLD:
                        pred_history.append((raw_pred, raw_conf))

                    if len(pred_history) > 0:
                        votes = Counter([p[0] for p in pred_history])
                        best_label, vote_count = votes.most_common(1)[0]

                        if vote_count >= MIN_VOTE_COUNT:
                            related_confs = [c for p, c in pred_history if p == best_label]
                            display_pred = best_label
                            display_conf = float(np.mean(related_confs))
                        else:
                            display_pred = "识别中..."
                            display_conf = raw_conf
                    else:
                        display_pred = "识别中..."
                        display_conf = raw_conf

            if (
                recording and
                display_pred not in ("等待中...", "识别中...", "请患者站在识别框内", "等待手部进入识别区") and
                display_conf >= CONF_THRESHOLD and
                display_pred != last_added_token and
                not (display_pred == deleted_token and now < deleted_until) and
                now - last_added_time >= TOKEN_ADD_INTERVAL
            ):
                token_buffer.append(display_pred)
                last_added_token = display_pred
                last_added_time = now
                if display_pred == deleted_token:
                    deleted_token = ""
                    deleted_until = 0.0
                emit_json("tokens", tokens=token_buffer)

            if args.json_output and now - last_json_time >= args.json_interval:
                dynamic_roi = tracking_debug.get("dynamic_roi")
                body_roi = tracking_debug.get("body_roi")
                person_bbox = tracking_debug.get("person_bbox")
                def serialize_box(box):
                    if box is None:
                        return None
                    return {
                        "x_min": round(float(box["x_min"]), 4),
                        "y_min": round(float(box["y_min"]), 4),
                        "x_max": round(float(box["x_max"]), 4),
                        "y_max": round(float(box["y_max"]), 4),
                    }

                emit_json(
                    "result",
                    text=display_pred,
                    confidence=round(display_conf, 4),
                    recording=recording,
                    tokens=token_buffer,
                    sentence=last_sentence,
                    composer=last_sentence_source,
                    fps=round(fps, 2),
                    ready=bool(ready_to_predict),
                    hands_valid=bool(hands_valid),
                    valid_hands=valid_hand_count,
                    roi_mode=tracking_debug.get("roi_mode", ROI_MODE_POSE_LOST),
                    roi_box=serialize_box(dynamic_roi),
                    body_roi=serialize_box(body_roi),
                    person_bbox=serialize_box(person_bbox),
                    tracking_status=(
                        "请患者站在识别框内"
                        if not target_pose_valid
                        else ("追踪中" if valid_hand_count > 0 else "等待手部")
                    ),
                    hand_lost_frames=int(hand_lost_frames),
                    sequence_frames=raw_frame_count,
                    window_sec=round(window_duration, 3),
                    frame_id=int(frame_id),
                    frame_index=frame_index,
                    latency_ms={
                        "capture": round(capture_ms, 3),
                        "frame_age": round(frame_age_ms, 3),
                        "pose": round(pose_ms, 3),
                        "hand": round(hand_ms, 3),
                        "rknn": round(rknn_ms, 3),
                        "draw": round(draw_ms, 3),
                    },
                )
                last_json_time = now

            if args.preview_output and now - last_preview_time >= args.preview_interval:
                preview_frame = frame.copy()
                draw_roi(preview_frame, tracking_debug)
                emit_preview(preview_frame, args.preview_width)
                last_preview_time = now

            if not args.no_window:
                draw_start = time.perf_counter()
                draw_roi(frame, tracking_debug)

                bar_w = min(160, max(110, frame.shape[1] // 4))
                bar_h = 28
                bar_x = max(20, frame.shape[1] - bar_w - 30)
                bar_y = min(frame.shape[0] - 60, 220)
                draw_progress_bar(frame, min(window_duration, WINDOW_SEC), WINDOW_SEC, bar_x, bar_y, bar_w, bar_h)
                cv2.putText(frame, "时间窗口进度", (bar_x, bar_y - 10),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 1)

                overlay_items = [
                    (f"预测结果：{display_pred}", (30, 35), font_big, (0, 255, 0)),
                    (f"置信度：{display_conf:.2f}", (30, 95), font_mid, (0, 255, 0)),
                    (f"窗口帧：{raw_frame_count}  时长：{window_duration:.2f}s",
                     (30, 140), font_mid, (255, 255, 255)),
                    (f"FPS：{fps:.1f}", (30, 185), font_mid, (255, 255, 255)),
                ]

                if args.profile:
                    overlay_items.extend([
                        (f"采集:{capture_ms:.1f}ms  延迟:{frame_age_ms:.1f}ms",
                         (30, 230), font_mid, (255, 255, 0)),
                        (f"Pose:{pose_ms:.1f}ms  Hand:{hand_ms:.1f}ms",
                         (30, 275), font_mid, (255, 255, 0)),
                        (f"RKNN:{rknn_ms:.1f}ms  绘制:{draw_ms:.1f}ms",
                         (30, 320), font_mid, (255, 255, 0)),
                    ])

                frame = draw_chinese_items(frame, overlay_items)

                draw_ms = update_ema(draw_ms, (time.perf_counter() - draw_start) * 1000.0)

                display_frame = frame
                if args.display_width > 0 and args.display_height > 0:
                    display_frame = cv2.resize(
                        frame,
                        (args.display_width, args.display_height),
                        interpolation=cv2.INTER_LINEAR,
                    )

                cv2.imshow(WINDOW_NAME, display_frame)

                key = cv2.waitKey(1) & 0xFF
                if key == ord("q"):
                    break

    if frame_reader is not None:
        frame_reader.stop()
    if cap is not None:
        cap.release()
    if not args.no_window:
        cv2.destroyAllWindows()
    if args.json_output:
        emit_json("stopped")
    command_reader.stop()
    rknn.release()


if __name__ == "__main__":
    main()
