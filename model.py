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
            )
        })

        # Trunk
        self.blocks = nn.ModuleList([
            NestedBottleneckBlock(channels) for _ in range(num_blocks)
        ])
        self.trunk_final = nn.Sequential(
            nn.BatchNorm2d(channels),
            Swish()
        )

        # Policy head for board moves
        self.policy_conv = nn.Conv2d(channels, 1, kernel_size=1)

        # Shared processing for pass and value
        self.shared_fc = nn.Linear(channels, c_head)
        self.shared_act = Swish()

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

    def save_checkpoint(self, optimizer, scheduler, epoch, loss, save_dir):
        """Save model checkpoint including optimizer and scheduler states."""
        if not os.path.exists(save_dir):
            os.makedirs(save_dir)

        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        filename = f'model_epoch{epoch}_{timestamp}.pt'
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
            'type': 'AdamW',
            'lr': optimizer.param_groups[0]['lr'],
            'weight_decay': optimizer.param_groups[0]['weight_decay']
        }

        # Extract scheduler config
        scheduler_config = {}
        if isinstance(scheduler, torch.optim.lr_scheduler.SequentialLR):
            # Handle the warmup + cosine scheduler
            warmup_epochs = getattr(scheduler, 'warmup_epochs', scheduler._milestones[0])
            scheduler_config = {
                'type': 'SequentialLR',
                'warmup_epochs': warmup_epochs,
                'total_epochs': scheduler._schedulers[1].T_max + warmup_epochs,
                'initial_lr': optimizer_config['lr'],
                'final_lr': scheduler._schedulers[1].eta_min
            }
        elif isinstance(scheduler, torch.optim.lr_scheduler.CosineAnnealingLR):
            scheduler_config = {
                'type': 'CosineAnnealingLR',
                'T_max': scheduler.T_max,
                'eta_min': scheduler.eta_min
            }
        else:
            scheduler_config = {
                'type': str(type(scheduler).__name__)
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

        return filepath

    @classmethod
    def load_from_checkpoint(cls, checkpoint_path, device="cuda"):
        """Class method to create and load a model from checkpoint."""
        checkpoint = torch.load(checkpoint_path, map_location=device)

        # Create model instance from saved config
        model = cls(**checkpoint['model_config']).to(device)
        model.load_state_dict(checkpoint['model_state_dict'])

        # Create optimizer with saved config
        optimizer = optim.AdamW(
            model.parameters(),
            lr=checkpoint['optimizer_config']['lr'],
            weight_decay=checkpoint['optimizer_config']['weight_decay']
        )
        optimizer.load_state_dict(checkpoint['optimizer_state_dict'])

        # Create scheduler based on saved type
        scheduler_config = checkpoint['scheduler_config']
        if scheduler_config['type'] == 'CosineAnnealingLR':
            scheduler = optim.lr_scheduler.CosineAnnealingLR(
                optimizer,
                T_max=scheduler_config['T_max'],
                eta_min=scheduler_config['eta_min']
            )
        else:
            # For backward compatibility, convert any other scheduler to CosineAnnealingLR
            print(f"Converting {scheduler_config['type']} to CosineAnnealingLR")
            scheduler = optim.lr_scheduler.CosineAnnealingLR(
                optimizer,
                T_max=200,  # Default to 200 epochs
                eta_min=0.00001  # Default final learning rate
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


def main():
    random.seed(42)

    # Initialize model
    model = GoNet(
        num_input_planes=go_data_gen.Board.num_feature_planes,
        num_input_features=go_data_gen.Board.num_feature_scalars,
        channels=128,
        num_blocks=6,
        c_head=50
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
