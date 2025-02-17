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


def train_epoch(model, data_generator, optimizer, scheduler, batch_size, device, steps_per_epoch):
    model.train()
    total_policy_loss = 0
    total_value_loss = 0
    num_batches = 0

    policy_criterion = nn.CrossEntropyLoss()
    value_criterion = nn.BCELoss()

    pbar = tqdm(range(steps_per_epoch), desc="Training", unit="batch")
    for _ in pbar:
        spatial_batch, scalar_batch, policy_batch, value_batch = data_generator.generate_batch(
            batch_size)

        spatial_batch = spatial_batch.to(device)
        scalar_batch = scalar_batch.to(device)
        policy_batch = policy_batch.to(device)
        value_batch = value_batch.to(device).unsqueeze(1)

        optimizer.zero_grad()

        policy_out, value_out = model(spatial_batch, scalar_batch)

        policy_loss = policy_criterion(policy_out, policy_batch)
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


def evaluate(model, data_generator, batch_size, device, eval_steps):
    model.eval()
    total_policy_loss = 0
    total_value_loss = 0
    num_batches = 0

    policy_criterion = nn.CrossEntropyLoss()
    value_criterion = nn.BCELoss()

    with torch.no_grad():
        pbar = tqdm(range(eval_steps), desc="Evaluating", unit="batch")
        for _ in pbar:
            spatial_batch, scalar_batch, policy_batch, value_batch = data_generator.generate_batch(
                batch_size)

            spatial_batch = spatial_batch.to(device)
            scalar_batch = scalar_batch.to(device)
            policy_batch = policy_batch.to(device)
            value_batch = value_batch.to(device).unsqueeze(1)

            policy_out, value_out = model(spatial_batch, scalar_batch)

            policy_loss = policy_criterion(policy_out, policy_batch)
            value_loss = value_criterion(value_out, value_batch)

            total_policy_loss += policy_loss.item()
            total_value_loss += value_loss.item()
            num_batches += 1

            pbar.set_postfix({
                'policy_loss': f'{policy_loss.item():.4f}',
                'value_loss': f'{value_loss.item():.4f}'
            })
            pbar.update()

        pbar.close()

    return total_policy_loss / num_batches, total_value_loss / num_batches


def save_model(model, optimizer, scheduler, epoch, loss, save_dir):
    if not os.path.exists(save_dir):
        os.makedirs(save_dir)

    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    filename = f'model_epoch{epoch}_{timestamp}.pt'
    filepath = os.path.join(save_dir, filename)

    torch.save({
        'epoch': epoch,
        'model_state_dict': model.state_dict(),
        'optimizer_state_dict': optimizer.state_dict(),
        'scheduler_state_dict': scheduler.state_dict(),
        'loss': loss,
    }, filepath)

    return filepath


def load_model(model, optimizer, scheduler, filepath):
    checkpoint = torch.load(filepath)
    model.load_state_dict(checkpoint['model_state_dict'])
    optimizer.load_state_dict(checkpoint['optimizer_state_dict'])
    scheduler.load_state_dict(checkpoint['scheduler_state_dict'])
    epoch = checkpoint['epoch']
    loss = checkpoint['loss']

    return model, optimizer, scheduler, epoch, loss


def evaluate_single_move(model, data_generator, device):
    """Evaluate a single move from a validation game and visualize the results."""
    model.eval()

    with torch.no_grad():
        # Generate a single example
        spatial_batch, scalar_batch, policy_batch, value_batch = data_generator.generate_batch(
            1)

        # Get the board state and next move from the generator
        board = data_generator.current_board
        next_move = data_generator.current_move

        print("\nCurrent board state before move:")
        board.print()

        # Move tensors to device
        spatial_batch = spatial_batch.to(device)
        scalar_batch = scalar_batch.to(device)

        # Get model predictions
        policy_out, value_out = model(spatial_batch, scalar_batch)

        # Print policy distribution
        print("\nPolicy distribution:")
        policy_probs = F.softmax(policy_out, dim=1)[0]
        top_k = 5
        values, indices = torch.topk(policy_probs, top_k)

        for i in range(top_k):
            idx = indices[i].item()
            prob = values[i].item()
            if idx == policy_out.size(1) - 1:
                print(f"Pass move: {prob:.3f}")
            else:
                mem_y = idx // go_data_gen.Board.data_size
                mem_x = idx % go_data_gen.Board.data_size
                x = mem_x - go_data_gen.Board.padding
                y = mem_y - go_data_gen.Board.padding
                print(f"Move ({x}, {y}): {prob:.3f}")

        # Get the predicted move
        max_idx = torch.argmax(policy_out).item()
        print(f"\nSelected move index: {max_idx}")

        if max_idx == policy_out.size(1) - 1:
            print("Predicted move: PASS")
            pred_move = go_data_gen.Move(next_move.color, True, go_data_gen.Vec2(0, 0))
        else:
            mem_y = max_idx // go_data_gen.Board.data_size
            mem_x = max_idx % go_data_gen.Board.data_size
            x = mem_x - go_data_gen.Board.padding
            y = mem_y - go_data_gen.Board.padding
            print(f"Predicted coordinates (memory): ({mem_x}, {mem_y})")
            print(f"Predicted coordinates (board): ({x}, {y})")
            pred_move = go_data_gen.Move(
                next_move.color, False, go_data_gen.Vec2(x, y))

        # Check if move is legal
        print(f"\nChecking if move is legal...")
        is_legal = board.get_move_legality(pred_move) == go_data_gen.MoveLegality.Legal
        print(f"Move is {'legal' if is_legal else 'illegal'}")

        if is_legal:
            # Play the predicted move
            board.play(pred_move)
            print("\nBoard after playing move:")
            board.print(highlight_fn=lambda mem_x, mem_y: not next_move.is_pass and
                        mem_x - go_data_gen.Board.padding == next_move.coord.x and
                        mem_y - go_data_gen.Board.padding == next_move.coord.y)
        else:
            print("WARNING: Illegal move predicted! Skipping board update.")

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
    num_epochs = 1000
    batch_size = 32
    learning_rate = 0.01
    steps_per_epoch = 100
    eval_steps = 100
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    save_dir = './checkpoints'

    # Initialize model
    model = GoNet(
        num_input_planes=go_data_gen.Board.num_feature_planes,
        num_input_features=go_data_gen.Board.num_feature_scalars,
        channels=64,
        num_blocks=4,
        c_head=32
    ).to(device)

    optimizer = optim.Adam(
        model.parameters(), lr=learning_rate, weight_decay=1e-4)

    # Learning rate scheduler using cosine annealing
    # Gradually reduces learning rate following a cosine curve
    scheduler = optim.lr_scheduler.CosineAnnealingLR(
        optimizer, T_max=num_epochs, eta_min=1e-6)

    # Initialize data generators
    train_data_dir = "./data/train"
    val_data_dir = "./data/val"
    train_generator = GoDataGenerator(train_data_dir)
    val_generator = GoDataGenerator(val_data_dir)

    # Training loop
    for epoch in range(num_epochs):
        print(f'\nEpoch {epoch+1}/{num_epochs}:')
        train_policy_loss, train_value_loss = train_epoch(
            model, train_generator, optimizer, scheduler, batch_size, device, steps_per_epoch)

        val_policy_loss, val_value_loss = evaluate(
            model, val_generator, batch_size, device, eval_steps)

        print(
            f'Train - Policy Loss: {train_policy_loss:.4f}, Value Loss: {train_value_loss:.4f}')
        print(
            f'Val - Policy Loss: {val_policy_loss:.4f}, Value Loss: {val_value_loss:.4f}')
        print(f'Current learning rate: {scheduler.get_last_lr()[0]}')

        # Save model checkpoint
        if (epoch + 1) % 1 == 0:
            total_loss = train_policy_loss + train_value_loss
            save_model(model, optimizer, scheduler,
                       epoch + 1, total_loss, save_dir)

            print("\nEvaluating single move prediction:")
            evaluate_single_move(model, val_generator, device)


if __name__ == "__main__":
    main()
