import argparse
from importlib.metadata import PackageNotFoundError, version
from pathlib import Path

import onnx
from rknn.api import RKNN


INPUT_NAME = "keypoints"
INPUT_SIZE_LIST = [[1, 30, 258]]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Convert the fixed-shape GRU ONNX model to RKNN."
    )
    parser.add_argument("--onnx", default="sign_gru_best.onnx")
    parser.add_argument("--output", default="sign_gru_best.rknn")
    parser.add_argument("--target", default="rk3588")
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def require_success(ret, step):
    if ret != 0:
        raise RuntimeError(f"RKNN {step} failed, return code: {ret}")


def get_package_version(package_name):
    try:
        return version(package_name)
    except PackageNotFoundError:
        return "unknown"


def main():
    args = parse_args()
    onnx_path = Path(args.onnx)
    output_path = Path(args.output)
    toolkit_version = get_package_version("rknn-toolkit2")

    if not hasattr(onnx, "mapping"):
        raise RuntimeError(
            f"onnx {onnx.__version__} is incompatible with RKNN Toolkit2 2.1.0. "
            "Use the Toolkit2 2.1.0 dependency set, for example onnx==1.14.1."
        )

    if not onnx_path.is_file():
        raise FileNotFoundError(f"ONNX model not found: {onnx_path}")

    print(f"ONNX:   {onnx_path}")
    print(f"RKNN:   {output_path}")
    print(f"Target: {args.target}")
    print(f"Toolkit2: {toolkit_version}")
    print(f"ONNX package: {onnx.__version__}")
    print(f"Input:  {INPUT_NAME} float32 {INPUT_SIZE_LIST[0]}")
    print("Mode:   non-quantized")

    rknn = RKNN(verbose=args.verbose)
    try:
        print("\n[1/4] Configuring RKNN...")
        ret = rknn.config(
            target_platform=args.target,
            optimization_level=3,
        )
        require_success(ret, "config")

        print("[2/4] Loading ONNX...")
        ret = rknn.load_onnx(model=str(onnx_path))
        require_success(ret, "load_onnx")

        print("[3/4] Building RKNN...")
        ret = rknn.build(do_quantization=False)
        require_success(ret, "build")

        print("[4/4] Exporting RKNN...")
        output_path.parent.mkdir(parents=True, exist_ok=True)
        ret = rknn.export_rknn(str(output_path))
        require_success(ret, "export_rknn")
    finally:
        rknn.release()

    print(f"\nConversion completed: {output_path}")


if __name__ == "__main__":
    main()
