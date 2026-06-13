import os
import cv2
import time
import argparse
import numpy as np
import mediapipe as mp


SEQ_LEN = 30

mp_holistic = mp.solutions.holistic
mp_drawing = mp.solutions.drawing_utils


def make_dir(path):
    if not os.path.exists(path):
        os.makedirs(path)


def extract_keypoints(results):
    """
    每帧提取：
    - pose: 33个点 × 4 = 132
    - left hand: 21个点 × 3 = 63
    - right hand: 21个点 × 3 = 63
    总维度：258
    """

    if results.pose_landmarks:
        pose = np.array(
            [[lm.x, lm.y, lm.z, lm.visibility] for lm in results.pose_landmarks.landmark]
        ).flatten()
    else:
        pose = np.zeros(33 * 4)

    if results.left_hand_landmarks:
        left_hand = np.array(
            [[lm.x, lm.y, lm.z] for lm in results.left_hand_landmarks.landmark]
        ).flatten()
    else:
        left_hand = np.zeros(21 * 3)

    if results.right_hand_landmarks:
        right_hand = np.array(
            [[lm.x, lm.y, lm.z] for lm in results.right_hand_landmarks.landmark]
        ).flatten()
    else:
        right_hand = np.zeros(21 * 3)

    return np.concatenate([pose, left_hand, right_hand])


def draw_landmarks(image, results):
    if results.pose_landmarks:
        mp_drawing.draw_landmarks(
            image,
            results.pose_landmarks,
            mp_holistic.POSE_CONNECTIONS
        )

    if results.left_hand_landmarks:
        mp_drawing.draw_landmarks(
            image,
            results.left_hand_landmarks,
            mp_holistic.HAND_CONNECTIONS
        )

    if results.right_hand_landmarks:
        mp_drawing.draw_landmarks(
            image,
            results.right_hand_landmarks,
            mp_holistic.HAND_CONNECTIONS
        )


def get_next_index(save_dir):
    files = [f for f in os.listdir(save_dir) if f.endswith(".npy")]
    if not files:
        return 1

    nums = []
    for f in files:
        name = os.path.splitext(f)[0]
        if name.isdigit():
            nums.append(int(name))

    return max(nums) + 1 if nums else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", type=str, required=True, help="当前采集的词，比如 发烧")
    parser.add_argument("--camera", type=int, default=0, help="摄像头编号，默认0")
    parser.add_argument("--save_root", type=str, default="dataset_keypoints")
    args = parser.parse_args()

    save_dir = os.path.join(args.save_root, args.label)
    make_dir(save_dir)

    cap = cv2.VideoCapture(args.camera)

    if not cap.isOpened():
        print("摄像头打开失败，请检查camera编号")
        return

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
    cap.set(cv2.CAP_PROP_FPS, 30)

    sample_index = get_next_index(save_dir)
    collecting = False
    sequence = []

    print("===================================")
    print("手语关键点数据采集工具")
    print(f"当前类别: {args.label}")
    print("按 空格 开始采集一段")
    print("按 q 退出")
    print("===================================")

    with mp_holistic.Holistic(
        static_image_mode=False,
        model_complexity=1,
        smooth_landmarks=True,
        enable_segmentation=False,
        refine_face_landmarks=False,
        min_detection_confidence=0.5,
        min_tracking_confidence=0.5
    ) as holistic:

        while True:
            ret, frame = cap.read()
            if not ret:
                print("读取摄像头失败")
                break

            frame = cv2.flip(frame, 1)

            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            rgb.flags.writeable = False
            results = holistic.process(rgb)
            rgb.flags.writeable = True

            keypoints = extract_keypoints(results)
            draw_landmarks(frame, results)

            if collecting:
                sequence.append(keypoints)

                cv2.putText(
                    frame,
                    f"REC {len(sequence)}/{SEQ_LEN}",
                    (30, 60),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    1.3,
                    (0, 0, 255),
                    3
                )

                if len(sequence) >= SEQ_LEN:
                    data = np.array(sequence)

                    save_path = os.path.join(save_dir, f"{sample_index:03d}.npy")
                    np.save(save_path, data)

                    print(f"已保存: {save_path}, shape={data.shape}")

                    sample_index += 1
                    sequence = []
                    collecting = False

                    time.sleep(0.3)

            else:
                cv2.putText(
                    frame,
                    "Press SPACE to collect",
                    (30, 60),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    1.1,
                    (0, 255, 0),
                    3
                )

            cv2.putText(
                frame,
                f"Label: {args.label}",
                (30, 110),
                cv2.FONT_HERSHEY_SIMPLEX,
                1.0,
                (255, 255, 255),
                2
            )

            cv2.putText(
                frame,
                f"Saved: {sample_index - 1}",
                (30, 150),
                cv2.FONT_HERSHEY_SIMPLEX,
                1.0,
                (255, 255, 255),
                2
            )

            cv2.imshow("Sign Dataset Collector", frame)

            key = cv2.waitKey(1) & 0xFF

            if key == ord("q"):
                break

            if key == 32 and not collecting:
                sequence = []
                collecting = True
                print(f"开始采集第 {sample_index} 段：{args.label}")

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()