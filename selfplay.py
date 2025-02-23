import argparse
import torch
import torch.nn.functional as F
import random

import go_data_gen
from model import GoNet


def sample_move(policy_probs, temperature=1.0):
    """
    Sample a move from the policy distribution using temperature scaling.
    Higher temperature = more randomness, lower = more deterministic.
    At temperature=0, this is equivalent to taking argmax.
    """
    if temperature == 0:
        return torch.argmax(policy_probs).item()
    
    # Apply temperature scaling
    logits = torch.log(policy_probs)
    scaled_probs = F.softmax(logits / temperature, dim=0)
    
    # Sample from the distribution
    return torch.multinomial(scaled_probs, 1).item()


def play_game(model, device, temperature=0.5, max_moves=200):
    # Initialize board
    board = go_data_gen.Board(go_data_gen.Vec2(19, 19), 7.5)
    current_color = go_data_gen.Color.Black
    consecutive_passes = 0
    move_count = 0

    print("\nInitial board state:")
    board.print()

    while move_count < max_moves and consecutive_passes < 2:
        # Get input features for current position
        spatial_features = torch.from_numpy(board.get_feature_planes(current_color))
        scalar_features = torch.from_numpy(board.get_feature_scalars(current_color))

        # Add batch dimension and move to device
        spatial_features = spatial_features.unsqueeze(0).to(device)
        scalar_features = scalar_features.unsqueeze(0).to(device)

        # Get model predictions
        with torch.no_grad():
            combined_policy, value = model(spatial_features, scalar_features)
            policy_probs = F.softmax(combined_policy, dim=1)[0]

        # Sample move from policy distribution
        max_idx = sample_move(policy_probs, temperature)
        pass_idx = board.data_size * board.data_size

        # Create the move
        if max_idx == pass_idx:  # Pass move index
            pred_move = go_data_gen.Move(current_color, True, go_data_gen.Vec2(0, 0))
            print(f"\n{current_color.name} plays: PASS")
            consecutive_passes += 1
        else:
            mem_y = max_idx // board.data_size
            mem_x = max_idx % board.data_size
            x = mem_x - board.padding
            y = mem_y - board.padding
            pred_move = go_data_gen.Move(current_color, False, go_data_gen.Vec2(x, y))
            print(f"\n{current_color.name} plays: ({x}, {y})")
            consecutive_passes = 0

        # Play the move
        board.play(pred_move)
        print(f"Move {move_count + 1}:")
        board.print()

        # Print current position evaluation
        print(f"Position evaluation: {value[0].item():.3f}")

        move_count += 1
        current_color = go_data_gen.opposite(current_color)

    print("\nGame ended after", move_count, "moves")
    if consecutive_passes == 2:
        print("Reason: Two consecutive passes")
    else:
        print("Reason: Maximum moves reached")


def main():
    parser = argparse.ArgumentParser(description="Self-play with a trained model")
    parser.add_argument("checkpoint_path", type=str, help="Path to the checkpoint file")
    parser.add_argument("--max-moves", type=int, default=200, help="Maximum number of moves")
    parser.add_argument("--temperature", type=float, default=0.1, 
                      help="Temperature for move sampling (0=deterministic, 1=raw probabilities)")
    parser.add_argument("--seed", type=int, help="Random seed for reproducibility")
    args = parser.parse_args()

    if args.seed is not None:
        torch.manual_seed(args.seed)
        random.seed(args.seed)

    device = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"Using device: {device}")
    print(f"Temperature: {args.temperature}")

    # Load the model using the class method
    model = GoNet.load_from_checkpoint(args.checkpoint_path, device)[0]
    model.eval()

    play_game(model, device, args.temperature, args.max_moves)


if __name__ == "__main__":
    main()
