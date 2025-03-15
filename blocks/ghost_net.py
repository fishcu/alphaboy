import torch
import torch.nn as nn
import math
from blocks.activation import Swish
from blocks.se import SEBlock


class GhostModule(nn.Module):
    """
    Ghost Module as described in "GhostNet: More Features from Cheap Operations"
    https://arxiv.org/abs/1911.11907
    """

    def __init__(self, inp, oup, kernel_size=1, ratio=2, dw_size=3, stride=1, relu=True):
        super(GhostModule, self).__init__()
        self.oup = oup
        init_channels = math.ceil(oup / ratio)
        new_channels = init_channels * (ratio - 1)

        # Primary convolution
        self.primary_conv = nn.Sequential(
            nn.Conv2d(inp, init_channels, kernel_size,
                      stride, kernel_size//2, bias=False),
            nn.BatchNorm2d(init_channels),
            FReLU(init_channels) if relu else nn.Identity()
        )

        # Cheap operation
        self.cheap_operation = nn.Sequential(
            nn.Conv2d(init_channels, new_channels, dw_size, 1, dw_size//2,
                      groups=init_channels, bias=False),
            nn.BatchNorm2d(new_channels),
            FReLU(new_channels) if relu else nn.Identity()
        )

    def forward(self, x):
        x1 = self.primary_conv(x)
        x2 = self.cheap_operation(x1)
        out = torch.cat([x1, x2], dim=1)
        # Slice to match desired output channels
        return out[:, :self.oup, :, :]


class GhostBottleneckBlock(nn.Module):
    """
    MobileNet-style bottleneck block with Ghost modules replacing 1x1 convolutions.
    """

    def __init__(self, squeeze_channels=64, expand_channels=256, ghost_ratio=2):
        super(GhostBottleneckBlock, self).__init__()

        # Expansion phase with Ghost module
        self.ghost1 = GhostModule(
            inp=squeeze_channels,
            oup=expand_channels,
            kernel_size=1,
            ratio=ghost_ratio,
            relu=True
        )

        # Depthwise phase (3x3 depthwise conv)
        self.depthwise = nn.Conv2d(
            in_channels=expand_channels,
            out_channels=expand_channels,
            kernel_size=3,
            padding=1,
            groups=expand_channels,
            bias=False
        )
        self.bn2 = nn.BatchNorm2d(expand_channels)
        self.activation = FReLU(expand_channels)

        # Projection phase with Ghost module
        self.ghost2 = GhostModule(
            inp=expand_channels,
            oup=squeeze_channels,
            kernel_size=1,
            ratio=ghost_ratio,
            relu=False  # No activation after projection
        )

        # Add Squeeze and Excitation block before the addition
        self.se_block = SEBlock(squeeze_channels)

    def forward(self, x):
        identity = x

        # Expansion with Ghost module
        out = self.ghost1(x)

        # Depthwise
        out = self.depthwise(out)
        out = self.bn2(out)
        out = self.activation(out)

        # Projection with Ghost module
        out = self.ghost2(out)

        # Apply SE block before the addition
        out = self.se_block(out)

        # Skip connection
        out = out + identity

        return out
