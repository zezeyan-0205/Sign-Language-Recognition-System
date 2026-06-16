import torch
import torch.nn as nn


SEQ_LEN = 30
INPUT_SIZE = 258


class TemporalBlock(nn.Module):
    def __init__(self, in_channels, out_channels, kernel_size, dilation, dropout):
        super().__init__()
        padding = dilation * (kernel_size - 1) // 2
        self.net = nn.Sequential(
            nn.Conv1d(
                in_channels,
                out_channels,
                kernel_size=kernel_size,
                padding=padding,
                dilation=dilation,
                bias=False,
            ),
            nn.BatchNorm1d(out_channels),
            nn.ReLU(inplace=True),
            nn.Dropout(dropout),
            nn.Conv1d(
                out_channels,
                out_channels,
                kernel_size=kernel_size,
                padding=padding,
                dilation=dilation,
                bias=False,
            ),
            nn.BatchNorm1d(out_channels),
            nn.ReLU(inplace=True),
            nn.Dropout(dropout),
        )
        self.residual = (
            nn.Identity()
            if in_channels == out_channels
            else nn.Conv1d(in_channels, out_channels, kernel_size=1)
        )
        self.relu = nn.ReLU(inplace=True)

    def forward(self, x):
        return self.relu(self.net(x) + self.residual(x))


class TCNClassifier(nn.Module):
    """RKNN-friendly temporal classifier built from Conv1d blocks."""

    def __init__(
        self,
        input_size,
        num_classes,
        channels=(128, 128, 128),
        kernel_size=3,
        dropout=0.2,
        seq_len=SEQ_LEN,
    ):
        super().__init__()
        if kernel_size % 2 == 0:
            raise ValueError("kernel_size must be odd to preserve sequence length")

        blocks = []
        in_channels = input_size
        for index, out_channels in enumerate(channels):
            blocks.append(
                TemporalBlock(
                    in_channels=in_channels,
                    out_channels=out_channels,
                    kernel_size=kernel_size,
                    dilation=2 ** index,
                    dropout=dropout,
                )
            )
            in_channels = out_channels

        self.features = nn.Sequential(*blocks)
        self.pool = nn.AvgPool1d(kernel_size=seq_len)
        self.classifier = nn.Sequential(
            nn.Flatten(),
            nn.Linear(in_channels, 128),
            nn.ReLU(inplace=True),
            nn.Dropout(dropout),
            nn.Linear(128, num_classes),
        )

    def forward(self, x):
        # Input contract: [batch, seq_len, feature_dim].
        x = x.transpose(1, 2)
        x = self.features(x)
        x = self.pool(x)
        return self.classifier(x)
