import os
import numpy as np

# ==========================
# 配置
# ==========================
# 数据集根目录，例如 dataset_keypoints
ROOT_DIR = "dataset_keypoints"

# 是否覆盖原文件
OVERWRITE = True

# ==========================
# 特征维度
# ==========================
POSE_DIM = 33 * 4          # 132
HAND_DIM = 21 * 3          # 63

LEFT_START = POSE_DIM
LEFT_END = POSE_DIM + HAND_DIM

RIGHT_START = LEFT_END
RIGHT_END = RIGHT_START + HAND_DIM

# ==========================
# 开始处理
# ==========================
count = 0

for root, dirs, files in os.walk(ROOT_DIR):
    for file in files:
        if not file.endswith(".npy"):
            continue

        path = os.path.join(root, file)

        data = np.load(path)

        # 检查形状
        if data.ndim != 2 or data.shape[1] != 258:
            print(f"[跳过] {path} 形状={data.shape}")
            continue

        # 复制左右手
        left = data[:, LEFT_START:LEFT_END].copy()
        right = data[:, RIGHT_START:RIGHT_END].copy()

        # 交换
        data[:, LEFT_START:LEFT_END] = right
        data[:, RIGHT_START:RIGHT_END] = left

        if OVERWRITE:
            np.save(path, data)
        else:
            new_path = path.replace(".npy", "_swapLR.npy")
            np.save(new_path, data)

        count += 1
        print(f"[完成] {path}")

print("=" * 50)
print(f"总共处理 {count} 个 .npy 文件")
print("左右手关键点交换完成！")