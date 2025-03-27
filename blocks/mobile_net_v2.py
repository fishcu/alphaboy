import torch
import torch.nn as nn
from blocks.activation import Swish
from blocks.attention import (
    SEBlock,
    CBAMBlock,
    ECABlock,
    CABlock,
    SimAMBlock,
    EMABlock,
    ELABlock,
    EANBlock,
    MECABlock,
    MVECABlock
)


class MobileNetV2Block(nn.Module):
    """
    Implementation of a MobileNet-style bottleneck block with depthwise separable convolutions.
    Enhanced with Squeeze and Excitation before the addition.
    """

    def __init__(self, in_channels=64, expand_channels=256):
        super(MobileNetV2Block, self).__init__()

        # Expansion phase (1x1 conv)
        self.conv1 = nn.Conv2d(
            in_channels=in_channels,
            out_channels=expand_channels,
            kernel_size=1,
            bias=False
        )
        self.bn1 = nn.BatchNorm2d(expand_channels)
        self.act1 = Swish()

        # Depthwise phase (3x3 depthwise conv)
        self.depthwise = nn.Conv2d(
            in_channels=expand_channels,
            out_channels=expand_channels,
            kernel_size=3,
            padding=1,
            groups=expand_channels,  # This makes it a depthwise convolution
            bias=False
        )
        self.bn2 = nn.BatchNorm2d(expand_channels)
        self.act2 = Swish()

        self.attention = EANBlock(expand_channels)

        # Projection phase (1x1 conv)
        self.conv2 = nn.Conv2d(
            in_channels=expand_channels,
            out_channels=in_channels,  # Project back to input channels
            kernel_size=1,
            bias=False
        )
        self.bn3 = nn.BatchNorm2d(in_channels)

    def forward(self, x, on_board_mask, board_width, board_height, num_intersections):
        identity = x

        # Expansion
        out = self.conv1(x)
        out = self.bn1(out)
        out = self.act1(out)

        # Depthwise
        out = self.depthwise(out)
        out = out * on_board_mask.unsqueeze(1)  # Mask off-board locations
        out = self.bn2(out)
        out = self.act2(out)

        # Attention
        out = self.attention(out)

        # Projection
        out = self.conv2(out)
        out = self.bn3(out)

        # Skip connection (always used since in_channels == out_channels)
        out = out + identity

        return out
