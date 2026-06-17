import argparse
import os
import sys
import time
import json
import cv2
import numpy as np
import onnxruntime as ort
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


# ================= 配置 =================
SEQ_LEN = 30
WINDOW_SEC = 1.5          # 与采集脚本的 --record_sec 保持一致
PREDICT_INTERVAL = 0.10   # 每隔多少秒预测一次，避免每帧都跑模型导致卡顿
INPUT_SIZE = 258
MIN_HAND_ACTIVE_FRAMES = 8
MODEL_PATH = "artifacts/models/sign_tcn_best.onnx"
LABEL_MAP_PATH = "label_map.json"

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

# 预测稳定参数
CONF_THRESHOLD = 0.70      # 低于这个置信度，显示“识别中...”
SMOOTH_WINDOW = 8          # 最近8次预测做投票
MIN_VOTE_COUNT = 4         # 至少4票一致才更新显示
# ================= MediaPipe =================
BaseOptions = mp.tasks.BaseOptions
VisionRunningMode = mp.tasks.vision.RunningMode
PoseLandmarker = mp.tasks.vision.PoseLandmarker
PoseLandmarkerOptions = mp.tasks.vision.PoseLandmarkerOptions
HandLandmarker = mp.tasks.vision.HandLandmarker
HandLandmarkerOptions = mp.tasks.vision.HandLandmarkerOptions


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run realtime ONNX sign recognition from a camera."
    )
    parser.add_argument("--model", default=MODEL_PATH, help="ONNX model path.")
    parser.add_argument("--labels", default=LABEL_MAP_PATH, help="Label map JSON path.")
    parser.add_argument("--feature-mode", choices=FEATURE_MODES, default=FEATURE_MODE_FULL)
    parser.add_argument("--camera", type=int, default=CAMERA_ID, help="OpenCV camera id.")
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


def draw_chinese_text(frame, text, pos, font, color=(0, 255, 0)):
    img_pil = Image.fromarray(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))
    draw = ImageDraw.Draw(img_pil)

    draw.text(
        pos,
        text,
        font=font,
        fill=color
    )

    return cv2.cvtColor(np.array(img_pil), cv2.COLOR_RGB2BGR)


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
    xs = [lm.x for lm in landmarks]
    ys = [lm.y for lm in landmarks]

    cx = sum(xs) / len(xs)
    cy = sum(ys) / len(ys)

    return ROI_X_MIN <= cx <= ROI_X_MAX and ROI_Y_MIN <= cy <= ROI_Y_MAX


def extract_pose(pose_result):
    if not pose_result.pose_landmarks:
        return np.zeros(POSE_DIM, dtype=np.float32)

    data = []
    for lm in pose_result.pose_landmarks[0]:
        data.extend([lm.x, lm.y, lm.z, lm.visibility])

    return np.array(data, dtype=np.float32)


def extract_hands(hand_result):
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



def extract_keypoints(pose_result, hand_result):
    pose = extract_pose(pose_result)
    left_hand, right_hand = extract_hands(hand_result)
    return np.concatenate([pose, left_hand, right_hand]).astype(np.float32)


def draw_roi(frame):
    h, w = frame.shape[:2]
    x1 = int(ROI_X_MIN * w)
    y1 = int(ROI_Y_MIN * h)
    x2 = int(ROI_X_MAX * w)
    y2 = int(ROI_Y_MAX * h)
    cv2.rectangle(frame, (x1, y1), (x2, y2), (255, 255, 0), 2)


def main():
    args = parse_args()
    input_size = get_feature_dim(args.feature_mode)
    font_big = load_chinese_font(42)
    font_mid = load_chinese_font(30)

    with open(args.labels, "r", encoding="utf-8") as f:
        labels = json.load(f)

    print("类别:", labels)

    session = ort.InferenceSession(
        args.model,
        providers=["CPUExecutionProvider"],
    )
    input_info = session.get_inputs()[0]
    output_info = session.get_outputs()[0]
    expected_input_shape = [1, SEQ_LEN, input_size]
    if input_info.shape != expected_input_shape:
        raise ValueError(
            f"ONNX输入形状不匹配: 实际为 {input_info.shape}, "
            f"期望为 {expected_input_shape}"
        )
    if output_info.shape != [1, len(labels)]:
        raise ValueError(
            f"ONNX输出形状不匹配: 实际为 {output_info.shape}, "
            f"期望为 {[1, len(labels)]}"
        )
    print("推理后端: ONNX Runtime CPU")
    print(f"特征模式: {args.feature_mode}")
    print(f"模型输入: {input_info.name} {input_info.shape}")
    print(f"模型输出: {output_info.name} {output_info.shape}")

    pose_options = PoseLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=POSE_MODEL),
        running_mode=VisionRunningMode.VIDEO,
        num_poses=1,
    )

    hand_options = HandLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=HAND_MODEL),
        running_mode=VisionRunningMode.VIDEO,
        num_hands=2,
    )

    cap = cv2.VideoCapture(args.camera)

    if not cap.isOpened():
        print("摄像头打开失败，可以把 CAMERA_ID 改成 1")
        return

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
    cap.set(cv2.CAP_PROP_FPS, 30)

    # 按时间缓存，而不是按固定帧数缓存。每项为 (timestamp, keypoints)
    sequence = deque()
    pred_history = deque(maxlen=SMOOTH_WINDOW)
    last_predict_time = 0.0

    display_pred = "等待中..."
    display_conf = 0.0

    fps_time = time.time()
    fps = 0.0

    # 进度条位置（画面右侧）
    BAR_X = 1080
    BAR_Y = 220
    BAR_W = 160
    BAR_H = 30

    with PoseLandmarker.create_from_options(pose_options) as pose_landmarker, \
         HandLandmarker.create_from_options(hand_options) as hand_landmarker:

        while True:
            ret, frame = cap.read()
            if not ret:
                break

            now = time.time()
            fps = 1.0 / (now - fps_time + 1e-6)
            fps_time = now

            # frame = cv2.flip(frame, 1)
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

            mp_image = mp.Image(
                image_format=mp.ImageFormat.SRGB,
                data=rgb
            )

            timestamp_ms = int(time.time() * 1000)

            pose_result = pose_landmarker.detect_for_video(mp_image, timestamp_ms)
            hand_result = hand_landmarker.detect_for_video(mp_image, timestamp_ms)

            keypoints = extract_keypoints(pose_result, hand_result)

            if has_valid_hand(keypoints):
                # 有手时，把当前帧加入“最近 WINDOW_SEC 秒”的时间窗口
                sequence.append((now, keypoints))
            else:
                # 没有手时，认为用户还没开始动作，清空缓存
                sequence.clear()
                pred_history.clear()
                display_pred = "等待中..."
                display_conf = 0.0

            # 只保留最近 WINDOW_SEC 秒的数据
            while sequence and now - sequence[0][0] > WINDOW_SEC:
                sequence.popleft()

            draw_roi(frame)

            # ========== 绘制时间窗口进度条 ==========
            if sequence:
                window_duration = sequence[-1][0] - sequence[0][0]
                raw_frame_count = len(sequence)
            else:
                window_duration = 0.0
                raw_frame_count = 0

            draw_progress_bar(frame, min(window_duration, WINDOW_SEC), WINDOW_SEC, BAR_X, BAR_Y, BAR_W, BAR_H)
            cv2.putText(frame, "时间窗口进度", (BAR_X, BAR_Y - 10),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 1)

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

                    logits = session.run(
                        [output_info.name],
                        {input_info.name: x},
                    )[0]
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

            frame = draw_chinese_text(
                frame,
                f"预测结果：{display_pred}",
                (30, 35),
                font_big,
                color=(0, 255, 0)
            )

            frame = draw_chinese_text(
                frame,
                f"置信度：{display_conf:.2f}",
                (30, 95),
                font_mid,
                color=(0, 255, 0)
            )

            frame = draw_chinese_text(
                frame,
                f"窗口帧：{raw_frame_count}  时长：{window_duration:.2f}s",
                (30, 140),
                font_mid,
                color=(255, 255, 255)
            )

            frame = draw_chinese_text(
                frame,
                f"FPS：{fps:.1f}",
                (30, 185),
                font_mid,
                color=(255, 255, 255)
            )

            cv2.imshow("Realtime Sign Recognition", frame)

            key = cv2.waitKey(1) & 0xFF
            if key == ord("q"):
                break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
