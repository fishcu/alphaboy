import argparse
import torch
import random

import go_data_gen
from model import GoNet, predict_move


def play_game(model, device, temperature=0.01, max_moves=200):
    # Initialize board
    board = go_data_gen.Board(go_data_gen.Vec2(19, 19), 7.5)
    current_color = go_data_gen.Color.Black
    consecutive_passes = 0
    move_count = 0

    print("\nInitial board state:")
    board.print()

    while move_count < max_moves and consecutive_passes < 2:
        # Use the predict_move function to get the next move and value
        pred_move, value = predict_move(
            model, board, current_color, device, temperature)

        # Check if the move is a pass
        if pred_move.is_pass:
            print(f"\n{current_color.name} plays: PASS")
            consecutive_passes += 1
        else:
            x, y = pred_move.coord.x, pred_move.coord.y
            print(f"\n{current_color.name} plays: ({x}, {y})")
            consecutive_passes = 0

        # Play the move
        board.play(pred_move)
        print(f"Move {move_count + 1}:")
        board.print()

        # Print current position evaluation using the value returned from predict_move
        print(f"Position evaluation: {torch.sigmoid(value).item():.3f}")

        move_count += 1
        current_color = go_data_gen.opposite(current_color)

    print("\nGame ended after", move_count, "moves")
    if consecutive_passes == 2:
        print("Reason: Two consecutive passes")
    else:
        print("Reason: Maximum moves reached")


def main():
    parser = argparse.ArgumentParser(
        description="Self-play with a trained model")
    parser.add_argument("checkpoint_path", type=str,
                        help="Path to the checkpoint file")
    parser.add_argument("--max-moves", type=int, default=400,
                        help="Maximum number of moves")
    parser.add_argument("--temperature", type=float, default=0.01,
                        help="Temperature for move sampling (0=deterministic, 1=raw probabilities)")
    parser.add_argument("--seed", type=int,
                        help="Random seed for reproducibility")
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
