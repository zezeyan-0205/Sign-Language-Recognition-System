import argparse
import json
import sys
from pathlib import Path

import numpy as np
import torch


PROJECT_ROOT = Path(__file__).resolve().parents[2]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from scripts.common.sequence_models import INPUT_SIZE, SEQ_LEN, TCNClassifier


def parse_args():
    parser = argparse.ArgumentParser(description="Export the TCN classifier to ONNX.")
    parser.add_argument("--weights", default="artifacts/models/sign_tcn_best.pth")
    parser.add_argument("--labels", default="label_map.json")
    parser.add_argument("--output", default="artifacts/models/sign_tcn_best.onnx")
    parser.add_argument("--channels", default="128,128,128")
    parser.add_argument("--kernel-size", type=int, default=3)
    parser.add_argument("--dropout", type=float, default=0.2)
    parser.add_argument("--opset", type=int, default=13)
    return parser.parse_args()


def parse_channels(value):
    channels = tuple(int(item) for item in value.split(",") if item.strip())
    if not channels:
        raise ValueError("--channels must contain at least one channel size")
    return channels


def load_labels(path):
    with Path(path).open("r", encoding="utf-8") as file:
        labels = json.load(file)
    if not isinstance(labels, list) or not labels:
        raise ValueError(f"{path} must contain a non-empty JSON list")
    return labels


def load_state_dict(path):
    try:
        return torch.load(path, map_location="cpu", weights_only=True)
    except TypeError:
        return torch.load(path, map_location="cpu")


def verify_with_onnxruntime(output_path, model, example_input):
    try:
        import onnxruntime as ort
    except ImportError:
        print("onnxruntime is not installed; skipped numerical comparison.")
        return

    with torch.no_grad():
        torch_output = model(example_input).cpu().numpy()

    session = ort.InferenceSession(str(output_path), providers=["CPUExecutionProvider"])
    onnx_output = session.run(
        ["logits"], {"keypoints": example_input.cpu().numpy()}
    )[0]
    max_error = float(np.max(np.abs(torch_output - onnx_output)))
    print(f"PyTorch/ONNX maximum absolute error: {max_error:.8f}")
    if not np.allclose(torch_output, onnx_output, rtol=1e-4, atol=1e-5):
        raise RuntimeError("ONNX output does not match the PyTorch output")


def main():
    args = parse_args()
    labels = load_labels(args.labels)
    channels = parse_channels(args.channels)

    model = TCNClassifier(
        input_size=INPUT_SIZE,
        num_classes=len(labels),
        channels=channels,
        kernel_size=args.kernel_size,
        dropout=args.dropout,
        seq_len=SEQ_LEN,
    )
    model.load_state_dict(load_state_dict(Path(args.weights)))
    model.eval()

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    example_input = torch.randn(1, SEQ_LEN, INPUT_SIZE, dtype=torch.float32)

    with torch.no_grad():
        torch.onnx.export(
            model,
            example_input,
            str(output_path),
            export_params=True,
            opset_version=args.opset,
            do_constant_folding=True,
            input_names=["keypoints"],
            output_names=["logits"],
        )

    import onnx

    onnx_model = onnx.load(str(output_path))
    onnx.checker.check_model(onnx_model)
    verify_with_onnxruntime(output_path, model, example_input)

    print(f"Exported: {output_path}")
    print(f"Input:  keypoints float32 [1, {SEQ_LEN}, {INPUT_SIZE}]")
    print(f"Output: logits float32 [1, {len(labels)}]")
    print(f"Labels: {labels}")


if __name__ == "__main__":
    main()
