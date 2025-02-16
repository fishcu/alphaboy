import os
import random
import torch
from tqdm import tqdm

import go_data_gen


class GoDataGenerator:
    def __init__(self, data_dir, debug=False):
        self.data_dir = data_dir
        self.sgf_files = self.load_sgf_files()
        self.debug = debug

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
        input_data = []
        policy_data = []
        value_data = []

        with tqdm(total=batch_size, desc="Generating minibatch") as pbar:
            while len(input_data) < batch_size:
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

                    # Combine into input tensor
                    input = (spatial_features, scalar_features)

                    # Create policy target - one-hot encoding of next move
                    next_move = moves[next_play_idx]
                    if next_move.is_pass:
                        policy = torch.zeros(
                            (board.data_size * board.data_size + 1))
                        policy[-1] = 1.0  # Last index represents pass
                    else:
                        policy = torch.zeros(
                            (board.data_size * board.data_size + 1))
                        move_idx = next_move.coord[1] * \
                            board.data_size + next_move.coord[0]
                        policy[move_idx] = 1.0

                    # Create value target
                    value = torch.tensor([result if to_play == go_data_gen.Color.Black
                                          else -result], dtype=torch.float32)

                    if self.debug:
                        print("\nFeature planes shape (NHWC):",
                              spatial_features.shape)
                        print("\nLegal moves plane:")
                        # First channel is legal moves
                        print(spatial_features[:, :, 0].numpy())

                        print("\nPolicy as board position:")
                        policy_grid = policy[:-
                                             1].reshape(board.data_size, board.data_size)
                        print(policy_grid.numpy())
                        print(f"Pass probability: {policy[-1]:.1f}")

                        print(f"\nValue target: {value.item():.1f}")

                    input_data.append(input)
                    policy_data.append(policy)
                    value_data.append(value)

                except Exception as e:
                    print(f"Error loading SGF file: {sgf_file}")
                    print(f"Error type: {type(e).__name__}")
                    print(f"Error message: {str(e)}")
                    print("Please inspect the file manually.")
                    continue

                pbar.update(1)

        # Stack the batches, maintaining NHWC format for spatial features
        spatial_batch = torch.stack([x[0] for x in input_data])
        scalar_batch = torch.stack([x[1] for x in input_data])
        policy_batch = torch.stack(policy_data)
        value_batch = torch.cat(value_data)

        return ((spatial_batch, scalar_batch), policy_batch, value_batch)


def main():
    random.seed(42)
    torch.set_printoptions(linewidth=120)
    data_dir = "./data/2025-02-13sgfs"
    generator = GoDataGenerator(data_dir, debug=True)

    batch_size = 2**3
    (spatial_batch, scalar_batch), policy_batch, value_batch = generator.generate_batch(batch_size)
    print("Spatial input batch shape:", spatial_batch.shape)
    print("Scalar input batch shape:", scalar_batch.shape)
    print("Policy batch shape:", policy_batch.shape)
    print("Value batch shape:", value_batch.shape)


if __name__ == "__main__":
    main()
