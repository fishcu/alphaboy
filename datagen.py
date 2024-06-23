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
        for root, _, files in os.walk(self.data_dir):
            for file in files:
                if file.endswith(".sgf") or file.endswith(".SGF"):
                    sgf_files.append(os.path.join(root, file))
        return sgf_files

    def generate_batch(self, batch_size):
        input_data = []
        policy_data = []
        value_data = []

        with tqdm(total=batch_size, desc="Generating batch") as pbar:
            while len(input_data) < batch_size:
                sgf_file = self.sgf_files[random.randint(
                    0, len(self.sgf_files) - 1)]

                board, moves, result = go_data_gen.load_sgf(sgf_file)

                play_idx = random.randint(0, len(moves) - 1)
                next_play_idx = play_idx + 1

                for move in moves[:next_play_idx]:
                    board.play(move)

                if self.debug:
                    print(f"SGF loaded from: {sgf_file}")
                    print(f"Showing board with {next_play_idx} moves played:")
                    board.print()

                input = self.encode_input(board, moves[play_idx])
                policy, value = self.encode_output(
                    moves[next_play_idx], result)

                input_data.append(input)
                policy_data.append(policy)
                value_data.append(value)

                pbar.update(1)

        return (torch.stack(input_data), torch.stack(policy_data), torch.cat(value_data))

    def encode_input(self, board, move):
        # Get 2D feature planes and scalar features as numpy arrays
        stacked_maps, scalar_features = board.get_nn_input_data(
            go_data_gen.opposite(move.color))
        assert stacked_maps.shape == (
            go_data_gen.Board.num_feature_planes, go_data_gen.Board.data_size, go_data_gen.Board.data_size)
        assert scalar_features.shape == (
            go_data_gen.Board.num_feature_scalars,)

        # Convert numpy arrays to PyTorch tensors
        stacked_maps_tensor = torch.from_numpy(stacked_maps)
        scalar_features_tensor = torch.from_numpy(scalar_features)

        # Repeat scalar features across spatial dimensions
        scalar_features_expanded = scalar_features_tensor.unsqueeze(1).unsqueeze(2).expand(
            -1, go_data_gen.Board.data_size, go_data_gen.Board.data_size)

        # Stack the expanded scalar features with the stacked maps
        x = torch.cat([stacked_maps_tensor, scalar_features_expanded], dim=0)

        assert x.shape == (go_data_gen.Board.num_feature_planes + go_data_gen.Board.num_feature_scalars,
                           go_data_gen.Board.data_size, go_data_gen.Board.data_size)

        return x

    def encode_output(self, next_move, result):
        # Encode policy (next move)
        policy = torch.zeros(go_data_gen.Board.data_size,
                             go_data_gen.Board.data_size)
        # Pass is encoded just outside the board area, within the padded area.
        # Since the pass coordinate is (-1, -1), summing with the padding will work.
        policy[next_move.coord[0] + go_data_gen.Board.padding,
               next_move.coord[1] + go_data_gen.Board.padding] = 1.0

        # Encode value (game result)
        value = torch.sigmoid(torch.tensor([result]))

        assert policy.shape == (
            go_data_gen.Board.data_size, go_data_gen.Board.data_size)
        assert value.shape == (1,)

        return policy, value


# Usage example
data_dir = "./data/"
generator = GoDataGenerator(data_dir, debug=True)

batch_size = 32
input_batch, policy_batch, value_batch = generator.generate_batch(batch_size)
print("Input batch shape:", input_batch.shape)
print("Policy batch shape:", policy_batch.shape)
print("Value batch shape:", value_batch.shape)
