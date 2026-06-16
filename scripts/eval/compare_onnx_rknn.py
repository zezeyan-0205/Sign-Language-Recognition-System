import argparse
import json
from pathlib import Path

import numpy as np
import onnxruntime as ort


SEQ_LEN = 30
INPUT_SIZE = 258
POSE_DIM = 33 * 4
HAND_DIM = 21 * 3


def parse_args():
    parser = argparse.ArgumentParser(
        description="Compare ONNX logits with RKNN logits for one keypoint sample."
    )
    parser.add_argument("--onnx", default="sign_gru_best.onnx")
    parser.add_argument("--labels", default="label_map.json")
    parser.add_argument("--input", required=True, help="Keypoint .npy sample.")
    parser.add_argument(
        "--prepared-input",
        action="store_true",
        help="Input is already a preprocessed (1, 30, 258) float32 tensor.",
    )
    parser.add_argument(
        "--rknn-output",
        required=True,
        help="RKNN logits .npy saved by test_gru_rknn.py --save-output.",
    )
    parser.add_argument("--topk", type=int, default=3)
    return parser.parse_args()


def load_labels(path):
    with path.open("r", encoding="utf-8") as file:
        labels = json.load(file)
    if not isinstance(labels, list) or not labels:
        raise ValueError(f"{path} must contain a non-empty JSON list")
    return labels


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


def prepare_input(path, prepared_input=False):
    data = np.load(path).astype(np.float32)
    if prepared_input:
        if data.shape != (1, SEQ_LEN, INPUT_SIZE):
            raise ValueError(f"Expected prepared input (1, {SEQ_LEN}, {INPUT_SIZE}), got {data.shape}")
        return np.ascontiguousarray(data)

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


def softmax(logits):
    logits = np.asarray(logits, dtype=np.float32).reshape(-1)
    shifted = logits - np.max(logits)
    exp_values = np.exp(shifted)
    return exp_values / np.sum(exp_values)


def print_topk(title, labels, logits, topk):
    probs = softmax(logits)
    indices = np.argsort(probs)[::-1][: min(topk, len(labels))]
    print(f"\n{title}:")
    for rank, index in enumerate(indices, start=1):
        print(f"Top {rank}: {labels[index]} prob={probs[index]:.4f}, logit={logits[index]:.4f}")
    return indices[0], probs


def main():
    args = parse_args()
    onnx_path = Path(args.onnx)
    labels_path = Path(args.labels)
    input_path = Path(args.input)
    rknn_output_path = Path(args.rknn_output)

    labels = load_labels(labels_path)
    input_data = prepare_input(input_path, args.prepared_input)

    session = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    input_name = session.get_inputs()[0].name
    onnx_logits = session.run(None, {input_name: input_data})[0].reshape(-1).astype(np.float32)
    rknn_logits = np.load(rknn_output_path).reshape(-1).astype(np.float32)

    if onnx_logits.shape != rknn_logits.shape:
        raise ValueError(f"Shape mismatch: ONNX {onnx_logits.shape}, RKNN {rknn_logits.shape}")

    print(f"Input: {input_path}")
    print(f"ONNX:  {onnx_path}")
    print(f"RKNN:  {rknn_output_path}")

    onnx_top1, onnx_probs = print_topk("ONNX", labels, onnx_logits, args.topk)
    rknn_top1, rknn_probs = print_topk("RKNN", labels, rknn_logits, args.topk)

    diff = onnx_logits - rknn_logits
    prob_diff = onnx_probs - rknn_probs
    cosine = float(np.dot(onnx_logits, rknn_logits) / (
        np.linalg.norm(onnx_logits) * np.linalg.norm(rknn_logits) + 1e-12
    ))

    print("\nDifference:")
    print(f"Top1 match: {'yes' if onnx_top1 == rknn_top1 else 'no'}")
    print(f"Max abs logit diff: {np.max(np.abs(diff)):.6f}")
    print(f"Mean abs logit diff: {np.mean(np.abs(diff)):.6f}")
    print(f"Max abs prob diff:  {np.max(np.abs(prob_diff)):.6f}")
    print(f"Cosine logits:      {cosine:.6f}")


if __name__ == "__main__":
    main()
