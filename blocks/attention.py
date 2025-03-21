import math
import torch.nn as nn
import torch


class SEBlock(nn.Module):
    def __init__(self, channels, reduction=16):
        super(SEBlock, self).__init__()
        self.fc1 = nn.Linear(channels, channels // reduction, bias=False)
        self.relu = nn.ReLU(inplace=True)
        self.fc2 = nn.Linear(channels // reduction, channels, bias=False)
        self.sigmoid = nn.Sigmoid()

    def forward(self, x, num_intersections):
        b, c, _, _ = x.size()
        # Custom pooling that normalizes by num_intersections for each sample in batch
        y = x.sum(dim=(2, 3)) / num_intersections.view(b, 1)
        y = self.fc1(y)
        y = self.relu(y)
        y = self.fc2(y)
        y = self.sigmoid(y)
        y = y.view(b, c, 1, 1)
        return x * y


class ECABlock(nn.Module):
    def __init__(self, k=5):
        super(ECABlock, self).__init__()
        self.conv1d = nn.Conv1d(1, 1, kernel_size=k,
                                padding=(k - 1) // 2, bias=False)
        self.sigmoid = nn.Sigmoid()

    def forward(self, x, num_intersections):
        # Input shape: (batch_size, channels, height, width)
        b, _, _, _ = x.size()
        y = x.sum(dim=(2, 3)) / num_intersections.view(b, 1)
        # Reshape to (batch_size, 1, channels) for Conv1d
        y = y.unsqueeze(1)  # Shape: (batch_size, 1, channels)
        y = self.conv1d(y)  # Shape: (batch_size, 1, channels)
        # Reshape back to (batch_size, channels, 1, 1)
        y = y.squeeze(1).unsqueeze(-1).unsqueeze(-1)
        y = self.sigmoid(y)  # Shape: (batch_size, channels, 1, 1)
        # Shape: (batch_size, channels, height, width)
        return x * y


class ELABlock(nn.Module):
    """
    Efficient Local Attention (ELA) Block that applies separate horizontal and vertical
    attention mechanisms to capture spatial dependencies efficiently.
    """

    def __init__(self, channels, kernel_size=7):
        super(ELABlock, self).__init__()
        self.pad = kernel_size // 2

        # Shared convolution for both pathways
        self.conv = nn.Conv1d(
            channels, channels, kernel_size=kernel_size,
            padding=self.pad, groups=channels // 2, bias=False
        )
        
        # Shared group normalization for both pathways
        self.gn = nn.GroupNorm(16, channels)
        self.sigmoid = nn.Sigmoid()

    def forward(self, x, board_width, board_height):
        b, c, h, w = x.size()

        # Horizontal attention (along height dimension)
        x_h = torch.sum(x, dim=3, keepdim=False) / board_width.unsqueeze(1).unsqueeze(2)  # [b, c, h]
        x_h = self.conv(x_h)  # [b, c, h]
        x_h = self.gn(x_h)
        x_h = self.sigmoid(x_h)
        x_h = x_h.unsqueeze(-1)  # [b, c, h, 1]

        # Vertical attention (along width dimension)
        x_w = torch.sum(x, dim=2, keepdim=False) / board_height.unsqueeze(1).unsqueeze(2)  # [b, c, w]
        x_w = self.conv(x_w)  # [b, c, w]
        x_w = self.gn(x_w)
        x_w = self.sigmoid(x_w)
        x_w = x_w.unsqueeze(2)  # [b, c, 1, w]

        # Apply attention
        return x * x_h * x_w
