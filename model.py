import torch
import torch.nn as nn
import torch.nn.functional as F
import os
from datetime import datetime
import torch.optim as optim

from torchinfo import summary

import go_data_gen

from datagen import GoDataGenerator

import random


class Swish(nn.Module):
    def __init__(self, beta=1.0):
        super(Swish, self).__init__()
        self.beta = beta

    def forward(self, x):
        return x * torch.sigmoid(self.beta * x)


class SEBlock(nn.Module):
    def __init__(self, channels, reduction=16):
        super(SEBlock, self).__init__()
        self.avg_pool = nn.AdaptiveAvgPool2d(1)
        self.fc1 = nn.Linear(channels, channels // reduction, bias=False)
        self.relu = nn.ReLU(inplace=True)
        self.fc2 = nn.Linear(channels // reduction, channels, bias=False)
        self.sigmoid = nn.Sigmoid()

    def forward(self, x):
        b, c, _, _ = x.size()
        y = self.avg_pool(x).view(b, c)
        y = self.fc1(y)
        y = self.relu(y)
        y = self.fc2(y)
        y = self.sigmoid(y)
        y = y.view(b, c, 1, 1)
        return x * y


class ShuffleNetBlock(nn.Module):
    """
    Implementation of a ShuffleNet block based on the original publication.
    Features:
    - Group convolutions for the 1x1 convolutions
    - Channel shuffle operation
    - Depthwise separable convolution
    - Residual connection
    """

    def __init__(self, squeeze_channels=64, expand_channels=256, groups=4):
        super(ShuffleNetBlock, self).__init__()

        # Ensure that expand_channels is divisible by groups
        assert expand_channels % groups == 0, "Expand channels must be divisible by groups"

        # First 1x1 grouped convolution for bottleneck
        self.conv1 = nn.Conv2d(
            in_channels=squeeze_channels,
            out_channels=expand_channels,
            kernel_size=1,
            groups=groups,
            bias=False
        )
        self.bn1 = nn.BatchNorm2d(expand_channels)
        self.swish1 = Swish()

        # Channel shuffle operation
        self.groups = groups

        # Depthwise 3x3 convolution
        self.conv2 = nn.Conv2d(
            in_channels=expand_channels,
            out_channels=expand_channels,
            kernel_size=3,
            padding=1,
            groups=expand_channels,  # Depthwise convolution
            bias=False
        )
        self.bn2 = nn.BatchNorm2d(expand_channels)

        # Second 1x1 grouped convolution to restore channel dimension
        self.conv3 = nn.Conv2d(
            in_channels=expand_channels,
            out_channels=squeeze_channels,
            kernel_size=1,
            groups=groups,
            bias=False
        )
        self.bn3 = nn.BatchNorm2d(squeeze_channels)

        # Final activation
        self.swish2 = Swish()

    def _channel_shuffle(self, x):
        batch_size, channels, height, width = x.size()
        channels_per_group = channels // self.groups

        # Reshape
        x = x.view(batch_size, self.groups, channels_per_group, height, width)

        # Transpose
        x = x.transpose(1, 2).contiguous()

        # Flatten
        x = x.view(batch_size, -1, height, width)

        return x

    def forward(self, x):
        residual = x

        # Bottleneck
        out = self.conv1(x)
        out = self.bn1(out)
        out = self.swish1(out)

        # Channel shuffle
        out = self._channel_shuffle(out)

        # Depthwise convolution
        out = self.conv2(out)
        out = self.bn2(out)

        # Pointwise convolution
        out = self.conv3(out)
        out = self.bn3(out)

        # Residual connection
        out = out + residual

        # Final activation
        out = self.swish2(out)

        return out


def channel_shuffle(x, groups):
    """
    Rearranges channels of the input tensor by dividing into groups and then transposing.

    Args:
        x (Tensor): Input tensor of shape [batch_size, channels, height, width]
        groups (int): Number of channel groups.

    Returns:
        Tensor: Channel-shuffled output tensor.
    """
    batch_size, num_channels, height, width = x.size()
    channels_per_group = num_channels // groups
    # Reshape: [batch_size, groups, channels_per_group, height, width]
    x = x.view(batch_size, groups, channels_per_group, height, width)
    # Transpose the group and channel dimensions
    x = torch.transpose(x, 1, 2).contiguous()
    # Flatten the tensor back to the original shape
    x = x.view(batch_size, -1, height, width)
    return x


class ShuffleNetBlockV2(nn.Module):
    """
    A simplified ShuffleNetV2 block supporting only the stride=1 variant.

    The input is split equally along the channel dimension into two halves. 
    The first half is left unchanged, while the second half is processed by:
      1. A 1×1 convolution followed by BatchNorm and ReLU.
      2. A depthwise 3×3 convolution with padding=1.
      3. A second 1×1 convolution followed by BatchNorm and ReLU.

    Finally, the two branches are concatenated and a channel shuffle is applied.

    Note: For stride=1, the number of input channels must equal the number of output channels.
    """

    def __init__(self, channels):
        super(ShuffleNetBlockV2, self).__init__()
        branch_channels = channels // 2
        self.branch2 = nn.Sequential(
            nn.Conv2d(branch_channels, branch_channels,
                      kernel_size=1, stride=1, padding=0, bias=False),
            nn.BatchNorm2d(branch_channels),
            nn.ReLU(inplace=True),
            nn.Conv2d(branch_channels, branch_channels, kernel_size=3,
                      stride=1, padding=1, groups=branch_channels, bias=False),
            nn.BatchNorm2d(branch_channels),
            nn.Conv2d(branch_channels, branch_channels,
                      kernel_size=1, stride=1, padding=0, bias=False),
            nn.BatchNorm2d(branch_channels),
            nn.ReLU(inplace=True)
        )

    def forward(self, x):
        # Split the input tensor along the channel dimension into two halves.
        x1, x2 = x.chunk(2, dim=1)
        # Process the second half and then concatenate with the first half.
        out = torch.cat((x1, self.branch2(x2)), dim=1)
        # Apply channel shuffle to mix the features from the two halves.
        out = channel_shuffle(out, 2)
        return out


class MobileNetBottleneckBlock(nn.Module):
    """
    Implementation of a MobileNet-style bottleneck block with depthwise separable convolutions.
    Enhanced with Squeeze and Excitation before the addition.
    """

    def __init__(self, squeeze_channels=64, expand_channels=256):
        super(MobileNetBottleneckBlock, self).__init__()

        # Expansion phase (1x1 conv)
        self.conv1 = nn.Conv2d(
            in_channels=squeeze_channels,
            out_channels=expand_channels,
            kernel_size=1,
            bias=False
        )
        self.bn1 = nn.BatchNorm2d(expand_channels)

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

        # Projection phase (1x1 conv)
        self.conv2 = nn.Conv2d(
            in_channels=expand_channels,
            out_channels=squeeze_channels,  # Project back to input channels
            kernel_size=1,
            bias=False
        )
        self.bn3 = nn.BatchNorm2d(squeeze_channels)

        # Add Squeeze and Excitation block before the addition
        self.se_block = SEBlock(squeeze_channels)

        # Activation function
        self.activation = Swish()

    def forward(self, x):
        identity = x

        # Expansion
        out = self.conv1(x)
        out = self.bn1(out)
        out = self.activation(out)

        # Depthwise
        out = self.depthwise(out)
        out = self.bn2(out)
        out = self.activation(out)

        # Projection
        out = self.conv2(out)
        out = self.bn3(out)

        # Apply SE block before the addition
        out = self.se_block(out)

        # Skip connection (always used since in_channels == out_channels)
        out = out + identity

        return out


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


class GoNet(nn.Module):
    def __init__(self, num_input_planes: int, num_input_features: int,
                 channels: int = 96, num_blocks: int = 4, c_head: int = 64):
        super(GoNet, self).__init__()

        # Input processing block
        self.input_process = nn.ModuleDict({
            'spatial_conv': nn.Conv2d(
                in_channels=num_input_planes,
                out_channels=channels,
                kernel_size=1,
                padding=0,
                bias=False
            ),
            'scalar_linear': nn.Linear(
                in_features=num_input_features,
                out_features=channels
            ),
            'bn': nn.BatchNorm2d(channels)
        })

        self.activation = Swish()

        # Trunk
        # self.blocks = nn.ModuleList([
        #     MobileNetBottleneckBlock(channels, channels * 4) for _ in range(num_blocks)
        # ])
        self.blocks = nn.ModuleList([
            ShuffleNetBlockV2(channels) for _ in range(num_blocks)
        ])
        self.trunk_final = nn.Sequential(
            nn.Conv2d(channels, channels, kernel_size=1, bias=False),
            nn.BatchNorm2d(channels),
            Swish()
        )

        # Policy head for board moves
        self.policy_conv = nn.Conv2d(channels, 1, kernel_size=1)

        # Shared processing for pass and value
        self.shared_fc = nn.Linear(channels, c_head)
        self.shared_act = self.activation

        # Combined pass and value head
        # 2 outputs: pass logit and value
        self.pass_value_fc = nn.Linear(c_head, 2)

        # Convert model to channels_last format
        self = self.to(memory_format=torch.channels_last)

    def forward(self, spatial_input, scalar_input):
        # Get legal moves mask from first feature plane before permuting
        legal_moves_mask = spatial_input[..., 0]  # [N, H, W]

        # Process spatial features
        # Convert from NHWC to NCHW format
        spatial_features = spatial_input.permute(0, 3, 1, 2)  # [N, C_in, H, W]
        spatial_features = self.input_process.spatial_conv(spatial_features)

        # Process scalar features
        scalar_features = self.input_process.scalar_linear(
            scalar_input)  # [N, C_out]
        scalar_features = scalar_features.reshape(
            scalar_features.shape[0], -1, 1, 1)  # [N, C_out, 1, 1]

        # Combine features
        x = spatial_features + scalar_features  # [N, C_out, H, W]
        x = self.activation(self.input_process.bn(x))

        # Process through trunk
        for block in self.blocks:
            x = block(x)
        trunk_output = self.trunk_final(x)  # [N, C, H, W]

        # Policy head for board moves
        policy = self.policy_conv(trunk_output)

        # Global average pooling for pass and value heads
        pooled = torch.mean(trunk_output, dim=(2, 3))  # [N, C]

        # Shared processing for pass and value
        shared = self.shared_fc(pooled)
        shared = self.shared_act(shared)

        # Pass and value head
        pass_logit, value = self.pass_value_fc(shared).split(1, dim=-1)

        # Squeeze last dimension from pass and value
        pass_logit = pass_logit.squeeze(-1)
        value = value.squeeze(-1)

        # Process policy output with legal moves mask
        combined_policy = self.process_policy_output(
            policy, pass_logit, legal_moves_mask)

        return combined_policy, value

    # New implementations for ShuffleNetBlock
    def save_checkpoint(self, optimizer, scheduler, epoch, loss, save_dir):
        """Save model checkpoint including optimizer and scheduler states."""
        if not os.path.exists(save_dir):
            os.makedirs(save_dir)

        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        filename = f'gonet_model_epoch{epoch}_{timestamp}.pt'
        filepath = os.path.join(save_dir, filename)

        # Save model hyperparameters
        model_config = {
            'num_input_planes': self.input_process.spatial_conv.in_channels,
            'num_input_features': self.input_process.scalar_linear.in_features,
            'channels': self.input_process.spatial_conv.out_channels,
            'num_blocks': len(self.blocks),
            'c_head': self.shared_fc.out_features
        }

        # Extract optimizer config
        optimizer_config = {
            'type': type(optimizer).__name__,
            'lr': optimizer.param_groups[0]['lr'],
            'weight_decay': optimizer.param_groups[0]['weight_decay']
        }

        # Extract scheduler config - only support CosineAnnealingLR
        scheduler_config = {
            'type': 'CosineAnnealingLR',
            'T_max': scheduler.T_max,
            'eta_min': scheduler.eta_min
        }

        torch.save({
            'epoch': epoch,
            'model_config': model_config,
            'optimizer_config': optimizer_config,
            'scheduler_config': scheduler_config,
            'model_state_dict': self.state_dict(),
            'optimizer_state_dict': optimizer.state_dict() if optimizer else None,
            'scheduler_state_dict': scheduler.state_dict() if scheduler else None,
            'loss': loss,
        }, filepath)

        print(f"Model checkpoint saved to {filepath}")
        return filepath

    @classmethod
    def load_from_checkpoint(cls, checkpoint_path, device="cuda"):
        """Class method to create and load a model from checkpoint."""
        checkpoint = torch.load(checkpoint_path, map_location=device)

        # Create model instance from saved config
        model = cls(**checkpoint['model_config']).to(device)

        # Try to load state dict
        try:
            model.load_state_dict(checkpoint['model_state_dict'])
        except Exception as e:
            print(f"Warning: Could not load model state directly: {e}")
            print("Attempting to load with strict=False to handle structure changes...")
            missing_keys, unexpected_keys = model.load_state_dict(
                checkpoint['model_state_dict'], strict=False
            )
            print(
                f"Model loaded with {len(missing_keys)} missing and {len(unexpected_keys)} unexpected keys")

        # Create optimizer based on saved config
        optimizer_config = checkpoint['optimizer_config']
        optimizer = optim.AdamW(
            model.parameters(),
            lr=optimizer_config['lr'],
            weight_decay=optimizer_config['weight_decay']
        )

        # Load optimizer state if available
        if 'optimizer_state_dict' in checkpoint and checkpoint['optimizer_state_dict']:
            try:
                optimizer.load_state_dict(checkpoint['optimizer_state_dict'])
            except Exception as e:
                print(f"Warning: Could not load optimizer state: {e}")
                print("Continuing with freshly initialized optimizer")

        # Create scheduler - only support CosineAnnealingLR
        scheduler_config = checkpoint['scheduler_config']
        scheduler = optim.lr_scheduler.CosineAnnealingLR(
            optimizer,
            T_max=scheduler_config.get('T_max', 100),
            eta_min=scheduler_config.get('eta_min', 1e-5)
        )

        # Load scheduler state if available
        if 'scheduler_state_dict' in checkpoint:
            try:
                scheduler.load_state_dict(checkpoint['scheduler_state_dict'])
            except Exception as e:
                print(f"Warning: Could not load scheduler state: {e}")
                print("Continuing with freshly initialized scheduler")

        return model, optimizer, scheduler, checkpoint['epoch'], checkpoint['loss']

    def process_policy_output(self, policy_out, pass_logit, legal_moves_mask=None):
        policy_out = policy_out.view(
            policy_out.size(0), -1)  # Flatten to (batch, H*W)
        combined_policy = torch.cat(
            [policy_out, pass_logit.unsqueeze(-1)], dim=1)

        if legal_moves_mask is not None:
            # Flatten the legal moves mask and add the pass move (always legal)
            legal_moves_mask = legal_moves_mask.view(
                legal_moves_mask.size(0), -1)
            pass_mask = torch.ones_like(pass_logit.unsqueeze(-1))
            full_mask = torch.cat([legal_moves_mask, pass_mask], dim=1)

            # Set illegal moves to -inf
            combined_policy = torch.where(
                full_mask == 1,
                combined_policy,
                torch.tensor(float('-inf')).to(combined_policy.device)
            )

        return combined_policy


def predict_move(model, board, color, device="cuda", temperature=0.01, allow_pass=True):
    """
    Predict a move for the given board position and color using the model.

    Args:
        model: The neural network model to use for prediction
        board: The current board state (go_data_gen.Board)
        color: The color to play (go_data_gen.Color)
        device: The device to run inference on ("cuda" or "cpu")
        temperature: Temperature for move sampling (0=deterministic, higher=more random)
        allow_pass: Whether to allow pass moves (set to False for handicap placement)

    Returns:
        tuple: (go_data_gen.Move, float) - The predicted move and the value prediction
    """
    # Get input features for current position
    spatial_features = torch.from_numpy(board.get_feature_planes(color))
    scalar_features = torch.from_numpy(board.get_feature_scalars(color))

    # Add batch dimension and move to device
    spatial_features = spatial_features.unsqueeze(0).to(device)
    scalar_features = scalar_features.unsqueeze(0).to(device)

    # Get model predictions
    with torch.no_grad():
        combined_policy, value = model(spatial_features, scalar_features)

        # If pass is not allowed, set the pass logit to a very negative value
        if not allow_pass:
            pass_idx = board.data_size * board.data_size
            combined_policy[0, pass_idx] = float('-inf')

        # Apply softmax to get probabilities
        policy_probs = F.softmax(combined_policy, dim=1)[0]

    # Sample move from policy distribution
    if temperature == 0:
        max_idx = torch.argmax(policy_probs).item()
    else:
        # Apply temperature scaling
        logits = torch.log(policy_probs)
        scaled_probs = F.softmax(logits / temperature, dim=0)
        # Sample from the distribution
        max_idx = torch.multinomial(scaled_probs, 1).item()

    pass_idx = board.data_size * board.data_size

    # Create the move
    if max_idx == pass_idx:  # Pass move index
        move = go_data_gen.Move(color, True, go_data_gen.Vec2(0, 0))
    else:
        mem_y = max_idx // board.data_size
        mem_x = max_idx % board.data_size
        x = mem_x - board.padding
        y = mem_y - board.padding
        move = go_data_gen.Move(color, False, go_data_gen.Vec2(x, y))

    # Return both the move and the value
    return move, value[0]


def main():
    random.seed(42)

    # Initialize model
    model = GoNet(
        num_input_planes=go_data_gen.Board.num_feature_planes,
        num_input_features=go_data_gen.Board.num_feature_scalars,
        channels=320,
        num_blocks=16,
        c_head=64
    )

    # Print model summary
    summary(model,
            input_size=[(1, go_data_gen.Board.data_size,
                        go_data_gen.Board.data_size,
                        go_data_gen.Board.num_feature_planes),
                        (1, go_data_gen.Board.num_feature_scalars)],
            col_names=["input_size", "output_size", "num_params", "kernel_size",
                       "mult_adds"],
            col_width=20,
            row_settings=["var_names"])

    # Get data and move to GPU
    data_dir = "./data/val"
    generator = GoDataGenerator(data_dir, debug=False)
    spatial_batch, scalar_batch, policy_batch, value_batch = generator.generate_batch(
        batch_size=2)

    spatial_batch = spatial_batch.cuda()
    scalar_batch = scalar_batch.cuda()

    # Forward pass
    policy_out, value_out = model(spatial_batch, scalar_batch)

    # Print shapes to verify matching interfaces
    print("\nInput shapes:")
    print("Spatial input:", spatial_batch.shape)
    print("Scalar input:", scalar_batch.shape)

    print("\nTarget shapes:")
    print("Policy target:", policy_batch.shape)
    print("Value target:", value_batch.shape)

    print("\nOutput shapes:")
    print("Policy output:", policy_out.shape)
    print("Value output:", value_out.shape)


if __name__ == "__main__":
    main()
