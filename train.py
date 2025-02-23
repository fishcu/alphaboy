import os
from datetime import datetime
from tqdm import tqdm

import torch
import torch.nn as nn
import torch.optim as optim
import torch.nn.functional as F

import go_data_gen

from datagen import GoDataGenerator
from model import GoNet


def process_model_output(policy_out, pass_logit, legal_moves_mask=None):
    """Process model policy output into combined policy logits.

    Args:
        policy_out: Raw policy output from model (batch, H*W)
        pass_logit: Pass move logits (batch, 1)
        legal_moves_mask: Optional tensor of legal moves (batch, H*W). If provided,
                         illegal moves will be masked to -inf before softmax
    """
    policy_out = policy_out.view(
        policy_out.size(0), -1)  # Flatten to (batch, H*W)
    combined_policy = torch.cat([policy_out, pass_logit], dim=1)

    if legal_moves_mask is not None:
        # Flatten the legal moves mask and add the pass move (always legal)
        legal_moves_mask = legal_moves_mask.view(legal_moves_mask.size(0), -1)
        pass_mask = torch.ones_like(pass_logit)
        full_mask = torch.cat([legal_moves_mask, pass_mask], dim=1)

        # Set illegal moves to -inf
        combined_policy = torch.where(
            full_mask == 1,
            combined_policy,
            torch.tensor(float('-inf')).to(combined_policy.device)
        )

    return combined_policy


def train_epoch(data_generator, model, batch_size, steps_per_epoch, optimizer, scheduler, device):
    model.train()
    total_policy_loss = 0
    total_value_loss = 0
    num_batches = 0

    policy_criterion = nn.CrossEntropyLoss()
    value_criterion = nn.BCEWithLogitsLoss()

    pbar = tqdm(range(steps_per_epoch), desc="Training", unit="batch")
    for _ in pbar:
        spatial_batch, scalar_batch, policy_batch, value_batch = data_generator.generate_batch(
            batch_size)

        spatial_batch = spatial_batch.to(device)
        scalar_batch = scalar_batch.to(device)
        policy_batch = policy_batch.to(device)
        value_batch = value_batch.to(device)

        optimizer.zero_grad()

        combined_policy, value_out = model(spatial_batch, scalar_batch)

        policy_loss = policy_criterion(combined_policy, policy_batch)
        value_loss = value_criterion(value_out, value_batch)
        loss = policy_loss + value_loss

        loss.backward()
        optimizer.step()

        total_policy_loss += policy_loss.item()
        total_value_loss += value_loss.item()
        num_batches += 1

        pbar.set_postfix({
            'policy_loss': f'{policy_loss.item():.4f}',
            'value_loss': f'{value_loss.item():.4f}'
        })

    pbar.close()
    scheduler.step()
    return total_policy_loss / num_batches, total_value_loss / num_batches


def validate(data_generator, model, validation_size, device):
    model.eval()
    total_value_loss = 0
    correct_moves = 0
    total_moves = 0

    value_criterion = nn.MSELoss()

    with torch.no_grad():
        spatial_batch, scalar_batch, policy_batch, value_batch = data_generator.generate_batch(
            validation_size)

        spatial_batch = spatial_batch.to(device)
        scalar_batch = scalar_batch.to(device)
        policy_batch = policy_batch.to(device)
        value_batch = value_batch.to(device)

        combined_policy, value_out = model(spatial_batch, scalar_batch)

        # Calculate accuracy
        pred_moves = torch.argmax(combined_policy, dim=1)
        correct_moves = (pred_moves == policy_batch).sum().item()
        total_moves = policy_batch.size(0)

        value_loss = value_criterion(value_out, value_batch)
        total_value_loss = value_loss.item()

        policy_accuracy = correct_moves / total_moves
        print(
            f'Validation - Policy Accuracy: {policy_accuracy:.4f}, Value Loss: {value_loss.item():.4f}')

        print("\nEvaluating single move prediction:")
        validate_single_move(model, spatial_batch, scalar_batch,
                             policy_batch, value_batch, data_generator, device)

    return correct_moves / total_moves, total_value_loss


def validate_single_move(model, spatial_batch, scalar_batch, policy_batch, value_batch, data_generator, device):
    model.eval()
    with torch.no_grad():
        # Get the board state and next move from the generator
        board = data_generator.current_board
        next_move = data_generator.current_move

        print("\nCurrent board state before move:")
        board.print()

        # Get model predictions
        combined_policy, value_out = model(spatial_batch, scalar_batch)
        policy_probs = F.softmax(combined_policy, dim=1)[0]

        # Print policy distribution
        print("\nPolicy distribution:")
        top_k = 5
        values, indices = torch.topk(policy_probs, top_k)
        for i in range(top_k):
            idx = indices[i].item()
            prob = values[i].item()
            if idx == board.data_size * board.data_size:  # Pass move index
                print(f"Pass move: {prob:.3f}")
            else:
                mem_y = idx // board.data_size
                mem_x = idx % board.data_size
                x = mem_x - board.padding
                y = mem_y - board.padding
                print(f"Move ({x}, {y}): {prob:.3f}")

        # Get the predicted move
        pred_idx = torch.argmax(combined_policy).item()
        print(f"\nSelected move index: {pred_idx}")

        if pred_idx == board.data_size * board.data_size:  # Pass move
            print("Predicted move: PASS")
            pred_move = go_data_gen.Move(
                next_move.color, True, go_data_gen.Vec2(0, 0))
        else:
            mem_y = pred_idx // board.data_size
            mem_x = pred_idx % board.data_size
            x = mem_x - board.padding
            y = mem_y - board.padding
            print(f"Predicted coordinates (memory): ({mem_x}, {mem_y})")
            print(f"Predicted coordinates (board): ({x}, {y})")
            pred_move = go_data_gen.Move(
                next_move.color, False, go_data_gen.Vec2(x, y))

        # Play the predicted move
        board.play(pred_move)
        print("\nBoard after playing move:")
        board.print(highlight_fn=lambda mem_x, mem_y: not next_move.is_pass and
                    mem_x - board.padding == next_move.coord.x and
                    mem_y - board.padding == next_move.coord.y)

        # Print move and value predictions
        print(f"\nModel pass probability: {policy_probs[-1]:.3f}")
        if next_move.is_pass:
            print("Target move: pass")
        else:
            print(f"Target move: ({next_move.coord.x}, {next_move.coord.y})")

        # Get the predicted and true values
        pred_value = value_out[0].item()
        true_value = value_batch[0].item()
        print(f"\nPredicted game result: {pred_value:.3f}")
        print(f"True game result: {true_value:.3f}")


def main():
    # Training parameters
    steps_per_epoch = 1000
    num_epochs = 200
    batch_size = 128
    initial_learning_rate = 0.005
    final_learning_rate = 0.00001
    validation_size = 1000

    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    save_dir = './checkpoints'

    # Parse command line arguments
    import argparse
    parser = argparse.ArgumentParser(description='Train Go model')
    parser.add_argument('--resume', type=str,
                        help='Path to checkpoint to resume from')
    args = parser.parse_args()

    if args.resume:
        print(f'Resuming from checkpoint: {args.resume}')
        model, optimizer, scheduler, start_epoch, _ = GoNet.load_from_checkpoint(
            args.resume, device)
        print(f'Resuming from epoch {start_epoch}')
    else:
        print('Starting new training run')
        start_epoch = 0
        # Initialize model
        model = GoNet(
            num_input_planes=go_data_gen.Board.num_feature_planes,
            num_input_features=go_data_gen.Board.num_feature_scalars,
            channels=128,
            num_blocks=6,
            c_head=32
        ).to(device)

        optimizer = optim.Adam(
            model.parameters(), lr=initial_learning_rate, weight_decay=1e-4)

        scheduler = optim.lr_scheduler.CosineAnnealingLR(
            optimizer,
            T_max=num_epochs,  # Total number of epochs
            eta_min=final_learning_rate  # Final learning rate
        )

    # Initialize data generators
    train_data_dir = "./data/train"
    val_data_dir = "./data/val"
    train_generator = GoDataGenerator(train_data_dir)
    val_generator = GoDataGenerator(val_data_dir)

    # Training loop
    for epoch in range(start_epoch, num_epochs):
        print(f'\nEpoch {epoch+1}/{num_epochs}:')
        train_policy_loss, train_value_loss = train_epoch(
            train_generator, model, batch_size, steps_per_epoch, optimizer, scheduler, device)
        print(f'Current learning rate: {scheduler.get_last_lr()[0]}')

        # Validate model
        val_policy_acc, val_value_loss = validate(
            val_generator, model, validation_size, device)
        print(
            f'Val - Policy Accuracy: {val_policy_acc:.4f}, Value Loss: {val_value_loss:.4f}')

        # Save checkpoint every epoch
        total_loss = train_policy_loss + train_value_loss
        model.save_checkpoint(optimizer, scheduler,
                              epoch + 1, total_loss, save_dir)


if __name__ == "__main__":
    main()
