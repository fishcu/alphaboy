import os
from datetime import datetime
from tqdm import tqdm
from torch.utils.tensorboard import SummaryWriter

import torch
import torch.nn as nn
import torch.optim as optim
import torch.nn.functional as F

import go_data_gen

from datagen import GoDataGenerator
from model import GoNet

import random


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


def validate(data_generator, model, validation_samples, batch_size, device):
    model.eval()
    total_value_loss = 0
    correct_moves = 0
    total_moves = 0

    value_criterion = nn.MSELoss()
    num_batches = (validation_samples + batch_size -
                   1) // batch_size  # Ceiling division

    with torch.no_grad():
        pbar = tqdm(range(num_batches), desc="Validating", unit="batch")
        for batch_idx in pbar:
            # Calculate size of current batch (last batch might be smaller)
            current_batch_size = min(
                batch_size, validation_samples - batch_idx * batch_size)

            spatial_batch, scalar_batch, policy_batch, value_batch = data_generator.generate_batch(
                current_batch_size)

            spatial_batch = spatial_batch.to(device)
            scalar_batch = scalar_batch.to(device)
            policy_batch = policy_batch.to(device)
            value_batch = value_batch.to(device)

            combined_policy, value_out = model(spatial_batch, scalar_batch)

            # Apply sigmoid to value_out before computing loss
            value_out = torch.sigmoid(value_out)

            # Calculate accuracy
            pred_moves = torch.argmax(combined_policy, dim=1)
            correct_moves += (pred_moves == policy_batch).sum().item()
            total_moves += policy_batch.size(0)

            value_loss = value_criterion(value_out, value_batch)
            total_value_loss += value_loss.item() * current_batch_size

            # Update progress bar with current metrics
            current_accuracy = correct_moves / total_moves
            current_avg_loss = total_value_loss / total_moves
            pbar.set_postfix({
                'accuracy': f'{current_accuracy:.4f}',
                'value_loss': f'{current_avg_loss:.4f}'
            })

            # Close progress bar before single move validation
            if batch_idx == num_batches - 1 and current_batch_size > 0:
                pbar.close()
                print("\nEvaluating single move prediction:")
                validate_single_move(model,
                                     spatial_batch[-1].unsqueeze(0),
                                     scalar_batch[-1].unsqueeze(0),
                                     value_batch[-1].unsqueeze(0),
                                     data_generator)

        if num_batches > 0 and not pbar.disable:  # Only close if not already closed
            pbar.close()
        policy_accuracy = correct_moves / total_moves
        avg_value_loss = total_value_loss / total_moves
        print(
            f'Validation - Policy Accuracy: {policy_accuracy:.4f}, Value Loss: {avg_value_loss:.4f}')

    return policy_accuracy, avg_value_loss


def validate_single_move(model, spatial_batch, scalar_batch, value_batch, data_generator):
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
        pred_value = torch.sigmoid(value_out[0]).item()
        true_value = value_batch[0].item()
        print(f"\nPredicted game result: {pred_value:.3f}")
        print(f"True game result: {true_value:.3f}")


def main():
    # Fix random seed
    random.seed(42)
    torch.manual_seed(42)

    # Enable TensorFloat32 tensor cores for better performance on Ampere+ GPUs
    torch.set_float32_matmul_precision('high')

    # Training parameters
    steps_per_epoch = 1000
    num_epochs = 200
    warmup_epochs = 10
    batch_size = 128
    initial_learning_rate = 0.001
    final_learning_rate = 0.00001
    validation_size = 6400
    weight_decay = 0.0001

    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    save_dir = './checkpoints'

    # Create tensorboard writer
    log_dir = os.path.join('runs', datetime.now().strftime('%Y%m%d_%H%M%S'))
    writer = SummaryWriter(log_dir)

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

        # Compile the model for faster training
        if hasattr(torch, 'compile'):
            print("Using torch.compile() to accelerate training")
            model = torch.compile(
                model,
                mode="reduce-overhead",  # More reliable than max-autotune for complex models
                fullgraph=True,          # Compile the entire model graph
                dynamic=False,           # Use static shapes for better optimization
            )
        else:
            print("torch.compile() not available in this PyTorch version")
    else:
        print('Starting new training run')
        start_epoch = 0
        # Initialize model
        model = GoNet(
            num_input_planes=go_data_gen.Board.num_feature_planes,
            num_input_features=go_data_gen.Board.num_feature_scalars,
            channels=320,
            num_blocks=16,
            c_head=64
        ).to(device)

        # Compile the model for faster training
        if hasattr(torch, 'compile'):
            print("Using torch.compile() to accelerate training")
            model = torch.compile(
                model,
                mode="reduce-overhead",  # More reliable than max-autotune for complex models
                fullgraph=True,          # Compile the entire model graph
                dynamic=False,           # Use static shapes for better optimization
            )
        else:
            print("torch.compile() not available in this PyTorch version")

        # Initialize optimizer with the initial learning rate
        optimizer = optim.AdamW(
            model.parameters(), lr=initial_learning_rate, weight_decay=weight_decay)

        # Create cosine annealing scheduler
        scheduler = optim.lr_scheduler.CosineAnnealingLR(
            optimizer,
            T_max=num_epochs,
            eta_min=final_learning_rate
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
            val_generator, model, validation_size, batch_size, device)

        # Save checkpoint every epoch
        total_loss = train_policy_loss + train_value_loss
        model.save_checkpoint(optimizer, scheduler,
                              epoch + 1, total_loss, save_dir)

        # Log metrics to tensorboard
        writer.add_scalar('Validation/Policy_Accuracy', val_policy_acc, epoch)
        writer.add_scalar('Validation/Value_MSE', val_value_loss, epoch)
        writer.add_scalar('Training/Policy_Loss', train_policy_loss, epoch)
        writer.add_scalar('Training/Value_Loss', train_value_loss, epoch)
        writer.add_scalar('Training/Learning_Rate',
                          scheduler.get_last_lr()[0], epoch)

    writer.close()


if __name__ == "__main__":
    main()
