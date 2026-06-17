import argparse
import json
import random
import sys
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from sklearn.metrics import classification_report, confusion_matrix
from sklearn.model_selection import train_test_split
from torch.utils.data import DataLoader, Dataset


PROJECT_ROOT = Path(__file__).resolve().parents[2]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from scripts.common.keypoint_features import (
    FEATURE_MODE_FULL,
    FEATURE_MODES,
    SEQ_LEN,
    get_feature_dim,
    prepare_sequence_file,
)
from scripts.common.sequence_models import TCNClassifier


def parse_args():
    parser = argparse.ArgumentParser(
        description="Train an RKNN-friendly TCN classifier on keypoint sequences."
    )
    parser.add_argument("--dataset", default="dataset_keypoints")
    parser.add_argument("--label-map", default="label_map.json")
    parser.add_argument("--save-best", default="artifacts/models/sign_tcn_best.pth")
    parser.add_argument("--save-last", default="artifacts/models/sign_tcn_last.pth")
    parser.add_argument("--epochs", type=int, default=120)
    parser.add_argument("--batch-size", type=int, default=16)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--weight-decay", type=float, default=1e-4)
    parser.add_argument("--dropout", type=float, default=0.2)
    parser.add_argument("--channels", default="128,128,128")
    parser.add_argument("--kernel-size", type=int, default=3)
    parser.add_argument("--feature-mode", choices=FEATURE_MODES, default=FEATURE_MODE_FULL)
    parser.add_argument("--val-ratio", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=42)
    return parser.parse_args()


def parse_channels(value):
    channels = tuple(int(item) for item in value.split(",") if item.strip())
    if not channels:
        raise ValueError("--channels must contain at least one channel size")
    return channels


def set_seed(seed):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    torch.cuda.manual_seed_all(seed)


class SignDataset(Dataset):
    def __init__(self, samples, labels, feature_mode):
        self.samples = samples
        self.labels = labels
        self.feature_mode = feature_mode

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, index):
        data = prepare_sequence_file(self.samples[index], self.feature_mode, SEQ_LEN)
        label = self.labels[index]
        return torch.tensor(data, dtype=torch.float32), torch.tensor(label, dtype=torch.long)


def load_dataset(root_dir):
    root = Path(root_dir)
    classes = sorted([path.name for path in root.iterdir() if path.is_dir()])
    if not classes:
        raise FileNotFoundError(f"No class folders found under {root}")

    class_to_index = {name: index for index, name in enumerate(classes)}
    samples = []
    labels = []
    for class_name in classes:
        files = sorted((root / class_name).glob("*.npy"))
        print(f"{class_name}: {len(files)} 段")
        for path in files:
            samples.append(path)
            labels.append(class_to_index[class_name])

    if not samples:
        raise FileNotFoundError(f"No .npy samples found under {root}")
    return samples, labels, classes


def evaluate(model, loader, device, classes):
    model.eval()
    correct = 0
    total = 0
    all_preds = []
    all_labels = []

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

    accuracy = correct / total if total else 0.0
    matrix = confusion_matrix(all_labels, all_preds)
    report = classification_report(
        all_labels,
        all_preds,
        target_names=classes,
        digits=4,
        zero_division=0,
    )
    return accuracy, matrix, report


def main():
    args = parse_args()
    channels = parse_channels(args.channels)
    input_size = get_feature_dim(args.feature_mode)
    set_seed(args.seed)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print("使用设备:", device)
    print("TCN channels:", channels)
    print("Feature mode:", args.feature_mode)
    print("Input size:", input_size)

    samples, labels, classes = load_dataset(args.dataset)
    print("类别:", classes)
    print("总样本数:", len(samples))

    label_path = Path(args.label_map)
    label_path.parent.mkdir(parents=True, exist_ok=True)
    with label_path.open("w", encoding="utf-8") as file:
        json.dump(classes, file, ensure_ascii=False, indent=4)

    train_samples, val_samples, train_labels, val_labels = train_test_split(
        samples,
        labels,
        test_size=args.val_ratio,
        random_state=args.seed,
        stratify=labels,
    )

    train_loader = DataLoader(
        SignDataset(train_samples, train_labels, args.feature_mode),
        batch_size=args.batch_size,
        shuffle=True,
        drop_last=False,
    )
    val_loader = DataLoader(
        SignDataset(val_samples, val_labels, args.feature_mode),
        batch_size=args.batch_size,
        shuffle=False,
        drop_last=False,
    )

    model = TCNClassifier(
        input_size=input_size,
        num_classes=len(classes),
        channels=channels,
        kernel_size=args.kernel_size,
        dropout=args.dropout,
        seq_len=SEQ_LEN,
    ).to(device)

    criterion = nn.CrossEntropyLoss()
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=args.lr, weight_decay=args.weight_decay
    )
    scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(
        optimizer, mode="max", factor=0.5, patience=10
    )

    best_acc = 0.0
    best_path = Path(args.save_best)
    last_path = Path(args.save_last)
    best_path.parent.mkdir(parents=True, exist_ok=True)
    last_path.parent.mkdir(parents=True, exist_ok=True)

    for epoch in range(args.epochs):
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

        train_acc = train_correct / train_total if train_total else 0.0
        val_acc, _, _ = evaluate(model, val_loader, device, classes)
        scheduler.step(val_acc)

        print(
            f"Epoch [{epoch + 1}/{args.epochs}] "
            f"Loss: {train_loss:.4f} "
            f"Train Acc: {train_acc:.4f} "
            f"Val Acc: {val_acc:.4f} "
            f"LR: {optimizer.param_groups[0]['lr']:.6f}"
        )

        if val_acc > best_acc:
            best_acc = val_acc
            torch.save(model.state_dict(), best_path)
            print(f"保存最佳模型: {best_path}")

        torch.save(model.state_dict(), last_path)

    print("\n训练完成")
    print("最佳验证准确率:", best_acc)

    print("\n加载最佳模型进行最终评估...")
    model.load_state_dict(torch.load(best_path, map_location=device))
    val_acc, matrix, report = evaluate(model, val_loader, device, classes)

    print("\n最终验证准确率:", val_acc)
    print("\n混淆矩阵:")
    print(matrix)
    print("\n分类报告:")
    print(report)

    output_dir = best_path.parent
    np.save(output_dir / "sign_tcn_confusion_matrix.npy", matrix)
    with (output_dir / "sign_tcn_classification_report.txt").open(
        "w", encoding="utf-8"
    ) as file:
        file.write(report)

    print("\n已保存:")
    print(best_path)
    print(last_path)
    print(label_path)
    print(output_dir / "sign_tcn_confusion_matrix.npy")
    print(output_dir / "sign_tcn_classification_report.txt")


if __name__ == "__main__":
    main()
