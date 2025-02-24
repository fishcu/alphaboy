import os
import random
import torch

import go_data_gen


class GoDataGenerator:
    def __init__(self, data_dir, debug=False):
        self.data_dir = data_dir
        self.sgf_files = self.load_sgf_files()
        self.debug = debug

        self.current_board = None
        self.current_move = None

    def load_sgf_files(self):
        sgf_files = []
        print("Indexing SGF files...")
        for root, _, files in os.walk(self.data_dir):
            for file in files:
                if file.endswith(".sgf") or file.endswith(".SGF"):
                    sgf_files.append(os.path.join(root, file))
        print(f"Found {len(sgf_files)} SGF files!")
        return sgf_files

    def generate_batch(self, batch_size: int):
        spatial_data = []
        scalar_data = []
        policy_data = []
        value_data = []

        while len(spatial_data) < batch_size:
            sgf_file = self.sgf_files[random.randint(
                0, len(self.sgf_files) - 1)]

            try:
                board, moves, result = go_data_gen.load_sgf(sgf_file)
                play_idx = random.randint(0, len(moves) - 2)
                next_play_idx = play_idx + 1

                # Play moves up to the current position
                for move in moves[:next_play_idx]:
                    board.play(move)

                if self.debug:
                    print(
                        f"\nShowing board with {next_play_idx} moves played:")
                    board.print()

                # Get the next player's color
                to_play = moves[next_play_idx].color

                # Get input features (keeping NHWC format)
                spatial_features = torch.from_numpy(
                    board.get_feature_planes(to_play))
                scalar_features = torch.from_numpy(
                    board.get_feature_scalars(to_play))

                # Create policy target - index of next move
                # Scalar tensor instead of size [1]
                policy = torch.tensor(0, dtype=torch.long)
                next_move = moves[next_play_idx]
                if next_move.is_pass:
                    # Last index represents pass
                    policy = torch.tensor(board.data_size * board.data_size)
                else:
                    # Add padding to convert board coordinates to memory coordinates
                    mem_x = next_move.coord.x + board.padding
                    mem_y = next_move.coord.y + board.padding
                    move_idx = mem_y * board.data_size + mem_x
                    policy = torch.tensor(move_idx)

                # Create value target (+1 for win, 0 for loss, 0.5 for draw)
                value = torch.tensor(0.5, dtype=torch.float32)
                if result > 0:  # Win for Black
                    value = torch.tensor(
                        1.0 if to_play == go_data_gen.Color.Black else 0.0, dtype=torch.float32)
                elif result < 0:  # Win for White
                    value = torch.tensor(
                        1.0 if to_play == go_data_gen.Color.White else 0.0, dtype=torch.float32)

                if self.debug:
                    print("\nPolicy as board position:")
                    policy_grid = torch.zeros(
                        board.data_size * board.data_size + 1)
                    # Set the chosen move to 1.0
                    policy_grid[policy.item()] = 1.0
                    policy_grid = policy_grid[:-
                                              1].reshape(board.data_size, board.data_size)
                    print(policy_grid.numpy())
                    print(
                        f"Pass move selected: {policy.item() == board.data_size * board.data_size}")
                    print(f"\nValue target: {value.item():.1f}")

                spatial_data.append(spatial_features)
                scalar_data.append(scalar_features)
                policy_data.append(policy)
                value_data.append(value)

            except Exception as e:
                print(f"Error loading SGF file: {os.path.abspath(sgf_file)}")
                print(f"Error type: {type(e).__name__}")
                print(f"Error message: {str(e)}")
                print("Please inspect the file manually.")
                continue

        # Stack the batches
        spatial_batch = torch.stack(spatial_data)  # [N, H, W, C]
        scalar_batch = torch.stack(scalar_data)    # [N, F]
        policy_batch = torch.stack(policy_data)    # [N]
        value_batch = torch.stack(value_data)    # [N]

        self.current_board = board
        self.current_move = moves[next_play_idx]

        return spatial_batch, scalar_batch, policy_batch, value_batch


def main():
    random.seed(42)
    torch.set_printoptions(linewidth=120)
    data_dir = "./data/val"
    generator = GoDataGenerator(data_dir, debug=True)

    batch_size = 2**3
    spatial_batch, scalar_batch, policy_batch, value_batch = generator.generate_batch(
        batch_size)

    # Debug policy encoding for last example in batch (matches current_move)
    print("\nDebugging policy encoding for last example:")
    policy = policy_batch[-1]  # Get last example's policy
    board = generator.current_board
    next_move = generator.current_move

    if next_move.is_pass:
        print("Target move is PASS")
        expected_idx = board.data_size * board.data_size
        assert policy.item(
        ) == expected_idx, f"Pass move should have index {expected_idx}, got {policy.item()}"
    else:
        print(
            f"Target move (board coordinates): ({next_move.coord.x}, {next_move.coord.y})")
        mem_x = next_move.coord.x + board.padding
        mem_y = next_move.coord.y + board.padding
        print(f"Target move (memory coordinates): ({mem_x}, {mem_y})")
        expected_idx = mem_y * board.data_size + mem_x
        print(f"Target move index: {expected_idx}")
        assert policy.item(
        ) == expected_idx, f"Target move should have index {expected_idx}, got {policy.item()}"

    print("\nLegal moves plane (memory coordinates):")
    print(spatial_batch[-1, :, :, 0].numpy())

    print("\nBatch shapes:")
    print("Spatial input batch shape:", spatial_batch.shape)
    print("Scalar input batch shape:", scalar_batch.shape)
    print("Policy batch shape:", policy_batch.shape)
    print("Value batch shape:", value_batch.shape)


if __name__ == "__main__":
    main()
