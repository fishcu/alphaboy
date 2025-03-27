import torch
import torch.nn as nn
import torch.nn.functional as F
import os
from datetime import datetime
import torch.optim as optim
import math

from torchinfo import summary

import go_data_gen

from datagen import GoDataGenerator

import random

from blocks.pooling import GlobalAvgPool2d
from blocks.activation import Swish
from blocks.mobile_net_v2 import MobileNetV2Block
from blocks.shuffle_net_v2 import ShuffleNetV2Block
from blocks.ghost_net import GhostBottleneckBlock


class GoNet(nn.Module):
    def __init__(self, num_input_planes: int, num_input_features: int,
                 channels: int = 64, num_blocks: int = 16, c_head: int = 64):
        super(GoNet, self).__init__()

        # Store the static property as a class attribute
        # This avoids the pybind11 static property access during forward pass
        self.legal_move_plane_index = go_data_gen.Board.legal_move_plane_index
        self.on_board_plane_index = go_data_gen.Board.on_board_plane_index

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
            'bn': nn.BatchNorm2d(channels),
            'act': Swish()
        })

        # Trunk
        self.blocks = nn.ModuleList([
            MobileNetV2Block(channels, channels * 6) for _ in range(num_blocks)
        ])

        # Policy head for board moves
        self.policy_head = nn.Sequential(
            nn.Conv2d(in_channels=channels, out_channels=1,
                      kernel_size=1, bias=False),
            nn.BatchNorm2d(1),
            Swish(),
        )

        # Separate the GlobalAvgPool2d for cleaner forward pass
        self.global_pool = GlobalAvgPool2d()

        # Rest of pass and value head processing
        self.pass_and_value_processing = nn.Sequential(
            nn.Linear(in_features=channels, out_features=c_head),
            nn.BatchNorm1d(c_head),
            Swish(),
            nn.Linear(in_features=c_head, out_features=2)
        )

        # Convert model to channels_last format
        self = self.to(memory_format=torch.channels_last)

    def forward(self, spatial_input, scalar_input, board_width, board_height, num_intersections):
        # Use self.legal_move_plane_index instead of go_data_gen.Board.legal_move_plane_index
        legal_moves_mask = spatial_input[..., self.legal_move_plane_index]
        # [N, H, W]
        on_board_mask = spatial_input[..., self.on_board_plane_index]

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
        x = self.input_process.bn(x)
        x = self.input_process.act(x)

        # Process through trunk
        for block in self.blocks:
            x = block(x, on_board_mask, board_width,
                      board_height, num_intersections)

        # Policy head for board moves
        policy = self.policy_head(x)

        # Process through pass and value head and extract pass logit and value
        # Use the separated GlobalAvgPool2d with num_intersections
        pooled_x = self.global_pool(x, num_intersections)
        # Process through the remaining layers
        final_output = self.pass_and_value_processing(pooled_x)
        pass_logit, value = final_output.split(1, dim=-1)
        pass_logit, value = pass_logit.squeeze(-1), value.squeeze(-1)

        # Process policy output with legal moves mask
        combined_policy = self.process_policy_output(
            policy, pass_logit, legal_moves_mask)

        return combined_policy, value

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
            'c_head': self.pass_and_value_processing[0].out_features
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

    # Extract board dimensions
    board_size = board.get_board_size()
    board_width = board_size.x
    board_height = board_size.y
    num_intersections = board_width * board_height

    # Get model predictions
    with torch.no_grad():
        combined_policy, value = model(
            spatial_features,
            scalar_features,
            board_width,
            board_height,
            num_intersections
        )

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
        channels=68,
        num_blocks=16,
        c_head=64
    )

    # Print model summary
    summary(model,
            input_size=[(1, go_data_gen.Board.data_size,
                        go_data_gen.Board.data_size,
                        go_data_gen.Board.num_feature_planes),
                        (1, go_data_gen.Board.num_feature_scalars),
                        (1,), (1,), (1,)],
            col_names=["input_size", "output_size", "num_params", "kernel_size",
                       "mult_adds"],
            col_width=20,
            row_settings=["var_names"])

    # Get data and move to GPU
    data_dir = "./data/val"
    generator = GoDataGenerator(data_dir, debug=False)
    spatial_batch, scalar_batch, policy_batch, value_batch, board_width_batch, board_height_batch, num_intersections_batch = generator.generate_batch(
        batch_size=2)

    spatial_batch = spatial_batch.cuda()
    scalar_batch = scalar_batch.cuda()
    board_width_batch = board_width_batch.cuda()
    board_height_batch = board_height_batch.cuda()
    num_intersections_batch = num_intersections_batch.cuda()

    # Forward pass
    policy_out, value_out = model(
        spatial_batch,
        scalar_batch,
        board_width_batch,
        board_height_batch,
        num_intersections_batch
    )

    # Print shapes to verify matching interfaces
    print("\nInput shapes:")
    print("Spatial input:", spatial_batch.shape)
    print("Scalar input:", scalar_batch.shape)
    print("Board width:", board_width_batch.shape)
    print("Board height:", board_height_batch.shape)
    print("Num intersections:", num_intersections_batch.shape)

    print("\nTarget shapes:")
    print("Policy target:", policy_batch.shape)
    print("Value target:", value_batch.shape)

    print("\nOutput shapes:")
    print("Policy output:", policy_out.shape)
    print("Value output:", value_out.shape)


if __name__ == "__main__":
    main()
