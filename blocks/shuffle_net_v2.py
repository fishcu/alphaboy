import torch
import torch.nn as nn
from blocks.activation import Swish
from blocks.attention import SEBlock


def channel_shuffle(x, groups):
    batch_size, channels, height, width = x.size()
    channels_per_group = channels // groups

    # Reshape
    x = x.view(batch_size, groups, channels_per_group, height, width)

    # Transpose
    x = x.transpose(1, 2).contiguous()

    # Flatten
    x = x.view(batch_size, -1, height, width)

    return x


class ShuffleNetV2Block(nn.Module):
    def __init__(self, channels):
        super(ShuffleNetV2Block, self).__init__()

        self.channels = channels
        self.split_channels = channels // 2

        # Right branch using sequential
        self.right_branch = nn.Sequential(
            # 1x1 convolution
            nn.Conv2d(self.split_channels, self.split_channels,
                      kernel_size=1, stride=1, padding=0, bias=False),
            nn.BatchNorm2d(self.split_channels),
            Swish(),

            # Depthwise convolution
            nn.Conv2d(self.split_channels, self.split_channels, kernel_size=3, stride=1,
                      padding=1, groups=self.split_channels, bias=False),
            nn.BatchNorm2d(self.split_channels),

            # Pointwise convolution
            nn.Conv2d(self.split_channels, self.split_channels,
                      kernel_size=1, stride=1, padding=0, bias=False),
            nn.BatchNorm2d(self.split_channels),

            # Squeeze and Excitation block
            SEBlock(self.split_channels, reduction=2)
        )

        # Activation for after residual connection
        self.act = Swish()

    def forward(self, x):
        # Channel split
        x_left, x_right = torch.split(
            x, [self.split_channels, self.split_channels], dim=1)

        # Right branch processing
        out_right = self.right_branch(x_right)

        # Residual connection and activation
        out_right = out_right + x_right
        out_right = self.act(out_right)

        # Concatenate branches
        out = torch.cat([x_left, out_right], dim=1)

        # Channel shuffle
        out = channel_shuffle(out, groups=2)

        return out
