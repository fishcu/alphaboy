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
                    print(f"\nShowing board with {next_play_idx} moves played:")
                    board.print()

                # Get the next player's color
                to_play = moves[next_play_idx].color

                # Get input features (keeping NHWC format)
                spatial_features = torch.from_numpy(
                    board.get_feature_planes(to_play))
                scalar_features = torch.from_numpy(
                    board.get_feature_scalars(to_play))

                # Create policy target - one-hot encoding of next move
                policy = torch.zeros((board.data_size * board.data_size + 1))
                next_move = moves[next_play_idx]
                if next_move.is_pass:
                    policy[-1] = 1.0  # Last index represents pass
                else:
                    # Add padding to convert board coordinates to memory coordinates
                    mem_x = next_move.coord.x + board.padding
                    mem_y = next_move.coord.y + board.padding
                    move_idx = mem_y * board.data_size + mem_x
                    policy[move_idx] = 1.0

                # Create value target
                value = torch.tensor([result], dtype=torch.float32)
                # Adjust for player perspective
                if to_play == go_data_gen.Color.White:
                    value = -value
                # Apply steep sigmoid with scaling factor
                scale = 10.0
                value = torch.sigmoid(scale * value)

                if self.debug:
                    print("\nPolicy as board position:")
                    policy_grid = policy[:-1].reshape(board.data_size, board.data_size)
                    print(policy_grid.numpy())
                    print(f"Pass probability: {policy[-1]:.1f}")
                    print(f"\nValue target: {value.item():.1f}")

                spatial_data.append(spatial_features)
                scalar_data.append(scalar_features)
                policy_data.append(policy)
                value_data.append(value)

            except Exception as e:
                # print(f"Error loading SGF file: {sgf_file}")
                # print(f"Error type: {type(e).__name__}")
                # print(f"Error message: {str(e)}")
                # print("Please inspect the file manually.")
                continue

        # Stack the batches
        spatial_batch = torch.stack(spatial_data)  # [N, H, W, C]
        scalar_batch = torch.stack(scalar_data)    # [N, F]
        policy_batch = torch.stack(policy_data)    # [N, H*W + 1]
        value_batch = torch.cat(value_data)        # [N, 1]

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
        print(f"Pass probability in policy: {policy[-1]:.1f}")
        assert policy[-1] == 1.0, f"Pass move should have probability 1.0, got {policy[-1]:.1f}"
    else:
        print(
            f"Target move (board coordinates): ({next_move.coord.x}, {next_move.coord.y})")
        mem_x = next_move.coord.x + board.padding
        mem_y = next_move.coord.y + board.padding
        print(f"Target move (memory coordinates): ({mem_x}, {mem_y})")
        move_idx = mem_y * board.data_size + mem_x
        print(f"Target move index in policy vector: {move_idx}")
        policy_value = policy[move_idx].item()
        print(f"Policy value at target index: {policy_value:.1f}")
        assert policy_value == 1.0, f"Target move should have probability 1.0, got {policy_value:.1f}"
        assert torch.sum(
            policy) == 1.0, f"Policy should sum to 1.0, got {torch.sum(policy):.1f}"

    # Print policy as 2D grid (excluding pass move)
    print("\nPolicy as 2D grid (memory coordinates):")
    policy_grid = policy[:-1].reshape(board.data_size, board.data_size)
    print(policy_grid.numpy())

    print("\nLegal moves plane (memory coordinates):")
    # Also show last example's legal moves
    print(spatial_batch[-1, :, :, 0].numpy())

    print("\nBatch shapes:")
    print("Spatial input batch shape:", spatial_batch.shape)
    print("Scalar input batch shape:", scalar_batch.shape)
    print("Policy batch shape:", policy_batch.shape)
    print("Value batch shape:", value_batch.shape)


if __name__ == "__main__":
    main()
