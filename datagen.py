import os
import random
import torch
from tqdm import tqdm

import go_data_gen

from io_conversions import *


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

    def generate_batch(self, batch_size: int):
        input_data = []
        policy_data = []
        value_data = []

        with tqdm(total=batch_size, desc="Generating batch") as pbar:
            while len(input_data) < batch_size:
                sgf_file = self.sgf_files[random.randint(
                    0, len(self.sgf_files) - 1)]
                # print(f"Loading SGF from: {os.path.abspath(sgf_file)}")

                try:
                    board, moves, result = go_data_gen.load_sgf(sgf_file)

                    play_idx = random.randint(0, len(moves) - 2)
                    next_play_idx = play_idx + 1

                    for move in moves[:next_play_idx]:
                        board.play(move)

                    if self.debug:
                        print(f"Showing board with {
                              next_play_idx} moves played:")
                        board.print()

                    input = encode_input(
                        board, go_data_gen.opposite(moves[play_idx].color))
                    policy, value = encode_output(moves[next_play_idx], result)

                    if self.debug:
                        print(f"input plane 2: \n{input[2]}\n")
                        print(f"policy: \n{policy}\n")
                        print(f"value: {value}")

                    input_data.append(input)
                    policy_data.append(policy)
                    value_data.append(value)

                except Exception as e:
                    print(f"Error loading SGF file: {sgf_file}")
                    print(f"Error type: {type(e).__name__}")
                    print(f"Error message: {str(e)}")
                    print("Please inspect the file manually.")

                pbar.update(1)

        return (torch.stack(input_data), torch.stack(policy_data), torch.cat(value_data))


def main():
    torch.set_printoptions(linewidth=120)
    data_dir = "./data/"
    generator = GoDataGenerator(data_dir, debug=True)

    batch_size = 2**3
    input_batch, policy_batch, value_batch = generator.generate_batch(
        batch_size)
    print("Input batch shape:", input_batch.shape)
    print("Policy batch shape:", policy_batch.shape)
    print("Value batch shape:", value_batch.shape)


if __name__ == "__main__":
    main()
