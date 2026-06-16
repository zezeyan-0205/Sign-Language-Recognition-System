import argparse
import json
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn


SEQ_LEN = 30
INPUT_SIZE = 258
HIDDEN_SIZE = 128
NUM_LAYERS = 2
DROPOUT = 0.3


class GRUClassifier(nn.Module):
    def __init__(self, input_size, hidden_size, num_classes):
        super().__init__()
        self.gru = nn.GRU(
            input_size=input_size,
            hidden_size=hidden_size,
            num_layers=NUM_LAYERS,
            batch_first=True,
            dropout=DROPOUT,
            bidirectional=True,
        )
        self.fc = nn.Sequential(
            nn.Linear(hidden_size * 2, 128),
            nn.ReLU(),
            nn.Dropout(DROPOUT),
            nn.Linear(128, num_classes),
        )

    def forward(self, x):
        out, _ = self.gru(x)
        return self.fc(out[:, -1, :])


def parse_args():
    parser = argparse.ArgumentParser(description="Export the GRU classifier to ONNX.")
    parser.add_argument("--weights", default="sign_gru_best.pth")
    parser.add_argument("--labels", default="label_map.json")
    parser.add_argument("--output", default="sign_gru_best.onnx")
    parser.add_argument(
        "--opset",
        type=int,
        default=13,
        help="ONNX opset. RKNN Toolkit2 2.1.0 works more predictably with opset 13.",
    )
    parser.add_argument(
        "--dynamic-batch",
        action="store_true",
        help="Allow variable batch size. Keep disabled for RKNN conversion.",
    )
    return parser.parse_args()


def load_labels(path):
    with path.open("r", encoding="utf-8") as file:
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

    session = ort.InferenceSession(
        str(output_path), providers=["CPUExecutionProvider"]
    )
    onnx_output = session.run(
        ["logits"], {"keypoints": example_input.cpu().numpy()}
    )[0]
    max_error = float(np.max(np.abs(torch_output - onnx_output)))
    print(f"PyTorch/ONNX maximum absolute error: {max_error:.8f}")
    if not np.allclose(torch_output, onnx_output, rtol=1e-4, atol=1e-5):
        raise RuntimeError("ONNX output does not match the PyTorch output")


def main():
    args = parse_args()
    weights_path = Path(args.weights)
    labels_path = Path(args.labels)
    output_path = Path(args.output)

    labels = load_labels(labels_path)
    model = GRUClassifier(INPUT_SIZE, HIDDEN_SIZE, len(labels))
    state_dict = load_state_dict(weights_path)
    model.load_state_dict(state_dict)
    model.eval()

    example_input = torch.randn(1, SEQ_LEN, INPUT_SIZE, dtype=torch.float32)
    dynamic_axes = None
    if args.dynamic_batch:
        dynamic_axes = {
            "keypoints": {0: "batch_size"},
            "logits": {0: "batch_size"},
        }

    output_path.parent.mkdir(parents=True, exist_ok=True)
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
            dynamic_axes=dynamic_axes,
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
