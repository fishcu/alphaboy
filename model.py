import torch
import torch.nn as nn
import torch.nn.functional as F


class NestedBottleneckBlock(nn.Module):
    """
        Implementation of "nested bottleneck residual nets" as depicted in
        https://raw.githubusercontent.com/lightvector/KataGo/master/images/docs/bottlenecknestedresblock.png
    """

    def __init__(self, channels: int):
        super(NestedBottleneckBlock, self).__init__()
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

        self.act = nn.ReLU(inplace=True)

    def forward(self, x):
        identity = x

        # First 1x1 conv
        out = self.act(self.bn1(x))
        out = self.conv1(out)

        # First pair of 3x3 convs
        temp = self.conv2(self.act(self.bn2(out)))
        out = self.conv3(self.act(self.bn3(temp))) + out

        # Second pair of 3x3 convs
        temp = self.conv4(self.act(self.bn4(out)))
        out = self.conv5(self.act(self.bn5(temp))) + out

        # Final 1x1 conv
        out = self.conv6(self.act(self.bn6(out)))

        return out + identity


class GoNet(nn.Module):
    def __init__(self, num_input_planes: int, num_input_features: int,
                 channels: int = 64, num_blocks: int = 4, c_head: int = 32):
        super(GoNet, self).__init__()

        # Input processing block
        self.input_process = nn.ModuleDict({
            'spatial_conv': nn.Conv2d(
                in_channels=num_input_planes,
                out_channels=channels,
                kernel_size=5,
                padding=2,
                bias=False
            ),
            'scalar_linear': nn.Linear(
                in_features=num_input_features,
                out_features=channels
            )
        })

        # Trunk
        self.blocks = nn.ModuleList([
            NestedBottleneckBlock(channels) for _ in range(num_blocks)
        ])
        self.trunk_final = nn.Sequential(
            nn.BatchNorm2d(channels),
            nn.ReLU(inplace=True)
        )

        # Head processing
        self.head_conv = nn.Conv2d(channels, c_head, kernel_size=1, bias=False)

        # Policy head
        self.policy_conv = nn.Conv2d(channels, 1, kernel_size=1)
        # 2 because of avg+max pooling
        self.policy_pass = nn.Linear(c_head * 2, 1)

        # Value head
        self.value = nn.Linear(c_head * 2, 1)

        # Convert model to channels_last format
        self = self.to(memory_format=torch.channels_last)

    def forward(self, x):
        spatial_input, scalar_input = x  # [N, H, W, C_in], [N, F]

        # Process spatial features
        spatial_features = spatial_input.permute(0, 3, 1, 2)  # [N, C_in, H, W]
        spatial_features = self.input_process.spatial_conv(spatial_features)

        # Process scalar features
        scalar_features = self.input_process.scalar_linear(
            scalar_input)  # [N, C_out]
        scalar_features = scalar_features.reshape(
            scalar_features.shape[0], -1, 1, 1)  # [N, C_out, 1, 1]

        # Combine features
        x = spatial_features + scalar_features  # [N, C_out, H, W]

        # Process through trunk
        for block in self.blocks:
            x = block(x)
        trunk_output = self.trunk_final(x)  # [N, C, H, W]

        # Head features processing
        head_features = self.head_conv(trunk_output)  # [N, c_head, H, W]

        # Global pooling (both average and max)
        avg_pooled = F.adaptive_avg_pool2d(
            head_features, 1).squeeze(-1).squeeze(-1)  # [N, c_head]
        max_pooled = F.adaptive_max_pool2d(
            head_features, 1).squeeze(-1).squeeze(-1)  # [N, c_head]
        pooled_features = torch.cat(
            [avg_pooled, max_pooled], dim=1)  # [N, c_head*2]

        # Policy head
        policy_map = self.policy_conv(trunk_output)  # [N, 1, H, W]
        policy_map = policy_map.squeeze(1)  # [N, H, W]
        policy_pass = self.policy_pass(pooled_features)  # [N, 1]

        # Value head
        value = self.value(pooled_features)  # [N, 1]

        # Convert policy map back to NHWC format and flatten for board positions
        policy_map = policy_map.reshape(policy_map.shape[0], -1)  # [N, H*W]

        # Combine policy map with pass move and apply softmax
        policy_logits = torch.cat(
            [policy_map, policy_pass], dim=1)  # [N, H*W + 1]
        # Softmax over all moves including pass
        policy = F.softmax(policy_logits, dim=1)

        # Apply sigmoid to value
        value = torch.sigmoid(value)

        return policy, value
