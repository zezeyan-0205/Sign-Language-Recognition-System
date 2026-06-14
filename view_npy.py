import numpy as np

arr = np.load("dataset_keypoints/我/005.npy")

# 打印数组整体信息 + 完整内容
print("========== 整个npy数组 形状 & 全部数据 ==========")
print(f"数组形状: {arr.shape}")
print(arr)

# 只看前5帧、每帧前30个特征
print("\n========== 前5帧，每帧前30维 ==========")
print(arr[:5, :30])

# 单独拆分 姿态/左手/右手 查看
pose_dim = 33 * 4
hand_dim = 21 * 3
pose = arr[:, :pose_dim]
left_hand = arr[:, pose_dim:pose_dim+hand_dim]
right_hand = arr[:, pose_dim+hand_dim:]

print("\n==== 姿态关键点(全部帧) ====")
print(pose)
print("\n==== 左手关键点(全部帧) ====")
print(left_hand)
print("\n==== 右手关键点(全部帧) ====")
print(right_hand)
