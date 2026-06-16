import os
import json
import random
import numpy as np
import torch
import torch.nn as nn
from sklearn.model_selection import train_test_split
from sklearn.metrics import confusion_matrix, classification_report
from torch.utils.data import Dataset, DataLoader


# ================= 配置 =================
DATASET_DIR = "dataset_keypoints"

SEQ_LEN = 30
INPUT_SIZE = 258
BATCH_SIZE = 16
EPOCHS = 120
LR = 1e-3

HIDDEN_SIZE = 128
NUM_LAYERS = 2
DROPOUT = 0.3

SAVE_BEST = "sign_gru_best.pth"
SAVE_LAST = "sign_gru_last.pth"
LABEL_MAP_PATH = "label_map.json"

SEED = 42


# ================= 随机种子 =================
def set_seed(seed=42):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    torch.cuda.manual_seed_all(seed)


# ================= 关键点归一化 =================
def normalize_keypoints(data):
    """
    data shape: (30, 258)

    结构：
    pose: 33 * 4 = 132
    left_hand: 21 * 3 = 63
    right_hand: 21 * 3 = 63

    归一化思路：
    1. 用左右肩中心作为身体中心
    2. 用肩宽作为尺度
    3. pose 的 x,y,z 做归一化
    4. hand 的 x,y,z 也按身体中心和肩宽归一化
    """

    data = data.copy().astype(np.float32)

    pose_dim = 33 * 4
    hand_dim = 21 * 3

    for t in range(data.shape[0]):
        frame = data[t]

        pose = frame[:pose_dim].reshape(33, 4)

        left_shoulder = pose[11, :3]
        right_shoulder = pose[12, :3]

        # 如果肩膀没检测到，就跳过这一帧
        if np.all(left_shoulder == 0) or np.all(right_shoulder == 0):
            continue

        center = (left_shoulder + right_shoulder) / 2.0
        shoulder_width = np.linalg.norm(left_shoulder[:2] - right_shoulder[:2])

        if shoulder_width < 1e-6:
            continue

        # 归一化 pose
        pose[:, 0:3] = (pose[:, 0:3] - center) / shoulder_width
        frame[:pose_dim] = pose.reshape(-1)

        # 归一化 left hand
        left_start = pose_dim
        left_end = pose_dim + hand_dim
        left_hand = frame[left_start:left_end].reshape(21, 3)

        if not np.all(left_hand == 0):
            left_hand = (left_hand - center) / shoulder_width
            frame[left_start:left_end] = left_hand.reshape(-1)

        # 归一化 right hand
        right_start = pose_dim + hand_dim
        right_end = pose_dim + hand_dim * 2
        right_hand = frame[right_start:right_end].reshape(21, 3)

        if not np.all(right_hand == 0):
            right_hand = (right_hand - center) / shoulder_width
            frame[right_start:right_end] = right_hand.reshape(-1)

        data[t] = frame

    return data


# ================= Dataset =================
class SignDataset(Dataset):
    def __init__(self, samples, labels, normalize=True):
        self.samples = samples
        self.labels = labels
        self.normalize = normalize

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        path = self.samples[idx]
        label = self.labels[idx]

        data = np.load(path).astype(np.float32)

        if data.ndim != 2:
            raise ValueError(f"数据维度错误: {path}, shape={data.shape}")

        # 防止帧数不一致
        if data.shape[0] > SEQ_LEN:
            data = data[:SEQ_LEN]
        elif data.shape[0] < SEQ_LEN:
            pad = np.zeros((SEQ_LEN - data.shape[0], data.shape[1]), dtype=np.float32)
            data = np.concatenate([data, pad], axis=0)

        if data.shape[1] != INPUT_SIZE:
            raise ValueError(f"特征维度错误: {path}, shape={data.shape}")

        if self.normalize:
            data = normalize_keypoints(data)

        return torch.tensor(data, dtype=torch.float32), torch.tensor(label, dtype=torch.long)


# ================= 模型 =================
class GRUClassifier(nn.Module):
    def __init__(self, input_size, hidden_size, num_classes):
        super().__init__()

        self.gru = nn.GRU(
            input_size=input_size,
            hidden_size=hidden_size,
            num_layers=NUM_LAYERS,
            batch_first=True,
            dropout=DROPOUT,
            bidirectional=True
        )

        self.fc = nn.Sequential(
            nn.Linear(hidden_size * 2, 128),
            nn.ReLU(),
            nn.Dropout(DROPOUT),
            nn.Linear(128, num_classes)
        )

    def forward(self, x):
        out, _ = self.gru(x)
        last = out[:, -1, :]
        return self.fc(last)


# ================= 加载数据 =================
def load_dataset(root_dir):
    classes = sorted([
        d for d in os.listdir(root_dir)
        if os.path.isdir(os.path.join(root_dir, d))
    ])

    class_to_idx = {cls: i for i, cls in enumerate(classes)}

    samples = []
    labels = []

    for cls in classes:
        cls_dir = os.path.join(root_dir, cls)
        files = sorted([f for f in os.listdir(cls_dir) if f.endswith(".npy")])

        print(f"{cls}: {len(files)} 段")

        for f in files:
            samples.append(os.path.join(cls_dir, f))
            labels.append(class_to_idx[cls])

    return samples, labels, classes


# ================= 评估 =================
def evaluate(model, loader, device, classes):
    model.eval()

    all_preds = []
    all_labels = []

    correct = 0
    total = 0

    with torch.no_grad():
        for x, y in loader:
            x = x.to(device)
            y = y.to(device)

            logits = model(x)
            preds = logits.argmax(dim=1)

            correct += (preds == y).sum().item()
            total += y.size(0)

            all_preds.extend(preds.cpu().numpy().tolist())
            all_labels.extend(y.cpu().numpy().tolist())

    acc = correct / total if total > 0 else 0

    cm = confusion_matrix(all_labels, all_preds)
    report = classification_report(
        all_labels,
        all_preds,
        target_names=classes,
        digits=4,
        zero_division=0
    )

    return acc, cm, report


# ================= 主函数 =================
def main():
    set_seed(SEED)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print("使用设备:", device)

    samples, labels, classes = load_dataset(DATASET_DIR)
    num_classes = len(classes)

    print("类别:", classes)
    print("总样本数:", len(samples))

    with open(LABEL_MAP_PATH, "w", encoding="utf-8") as f:
        json.dump(classes, f, ensure_ascii=False, indent=4)

    train_samples, val_samples, train_labels, val_labels = train_test_split(
        samples,
        labels,
        test_size=0.2,
        random_state=SEED,
        stratify=labels
    )

    train_dataset = SignDataset(train_samples, train_labels, normalize=True)
    val_dataset = SignDataset(val_samples, val_labels, normalize=True)

    train_loader = DataLoader(
        train_dataset,
        batch_size=BATCH_SIZE,
        shuffle=True,
        drop_last=False
    )

    val_loader = DataLoader(
        val_dataset,
        batch_size=BATCH_SIZE,
        shuffle=False,
        drop_last=False
    )

    model = GRUClassifier(
        input_size=INPUT_SIZE,
        hidden_size=HIDDEN_SIZE,
        num_classes=num_classes
    ).to(device)

    criterion = nn.CrossEntropyLoss()
    optimizer = torch.optim.AdamW(model.parameters(), lr=LR, weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(
        optimizer,
        mode="max",
        factor=0.5,
        patience=10
    )

    best_acc = 0.0

    for epoch in range(EPOCHS):
        model.train()

        train_loss = 0.0
        train_correct = 0
        train_total = 0

        for x, y in train_loader:
            x = x.to(device)
            y = y.to(device)

            optimizer.zero_grad()

            logits = model(x)
            loss = criterion(logits, y)

            loss.backward()
            optimizer.step()

            train_loss += loss.item()

            preds = logits.argmax(dim=1)
            train_correct += (preds == y).sum().item()
            train_total += y.size(0)

        train_acc = train_correct / train_total

        val_acc, _, _ = evaluate(model, val_loader, device, classes)

        scheduler.step(val_acc)

        print(
            f"Epoch [{epoch + 1}/{EPOCHS}] "
            f"Loss: {train_loss:.4f} "
            f"Train Acc: {train_acc:.4f} "
            f"Val Acc: {val_acc:.4f} "
            f"LR: {optimizer.param_groups[0]['lr']:.6f}"
        )

        if val_acc > best_acc:
            best_acc = val_acc
            torch.save(model.state_dict(), SAVE_BEST)
            print(f"保存最佳模型: {SAVE_BEST}")

        torch.save(model.state_dict(), SAVE_LAST)

    print("\n训练完成")
    print("最佳验证准确率:", best_acc)

    print("\n加载最佳模型进行最终评估...")
    model.load_state_dict(torch.load(SAVE_BEST, map_location=device))

    val_acc, cm, report = evaluate(model, val_loader, device, classes)

    print("\n最终验证准确率:", val_acc)

    print("\n混淆矩阵:")
    print(cm)

    print("\n分类报告:")
    print(report)

    np.save("confusion_matrix.npy", cm)

    with open("classification_report.txt", "w", encoding="utf-8") as f:
        f.write(report)

    print("\n已保存:")
    print("sign_gru_best.pth")
    print("sign_gru_last.pth")
    print("label_map.json")
    print("confusion_matrix.npy")
    print("classification_report.txt")


if __name__ == "__main__":
    main()