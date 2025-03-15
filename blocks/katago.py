import torch
import torch.nn as nn
from blocks.activation import Swish


class KataGoBlock(nn.Module):
    """
        Implementation of "nested bottleneck residual nets" as depicted in
        https://raw.githubusercontent.com/lightvector/KataGo/master/images/docs/bottlenecknestedresblock.png
    """

    def __init__(self, channels: int):
        super(KataGoBlock, self).__init__()
        half_channels = channels // 2

        # Main branch (1x1 conv)
        self.bn1 = nn.BatchNorm2d(channels)
        self.conv1 = nn.Conv2d(channels, half_channels,
                               kernel_size=1, bias=False)

        # Two 3x3 conv layers
        self.bn2 = nn.BatchNorm2d(half_channels)
        self.conv2 = nn.Conv2d(half_channels, half_channels,
                               kernel_size=3, padding=1, bias=False)
        self.bn3 = nn.BatchNorm2d(half_channels)
        self.conv3 = nn.Conv2d(half_channels, half_channels,
                               kernel_size=3, padding=1, bias=False)

        # Two more 3x3 conv layers
        self.bn4 = nn.BatchNorm2d(half_channels)
        self.conv4 = nn.Conv2d(half_channels, half_channels,
                               kernel_size=3, padding=1, bias=False)
        self.bn5 = nn.BatchNorm2d(half_channels)
        self.conv5 = nn.Conv2d(half_channels, half_channels,
                               kernel_size=3, padding=1, bias=False)

        # Final 1x1 conv
        self.bn6 = nn.BatchNorm2d(half_channels)
        self.conv6 = nn.Conv2d(half_channels, channels,
                               kernel_size=1, bias=False)

        self.act = Swish()

    def forward(self, x):
        identity = x

        # First 1x1 conv
        out = self.conv1(self.act(self.bn1(x)))

        # First pair of 3x3 convs
        temp = self.conv2(self.act(self.bn2(out)))
        out = self.conv3(self.act(self.bn3(temp))) + out

        # Second pair of 3x3 convs
        temp = self.conv4(self.act(self.bn4(out)))
        out = self.conv5(self.act(self.bn5(temp))) + out

        # Final 1x1 conv
        out = self.conv6(self.act(self.bn6(out)))

        return out + identity
