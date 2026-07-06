import os
import time
import argparse
import cv2
import numpy as np
import mediapipe as mp


DEFAULT_SEQ_LEN = 30
COUNTDOWN_SEC = 1
DEFAULT_RECORD_SEC = 1.5

POSE_DIM = 33 * 4
HAND_DIM = 21 * 3
FEATURE_DIM = POSE_DIM + HAND_DIM * 2

# 只保留画面中间区域的手
# 注意：你已经关闭了 frame = cv2.flip(frame, 1)，所以 Left/Right 不需要再反过来存
ROI_X_MIN = 0.15
ROI_X_MAX = 0.85
ROI_Y_MIN = 0.0
ROI_Y_MAX = 1.0

BaseOptions = mp.tasks.BaseOptions
VisionRunningMode = mp.tasks.vision.RunningMode
PoseLandmarker = mp.tasks.vision.PoseLandmarker
PoseLandmarkerOptions = mp.tasks.vision.PoseLandmarkerOptions
HandLandmarker = mp.tasks.vision.HandLandmarker
HandLandmarkerOptions = mp.tasks.vision.HandLandmarkerOptions


POSE_CONNECTIONS = [
    (11, 12), (11, 13), (13, 15),
    (12, 14), (14, 16),
    (11, 23), (12, 24), (23, 24),
]

HAND_CONNECTIONS = [
    (0, 1), (1, 2), (2, 3), (3, 4),
    (0, 5), (5, 6), (6, 7), (7, 8),
    (0, 9), (9, 10), (10, 11), (11, 12),
    (0, 13), (13, 14), (14, 15), (15, 16),
    (0, 17), (17, 18), (18, 19), (19, 20),
    (5, 9), (9, 13), (13, 17),
]


def make_dir(path):
    os.makedirs(path, exist_ok=True)


def get_next_index(save_dir):
    files = [f for f in os.listdir(save_dir) if f.endswith(".npy")]
    nums = []

    for f in files:
        name = os.path.splitext(f)[0]
        if name.isdigit():
            nums.append(int(name))

    return max(nums) + 1 if nums else 1


def hand_in_center_roi(landmarks):
    xs = [lm.x for lm in landmarks]
    ys = [lm.y for lm in landmarks]

    cx = sum(xs) / len(xs)
    cy = sum(ys) / len(ys)

    return (
        ROI_X_MIN <= cx <= ROI_X_MAX and
        ROI_Y_MIN <= cy <= ROI_Y_MAX
    )


def valid_hand_count(hand_result):
    """统计当前帧中位于 ROI 内的手数量。"""
    if not hand_result.hand_landmarks:
        return 0

    count = 0
    for landmarks in hand_result.hand_landmarks:
        if hand_in_center_roi(landmarks):
            count += 1

    return count


def draw_roi(frame):
    h, w = frame.shape[:2]

    x1 = int(ROI_X_MIN * w)
    y1 = int(ROI_Y_MIN * h)
    x2 = int(ROI_X_MAX * w)
    y2 = int(ROI_Y_MAX * h)

    cv2.rectangle(frame, (x1, y1), (x2, y2), (255, 255, 0), 2)

    put_text(
        frame,
        "Hand ROI",
        (x1, max(25, y1 + 25)),
        scale=0.7,
        color=(255, 255, 0),
        thickness=2
    )


def extract_pose(pose_result):
    if not pose_result.pose_landmarks:
        return np.zeros(POSE_DIM, dtype=np.float32)

    landmarks = pose_result.pose_landmarks[0]
    data = []

    for lm in landmarks:
        data.extend([lm.x, lm.y, lm.z, lm.visibility])

    return np.array(data, dtype=np.float32)


def extract_hands(hand_result):
    left_hand = np.zeros(HAND_DIM, dtype=np.float32)
    right_hand = np.zeros(HAND_DIM, dtype=np.float32)

    if not hand_result.hand_landmarks:
        return left_hand, right_hand

    for idx, landmarks in enumerate(hand_result.hand_landmarks):

        # 不在画面中间 ROI 内的手直接忽略
        if not hand_in_center_roi(landmarks):
            continue

        handedness = hand_result.handedness[idx][0].category_name

        data = []
        for lm in landmarks:
            data.extend([lm.x, lm.y, lm.z])

        data = np.array(data, dtype=np.float32)

        # 现在已经关闭 frame = cv2.flip(frame, 1)
        # 所以这里正常保存：Left -> left_hand，Right -> right_hand
        if handedness == "Left":
            left_hand = data
        elif handedness == "Right":
            right_hand = data

    return left_hand, right_hand


def extract_keypoints(pose_result, hand_result):
    pose = extract_pose(pose_result)
    left_hand, right_hand = extract_hands(hand_result)
    return np.concatenate([pose, left_hand, right_hand]).astype(np.float32)


def resample_sequence(sequence, target_len=30):
    """
    把任意长度的序列重采样到固定 target_len。
    例如：1.5秒内采到 45 帧 -> 30 帧；采到 18 帧 -> 插值到 30 帧。
    """
    seq = np.asarray(sequence, dtype=np.float32)

    if seq.ndim != 2 or seq.shape[1] != FEATURE_DIM:
        raise ValueError(f"sequence shape 应为 (N, {FEATURE_DIM})，但得到 {seq.shape}")

    if len(seq) == 0:
        return np.zeros((target_len, FEATURE_DIM), dtype=np.float32)

    if len(seq) == 1:
        return np.repeat(seq, target_len, axis=0).astype(np.float32)

    old_idx = np.linspace(0, len(seq) - 1, num=len(seq))
    new_idx = np.linspace(0, len(seq) - 1, num=target_len)

    # axis=0 表示按时间维插值
    out = np.empty((target_len, seq.shape[1]), dtype=np.float32)
    for d in range(seq.shape[1]):
        out[:, d] = np.interp(new_idx, old_idx, seq[:, d])

    return out.astype(np.float32)


def hand_valid_rate(data):
    """计算保存后样本的左右手有效帧比例，方便判断样本质量。"""
    left = data[:, POSE_DIM:POSE_DIM + HAND_DIM]
    right = data[:, POSE_DIM + HAND_DIM:POSE_DIM + HAND_DIM * 2]

    left_valid = np.any(np.abs(left) > 1e-6, axis=1).mean()
    right_valid = np.any(np.abs(right) > 1e-6, axis=1).mean()

    return left_valid, right_valid


def to_pixel(frame, lm):
    h, w = frame.shape[:2]
    return int(lm.x * w), int(lm.y * h)


def draw_pose(frame, pose_result):
    if not pose_result.pose_landmarks:
        return

    landmarks = pose_result.pose_landmarks[0]

    for a, b in POSE_CONNECTIONS:
        if a < len(landmarks) and b < len(landmarks):
            x1, y1 = to_pixel(frame, landmarks[a])
            x2, y2 = to_pixel(frame, landmarks[b])
            cv2.line(frame, (x1, y1), (x2, y2), (0, 255, 0), 3)

    for lm in landmarks:
        x, y = to_pixel(frame, lm)
        cv2.circle(frame, (x, y), 4, (0, 255, 0), -1)


def draw_hands(frame, hand_result):
    if not hand_result.hand_landmarks:
        return

    for landmarks in hand_result.hand_landmarks:

        # 不在 ROI 里的手不画
        if not hand_in_center_roi(landmarks):
            continue

        for a, b in HAND_CONNECTIONS:
            x1, y1 = to_pixel(frame, landmarks[a])
            x2, y2 = to_pixel(frame, landmarks[b])
            cv2.line(frame, (x1, y1), (x2, y2), (0, 0, 255), 2)

        for lm in landmarks:
            x, y = to_pixel(frame, lm)
            cv2.circle(frame, (x, y), 3, (0, 0, 255), -1)


def put_text(frame, text, pos, scale=0.9, color=(255, 255, 255), thickness=2):
    cv2.putText(
        frame,
        text,
        pos,
        cv2.FONT_HERSHEY_SIMPLEX,
        scale,
        color,
        thickness,
        cv2.LINE_AA,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", type=str, required=True)
    parser.add_argument("--camera", type=int, default=0)
    parser.add_argument("--save_root", type=str, default="dataset_keypoints")
    parser.add_argument("--pose_model", type=str, default="models/pose_landmarker.task")
    parser.add_argument("--hand_model", type=str, default="models/hand_landmarker.task")

    # 模型输入长度，仍然保持 30 帧
    parser.add_argument("--seq_len", type=int, default=DEFAULT_SEQ_LEN)

    # 新增：固定采集时长。采集 record_sec 秒，然后重采样成 seq_len 帧。
    parser.add_argument("--record_sec", type=float, default=DEFAULT_RECORD_SEC)

    # 是否要求采集开始时至少检测到一只手
    parser.add_argument("--require_hand", action="store_true")

    args = parser.parse_args()

    if not os.path.exists(args.pose_model):
        raise FileNotFoundError(f"找不到模型: {args.pose_model}")

    if not os.path.exists(args.hand_model):
        raise FileNotFoundError(f"找不到模型: {args.hand_model}")

    save_dir = os.path.join(args.save_root, args.label)
    make_dir(save_dir)

    cap = cv2.VideoCapture(args.camera)
    if not cap.isOpened():
        print("摄像头打开失败，可以尝试 --camera 1")
        return

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
    cap.set(cv2.CAP_PROP_FPS, 30)


    # 关闭自动曝光
    cap.set(cv2.CAP_PROP_AUTO_EXPOSURE, 1)

    # 调低曝光，数值越小通常越暗
    cap.set(cv2.CAP_PROP_EXPOSURE, 400)

    # 可选：降低增益，避免画面又亮又糊
    cap.set(cv2.CAP_PROP_GAIN, 0)

    pose_options = PoseLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=args.pose_model),
        running_mode=VisionRunningMode.VIDEO,
        num_poses=1,
        min_pose_detection_confidence=0.5,
        min_pose_presence_confidence=0.5,
        min_tracking_confidence=0.5,
    )

    hand_options = HandLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=args.hand_model),
        running_mode=VisionRunningMode.VIDEO,
        num_hands=2,
        min_hand_detection_confidence=0.5,
        min_hand_presence_confidence=0.5,
        min_tracking_confidence=0.5,
    )

    sample_index = get_next_index(save_dir)
    state = "idle"
    countdown_start = None

    # 原始采集序列，长度不固定
    raw_sequence = []
    collect_start_time = None

    fps = 0.0
    last_frame_time = time.time()

    print("====================================")
    print("MediaPipe Tasks 手语数据采集工具 V3")
    print(f"当前类别: {args.label}")
    print(f"采集方式: 固定 {args.record_sec:.2f} 秒 -> 重采样成 {args.seq_len} 帧")
    print("空格：倒计时后开始采集")
    print("q：退出")
    print("====================================")

    with PoseLandmarker.create_from_options(pose_options) as pose_landmarker, \
         HandLandmarker.create_from_options(hand_options) as hand_landmarker:

        while True:
            ret, frame = cap.read()
            if not ret:
                print("读取摄像头失败")
                break

            # 不再镜像翻转，否则左右手容易混乱
            # frame = cv2.flip(frame, 1)

            now = time.time()
            dt = now - last_frame_time
            last_frame_time = now
            if dt > 0:
                fps = 0.9 * fps + 0.1 * (1.0 / dt) if fps > 0 else (1.0 / dt)

            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

            mp_image = mp.Image(
                image_format=mp.ImageFormat.SRGB,
                data=rgb
            )

            timestamp_ms = int(time.time() * 1000)

            pose_result = pose_landmarker.detect_for_video(mp_image, timestamp_ms)
            hand_result = hand_landmarker.detect_for_video(mp_image, timestamp_ms)

            keypoints = extract_keypoints(pose_result, hand_result)
            hands_now = valid_hand_count(hand_result)

            draw_pose(frame, pose_result)
            draw_hands(frame, hand_result)
            draw_roi(frame)

            put_text(frame, f"Label: {args.label}", (30, 50))
            put_text(frame, f"Saved: {sample_index - 1}", (30, 90))
            put_text(frame, f"FPS: {fps:.1f}  Hands: {hands_now}", (30, 130), scale=0.75)
            put_text(frame, "SPACE: collect   q: quit", (30, 165), scale=0.75)

            if state == "idle":
                put_text(
                    frame,
                    "READY",
                    (30, 220),
                    scale=1.2,
                    color=(0, 255, 0),
                    thickness=3
                )

            elif state == "countdown":
                elapsed = time.time() - countdown_start
                remain = COUNTDOWN_SEC - int(elapsed)

                if remain > 0:
                    put_text(
                        frame,
                        str(remain),
                        (frame.shape[1] // 2 - 40, frame.shape[0] // 2),
                        scale=4.0,
                        color=(0, 255, 255),
                        thickness=6
                    )
                else:
                    if args.require_hand and hands_now < 1:
                        put_text(
                            frame,
                            "Show your hand first...",
                            (30, 280),
                            scale=1.0,
                            color=(0, 255, 255),
                            thickness=3
                        )
                    else:
                        state = "collecting"
                        raw_sequence = []
                        collect_start_time = time.time()
                        print(f"开始采集第 {sample_index} 段：{args.label}")

            elif state == "collecting":
                raw_sequence.append(keypoints)

                elapsed = time.time() - collect_start_time
                progress = min(elapsed / args.record_sec, 1.0)

                bar_x, bar_y = 30, 220
                bar_w, bar_h = 400, 25
                fill_w = int(bar_w * progress)

                cv2.rectangle(
                    frame,
                    (bar_x, bar_y),
                    (bar_x + bar_w, bar_y + bar_h),
                    (255, 255, 255),
                    2
                )
                cv2.rectangle(
                    frame,
                    (bar_x, bar_y),
                    (bar_x + fill_w, bar_y + bar_h),
                    (0, 0, 255),
                    -1
                )

                put_text(
                    frame,
                    f"REC {elapsed:.2f}/{args.record_sec:.2f}s  raw={len(raw_sequence)}",
                    (30, 285),
                    scale=1.0,
                    color=(0, 0, 255),
                    thickness=3
                )

                if elapsed >= args.record_sec:
                    data = resample_sequence(raw_sequence, target_len=args.seq_len)
                    save_path = os.path.join(save_dir, f"{sample_index:03d}.npy")
                    np.save(save_path, data)

                    left_rate, right_rate = hand_valid_rate(data)

                    print(
                        f"已保存: {save_path}, shape={data.shape}, "
                        f"raw_frames={len(raw_sequence)}, "
                        f"left_valid={left_rate:.2f}, right_valid={right_rate:.2f}"
                    )

                    sample_index += 1
                    raw_sequence = []
                    collect_start_time = None
                    state = "idle"
                    time.sleep(0.2)

            cv2.imshow("Sign Dataset Collector V3 Time Resample", frame)

            key = cv2.waitKey(1) & 0xFF

            if key == ord("q"):
                break

            if key == 32 and state == "idle":
                state = "countdown"
                countdown_start = time.time()
                print("倒计时开始，准备做动作")

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
