import argparse
from pathlib import Path

import numpy as np


SEQ_LEN = 30
INPUT_SIZE = 258
POSE_DIM = 33 * 4
HAND_DIM = 21 * 3


def parse_args():
    parser = argparse.ArgumentParser(
        description="Prepare one keypoint .npy sample into the exact GRU input tensor."
    )
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", default="prepared_input.npy")
    return parser.parse_args()


def normalize_keypoints(data):
    data = data.copy().astype(np.float32)

    for frame in data:
        pose = frame[:POSE_DIM].reshape(33, 4)
        left_shoulder = pose[11, :3]
        right_shoulder = pose[12, :3]

        if np.all(left_shoulder == 0) or np.all(right_shoulder == 0):
            continue

        center = (left_shoulder + right_shoulder) / 2.0
        shoulder_width = np.linalg.norm(left_shoulder[:2] - right_shoulder[:2])
        if shoulder_width < 1e-6:
            continue

        pose[:, :3] = (pose[:, :3] - center) / shoulder_width
        frame[:POSE_DIM] = pose.reshape(-1)

        left_start = POSE_DIM
        right_start = POSE_DIM + HAND_DIM
        left_hand = frame[left_start:right_start].reshape(21, 3)
        right_hand = frame[right_start:right_start + HAND_DIM].reshape(21, 3)

        if not np.all(left_hand == 0):
            frame[left_start:right_start] = ((left_hand - center) / shoulder_width).reshape(-1)

        if not np.all(right_hand == 0):
            frame[right_start:right_start + HAND_DIM] = ((right_hand - center) / shoulder_width).reshape(-1)

    return data


def prepare_input(path):
    data = np.load(path).astype(np.float32)
    if data.ndim == 3:
        if data.shape[0] != 1:
            raise ValueError(f"Only batch=1 is supported, got {data.shape}")
        data = data[0]

    if data.ndim != 2 or data.shape[1] != INPUT_SIZE:
        raise ValueError(f"Expected (N, {INPUT_SIZE}), got {data.shape}")

    if data.shape[0] > SEQ_LEN:
        data = data[:SEQ_LEN]
    elif data.shape[0] < SEQ_LEN:
        padding = np.zeros((SEQ_LEN - data.shape[0], INPUT_SIZE), dtype=np.float32)
        data = np.concatenate((data, padding), axis=0)

    data = normalize_keypoints(data)
    return np.ascontiguousarray(data[None, ...])


def main():
    args = parse_args()
    output_path = Path(args.output)
    tensor = prepare_input(Path(args.input))
    np.save(output_path, tensor)
    print(f"Saved: {output_path}")
    print(f"shape={tensor.shape}, dtype={tensor.dtype}, min={tensor.min():.6f}, max={tensor.max():.6f}")


if __name__ == "__main__":
    main()
