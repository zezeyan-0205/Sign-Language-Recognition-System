import argparse
import json
import time
from pathlib import Path

import numpy as np

try:
    from rknnlite.api import RKNNLite
except ImportError as exc:
    raise SystemExit(
        "未找到 rknn-toolkit-lite2，请在 RK3588 板端安装对应版本后再运行。"
    ) from exc


SEQ_LEN = 30
INPUT_SIZE = 258
POSE_DIM = 33 * 4
HAND_DIM = 21 * 3


def parse_args():
    parser = argparse.ArgumentParser(
        description="在 RK3588 上测试 GRU RKNN 模型的输出和推理延迟。"
    )
    parser.add_argument("--model", default="sign_gru_best.rknn")
    parser.add_argument("--labels", default="label_map.json")
    parser.add_argument(
        "--input",
        help="关键点 .npy 文件，形状为 (N, 258) 或 (1, 30, 258)；不指定则使用随机输入。",
    )
    parser.add_argument(
        "--prepared-input",
        action="store_true",
        help="输入已经是预处理后的 (1, 30, 258) float32 张量，跳过补帧和归一化。",
    )
    parser.add_argument("--repeat", type=int, default=20, help="正式计时次数。")
    parser.add_argument("--warmup", type=int, default=5, help="计时前预热次数。")
    parser.add_argument("--topk", type=int, default=3)
    parser.add_argument(
        "--save-output",
        help="把最后一次 RKNN 输出 logits 保存为 .npy，方便和 ONNX 输出做数值对比。",
    )
    parser.add_argument(
        "--check-input-consumption",
        action="store_true",
        help="额外比较全零输入和随机输入。版本不匹配时可能触发 RKNN runtime 崩溃，默认关闭。",
    )
    parser.add_argument(
        "--pass-through",
        action="store_true",
        help="显式启用 inputs_pass_through=[1]，直接透传 float32 时序特征。",
    )
    return parser.parse_args()


def load_labels(path):
    with path.open("r", encoding="utf-8") as file:
        labels = json.load(file)
    if not isinstance(labels, list) or not labels:
        raise ValueError(f"{path} 必须是非空 JSON 列表")
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
        shoulder_width = np.linalg.norm(
            left_shoulder[:2] - right_shoulder[:2]
        )
        if shoulder_width < 1e-6:
            continue

        pose[:, :3] = (pose[:, :3] - center) / shoulder_width
        frame[:POSE_DIM] = pose.reshape(-1)

        left_start = POSE_DIM
        right_start = POSE_DIM + HAND_DIM
        left_hand = frame[left_start:right_start].reshape(21, 3)
        right_hand = frame[right_start:right_start + HAND_DIM].reshape(21, 3)

        if not np.all(left_hand == 0):
            frame[left_start:right_start] = (
                (left_hand - center) / shoulder_width
            ).reshape(-1)

        if not np.all(right_hand == 0):
            frame[right_start:right_start + HAND_DIM] = (
                (right_hand - center) / shoulder_width
            ).reshape(-1)

    return data


def prepare_input(path, prepared_input=False):
    if path is None:
        rng = np.random.default_rng(42)
        return rng.standard_normal(
            (1, SEQ_LEN, INPUT_SIZE), dtype=np.float32
        ), "随机输入（仅验证模型能否运行）"

    data = np.load(path).astype(np.float32)
    if prepared_input:
        if data.shape != (1, SEQ_LEN, INPUT_SIZE):
            raise ValueError(
                f"预处理输入形状应为 (1, {SEQ_LEN}, {INPUT_SIZE})，实际为 {data.shape}"
            )
        return np.ascontiguousarray(data), f"{path}（已预处理）"

    if data.ndim == 3:
        if data.shape[0] != 1:
            raise ValueError(f"仅支持 batch=1，实际输入形状为 {data.shape}")
        data = data[0]

    if data.ndim != 2 or data.shape[1] != INPUT_SIZE:
        raise ValueError(
            f"输入形状应为 (N, {INPUT_SIZE})，实际为 {data.shape}"
        )

    if data.shape[0] > SEQ_LEN:
        data = data[:SEQ_LEN]
    elif data.shape[0] < SEQ_LEN:
        padding = np.zeros(
            (SEQ_LEN - data.shape[0], INPUT_SIZE), dtype=np.float32
        )
        data = np.concatenate((data, padding), axis=0)

    data = normalize_keypoints(data)
    return np.ascontiguousarray(data[None, ...]), str(path)


def softmax(logits):
    logits = np.asarray(logits, dtype=np.float32).reshape(-1)
    shifted = logits - np.max(logits)
    exp_values = np.exp(shifted)
    return exp_values / np.sum(exp_values)


def run_inference(rknn, input_data, pass_through):
    kwargs = {}
    if pass_through:
        # 时序特征已经在训练侧完成归一化，直接透传给 NPU，禁止图像预处理介入。
        kwargs["inputs_pass_through"] = [1]
    outputs = rknn.inference(inputs=[input_data], **kwargs)
    if not outputs:
        raise RuntimeError("RKNN 推理没有返回输出")
    return np.asarray(outputs[0])


def main():
    args = parse_args()
    if args.repeat <= 0 or args.warmup < 0 or args.topk <= 0:
        raise ValueError("repeat、topk 必须大于 0，warmup 不能小于 0")

    model_path = Path(args.model)
    labels_path = Path(args.labels)
    input_path = Path(args.input) if args.input else None

    if not model_path.is_file():
        raise FileNotFoundError(f"找不到 RKNN 模型：{model_path}")
    if not labels_path.is_file():
        raise FileNotFoundError(f"找不到标签文件：{labels_path}")
    if input_path is not None and not input_path.is_file():
        raise FileNotFoundError(f"找不到输入文件：{input_path}")

    labels = load_labels(labels_path)
    input_data, input_description = prepare_input(input_path, args.prepared_input)

    print(f"模型：{model_path}")
    print(f"输入：{input_description}")
    print(f"输入张量：shape={input_data.shape}, dtype={input_data.dtype}")
    print(f"类别：{labels}")

    print("创建 RKNNLite 对象...", flush=True)
    rknn = RKNNLite(verbose=False)
    try:
        print("加载 RKNN 模型...", flush=True)
        ret = rknn.load_rknn(str(model_path))
        print(f"load_rknn 返回：{ret}", flush=True)
        if ret != 0:
            raise RuntimeError(f"load_rknn 失败，返回值：{ret}")

        print("初始化 RKNN runtime...", flush=True)
        ret = rknn.init_runtime(core_mask=RKNNLite.NPU_CORE_0)
        print(f"init_runtime 返回：{ret}", flush=True)
        if ret != 0:
            raise RuntimeError(f"init_runtime 失败，返回值：{ret}")

        if args.check_input_consumption:
            zero_output = run_inference(
                rknn, np.zeros_like(input_data), args.pass_through
            )
            probe_output = run_inference(
                rknn,
                np.random.default_rng(2026).standard_normal(
                    input_data.shape, dtype=np.float32
                ),
                args.pass_through,
            )
            if np.allclose(zero_output, probe_output, rtol=1e-5, atol=1e-5):
                raise RuntimeError(
                    "RKNN 对全零输入和随机输入返回相同结果，模型没有正确消费输入。"
                    "请检查模型编译版本与板端 librknnrt/驱动版本是否匹配。"
                )

        print(f"预热 {args.warmup} 次...")
        for _ in range(args.warmup):
            run_inference(rknn, input_data, args.pass_through)

        elapsed_ms = []
        output = None
        for _ in range(args.repeat):
            start = time.perf_counter()
            output = run_inference(rknn, input_data, args.pass_through)
            elapsed_ms.append((time.perf_counter() - start) * 1000.0)
    finally:
        rknn.release()

    logits = output.reshape(-1)
    if logits.size != len(labels):
        raise ValueError(
            f"模型输出类别数为 {logits.size}，标签数量为 {len(labels)}"
        )

    if args.save_output:
        output_path = Path(args.save_output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        np.save(output_path, logits.astype(np.float32))
        print(f"\n已保存 RKNN logits：{output_path}")

    probabilities = softmax(logits)
    topk = min(args.topk, len(labels))
    top_indices = np.argsort(probabilities)[::-1][:topk]

    print("\n推理结果：")
    for rank, index in enumerate(top_indices, start=1):
        print(
            f"Top {rank}: {labels[index]} "
            f"prob={probabilities[index]:.4f}, logit={logits[index]:.4f}"
        )

    times = np.asarray(elapsed_ms)
    print("\nNPU 延迟：")
    print(f"首次计时：{times[0]:.3f} ms")
    print(f"平均：{times.mean():.3f} ms")
    print(f"最小：{times.min():.3f} ms")
    print(f"最大：{times.max():.3f} ms")
    print(f"吞吐估算：{1000.0 / times.mean():.2f} 次/秒")

    if input_path is not None and input_path.parent.name in labels:
        predicted = labels[int(top_indices[0])]
        expected = input_path.parent.name
        print(f"\n样本标签：{expected}")
        print(f"判断：{'正确' if predicted == expected else '错误'}")


if __name__ == "__main__":
    main()
