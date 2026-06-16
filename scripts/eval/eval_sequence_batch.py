import argparse
from collections import Counter
import csv
import json
import time
from pathlib import Path

import numpy as np


SEQ_LEN = 30
INPUT_SIZE = 258
POSE_DIM = 33 * 4
HAND_DIM = 21 * 3


def parse_args():
    parser = argparse.ArgumentParser(
        description="Batch evaluate sequence ONNX or RKNN models on keypoint .npy samples."
    )
    parser.add_argument("--dataset", default="dataset_keypoints")
    parser.add_argument("--labels", default="label_map.json")
    parser.add_argument("--backend", choices=["onnx", "rknn"], help="Inference backend.")
    parser.add_argument("--model", help="Model path for the selected backend.")
    parser.add_argument("--output", help="Write per-sample results to JSON.")
    parser.add_argument("--csv-output", help="Optional CSV summary output.")
    parser.add_argument("--limit-per-class", type=int, default=0)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument(
        "--pass-through",
        action="store_true",
        help="Legacy RKNN input pass-through switch. Do not use it for the TCN model.",
    )
    parser.add_argument("--compare", help="Compare current/primary JSON with --against JSON.")
    parser.add_argument("--against", help="Secondary JSON for --compare.")
    return parser.parse_args()


def load_labels(path):
    with Path(path).open("r", encoding="utf-8") as file:
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


def prepare_input(path):
    data = np.load(path).astype(np.float32)
    if data.ndim == 3:
        if data.shape[0] != 1:
            raise ValueError(f"Only batch=1 is supported, got {data.shape} from {path}")
        data = data[0]

    if data.ndim != 2 or data.shape[1] != INPUT_SIZE:
        raise ValueError(f"Expected (N, {INPUT_SIZE}), got {data.shape} from {path}")

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


def collect_samples(dataset_dir, labels, limit_per_class):
    samples = []
    root = Path(dataset_dir)
    for label_index, label in enumerate(labels):
        class_dir = root / label
        files = sorted(class_dir.glob("*.npy"))
        if limit_per_class > 0:
            files = files[:limit_per_class]
        for path in files:
            samples.append((path, label, label_index))
    if not samples:
        raise FileNotFoundError(f"No .npy samples found under {root}")
    return samples


class OnnxRunner:
    def __init__(self, model_path):
        import onnxruntime as ort

        self.session = ort.InferenceSession(str(model_path), providers=["CPUExecutionProvider"])
        self.input_name = self.session.get_inputs()[0].name

    def run(self, input_data):
        return self.session.run(None, {self.input_name: input_data})[0].reshape(-1).astype(np.float32)

    def close(self):
        return None


class RknnRunner:
    def __init__(self, model_path, warmup, pass_through):
        from rknnlite.api import RKNNLite

        print(f"Loading RKNN model: {model_path}", flush=True)
        self.rknn = RKNNLite(verbose=False)
        self.pass_through = pass_through
        ret = self.rknn.load_rknn(str(model_path))
        print(f"load_rknn ret={ret}", flush=True)
        if ret != 0:
            raise RuntimeError(f"load_rknn failed: {ret}")

        print("Initializing RKNN runtime...", flush=True)
        ret = self.rknn.init_runtime(core_mask=RKNNLite.NPU_CORE_0)
        print(f"init_runtime ret={ret}", flush=True)
        if ret != 0:
            raise RuntimeError(f"init_runtime failed: {ret}")

        dummy = np.zeros((1, SEQ_LEN, INPUT_SIZE), dtype=np.float32)
        for index in range(warmup):
            print(f"Warmup {index + 1}/{warmup}...", flush=True)
            self.run(dummy)
        print("RKNN warmup completed.", flush=True)

    def run(self, input_data):
        kwargs = {}
        if self.pass_through:
            kwargs["inputs_pass_through"] = [1]
        outputs = self.rknn.inference(inputs=[input_data], **kwargs)
        if not outputs:
            raise RuntimeError("RKNN inference returned no outputs")
        return np.asarray(outputs[0]).reshape(-1).astype(np.float32)

    def close(self):
        self.rknn.release()


def make_runner(backend, model_path, warmup, pass_through):
    if backend == "onnx":
        return OnnxRunner(model_path)
    if backend == "rknn":
        return RknnRunner(model_path, warmup, pass_through)
    raise ValueError(f"Unsupported backend: {backend}")


def evaluate(args):
    labels = load_labels(args.labels)
    samples = collect_samples(args.dataset, labels, args.limit_per_class)
    runner = make_runner(args.backend, Path(args.model), args.warmup, args.pass_through)

    results = []
    correct = 0
    latencies = []
    try:
        for index, (path, expected_label, expected_index) in enumerate(samples, start=1):
            input_data = prepare_input(path)
            print(f"Running sample {index}/{len(samples)}: {path}", flush=True)
            start = time.perf_counter()
            logits = runner.run(input_data)
            latency_ms = (time.perf_counter() - start) * 1000.0
            latencies.append(latency_ms)

            probs = softmax(logits)
            pred_index = int(np.argmax(probs))
            pred_label = labels[pred_index]
            is_correct = pred_index == expected_index
            correct += int(is_correct)

            result = {
                "path": str(path),
                "expected": expected_label,
                "predicted": pred_label,
                "correct": is_correct,
                "confidence": float(probs[pred_index]),
                "latency_ms": float(latency_ms),
                "logits": logits.tolist(),
                "probabilities": probs.tolist(),
            }
            results.append(result)
            status = "OK" if is_correct else "ERR"
            print(f"[{index:04d}/{len(samples):04d}] {status} {path} -> {pred_label} ({probs[pred_index]:.4f})")
    finally:
        runner.close()

    accuracy = correct / len(samples)
    latency_arr = np.asarray(latencies, dtype=np.float32)
    payload = {
        "backend": args.backend,
        "model": args.model,
        "dataset": args.dataset,
        "labels": labels,
        "sample_count": len(samples),
        "accuracy": accuracy,
        "latency_ms": {
            "mean": float(np.mean(latency_arr)),
            "min": float(np.min(latency_arr)),
            "max": float(np.max(latency_arr)),
            "p50": float(np.percentile(latency_arr, 50)),
            "p90": float(np.percentile(latency_arr, 90)),
            "p95": float(np.percentile(latency_arr, 95)),
        },
        "results": results,
    }

    print("\nSummary:")
    print(f"Backend: {args.backend}")
    print(f"Samples: {len(samples)}")
    print(f"Accuracy: {accuracy:.4f}")
    print(f"Latency mean/p50/p95: {payload['latency_ms']['mean']:.3f} / "
          f"{payload['latency_ms']['p50']:.3f} / {payload['latency_ms']['p95']:.3f} ms")

    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("w", encoding="utf-8") as file:
            json.dump(payload, file, ensure_ascii=False, indent=2)
        print(f"Saved JSON: {output_path}")

    if args.csv_output:
        write_csv(Path(args.csv_output), results)
        print(f"Saved CSV: {args.csv_output}")


def write_csv(path, results):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(
            file,
            fieldnames=["path", "expected", "predicted", "correct", "confidence", "latency_ms"],
        )
        writer.writeheader()
        for row in results:
            writer.writerow({key: row[key] for key in writer.fieldnames})


def load_eval(path):
    with Path(path).open("r", encoding="utf-8") as file:
        return json.load(file)


def compare_results(primary_path, against_path):
    primary = load_eval(primary_path)
    against = load_eval(against_path)
    primary_by_path = {item["path"]: item for item in primary["results"]}
    against_by_path = {item["path"]: item for item in against["results"]}
    shared_paths = sorted(set(primary_by_path) & set(against_by_path))
    if not shared_paths:
        raise ValueError("No shared sample paths between result files")

    top1_match = 0
    cosine_values = []
    max_prob_diffs = []
    mismatches = []
    primary_correct = 0
    against_correct = 0
    primary_predictions = Counter()
    against_predictions = Counter()
    expected_counts = Counter()
    for path in shared_paths:
        lhs = primary_by_path[path]
        rhs = against_by_path[path]
        primary_correct += int(lhs["correct"])
        against_correct += int(rhs["correct"])
        primary_predictions[lhs["predicted"]] += 1
        against_predictions[rhs["predicted"]] += 1
        expected_counts[lhs["expected"]] += 1
        top1_same = lhs["predicted"] == rhs["predicted"]
        top1_match += int(top1_same)
        if not top1_same:
            mismatches.append((path, lhs["predicted"], rhs["predicted"], lhs["expected"]))

        lhs_logits = np.asarray(lhs["logits"], dtype=np.float32)
        rhs_logits = np.asarray(rhs["logits"], dtype=np.float32)
        cosine = float(np.dot(lhs_logits, rhs_logits) / (
            np.linalg.norm(lhs_logits) * np.linalg.norm(rhs_logits) + 1e-12
        ))
        cosine_values.append(cosine)

        lhs_probs = np.asarray(lhs["probabilities"], dtype=np.float32)
        rhs_probs = np.asarray(rhs["probabilities"], dtype=np.float32)
        max_prob_diffs.append(float(np.max(np.abs(lhs_probs - rhs_probs))))

    print("Comparison:")
    print(f"Primary: {primary_path} ({primary['backend']})")
    print(f"Against: {against_path} ({against['backend']})")
    print(f"Shared samples: {len(shared_paths)}")
    print(f"Primary accuracy: {primary_correct / len(shared_paths):.4f}")
    print(f"Against accuracy: {against_correct / len(shared_paths):.4f}")
    print(f"Top1 agreement: {top1_match / len(shared_paths):.4f}")
    print(f"Mean cosine logits: {np.mean(cosine_values):.6f}")
    print(f"Mean max prob diff: {np.mean(max_prob_diffs):.6f}")
    print(f"Expected distribution: {dict(expected_counts)}")
    print(f"Primary prediction distribution: {dict(primary_predictions)}")
    print(f"Against prediction distribution: {dict(against_predictions)}")
    print(f"Mismatches: {len(mismatches)}")
    for path, lhs_pred, rhs_pred, expected in mismatches[:20]:
        print(f"  {path}: primary={lhs_pred}, against={rhs_pred}, expected={expected}")


def main():
    args = parse_args()
    if args.compare:
        if not args.against:
            raise ValueError("--compare requires --against")
        compare_results(args.compare, args.against)
        return

    if not args.backend or not args.model or not args.output:
        raise ValueError("Evaluation requires --backend, --model, and --output")
    evaluate(args)


if __name__ == "__main__":
    main()
