import torch
import torch.nn as nn
from blocks.activation import Swish
from blocks.attention import SEBlock
import math


class GhostModule(nn.Module):
    def __init__(self, in_channels, out_channels, ratio, activation=True):
        super(GhostModule, self).__init__()
        self.out_channels = out_channels
        primary_channels = math.ceil(out_channels / ratio)
        cheap_channels = out_channels - primary_channels

        # Primary convolution
        self.primary_conv = nn.Sequential(
            nn.Conv2d(in_channels, primary_channels,
                      kernel_size=1, bias=False),
            nn.BatchNorm2d(primary_channels),
            Swish() if activation else nn.Sequential(),
        )

        # Cheap operation
        self.cheap_operation = nn.Sequential(
            nn.Conv2d(primary_channels, cheap_channels, kernel_size=3,
                      padding=1, groups=primary_channels, bias=False),
            nn.BatchNorm2d(cheap_channels),
            Swish() if activation else nn.Sequential(),
        )

    def forward(self, x):
        # Generate primary features
        primary_feat = self.primary_conv(x)

        # Generate ghost features via cheap operations
        cheap_feat = self.cheap_operation(primary_feat)

        # Concatenate primary and ghost features
        output = torch.cat([primary_feat, cheap_feat], dim=1)

        # Slice to match desired output channels
        return output[:, :self.out_channels, :, :]


class GhostBottleneckBlock(nn.Module):
    def __init__(self, channels, expand_channels, ghost_ratio=2):
        super(GhostBottleneckBlock, self).__init__()

        # Point-wise expansion via Ghost module
        self.ghost1 = GhostModule(
            channels, expand_channels, ghost_ratio, activation=True)

        # Squeeze-and-Excitation block
        self.se = SEBlock(expand_channels, reduction=16)

        # Point-wise linear projection via Ghost module
        self.ghost2 = GhostModule(
            expand_channels, channels, ghost_ratio, activation=False)

    def forward(self, x):
        # Store residual (for identity shortcut)
        residual = x

        # Ghost module 1: expansion
        x = self.ghost1(x)

        # Squeeze-and-Excitation
        x = self.se(x)

        # Ghost module 2: projection
        x = self.ghost2(x)

        # Residual connection
        return x + residual
