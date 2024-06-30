import torch

import go_data_gen

from model import GoNet
from io_conversions import *


def str_to_color(color_str):
    """Convert GTP color string to go_data_gen.Color enum."""
    if color_str.lower() in ['b', 'black']:
        return go_data_gen.Color.Black
    elif color_str.lower() in ['w', 'white']:
        return go_data_gen.Color.White
    else:
        raise ValueError(f"Invalid color string: {color_str}")


def color_to_str(color):
    """Convert go_data_gen.Color enum to GTP color string."""
    if color == go_data_gen.Color.Black:
        return 'B'
    elif color == go_data_gen.Color.White:
        return 'W'
    else:
        raise ValueError(f"Invalid Color enum: {color}")


def str_to_coord(vertex):
    """Convert GTP vertex (e.g., 'D4') to (row, col) tuple."""
    if vertex.lower() == 'pass':
        return go_data_gen.pass_coord
    col = ord(vertex[0].upper()) - ord('A')
    if col > 7:  # Skip 'I'
        col -= 1
    row = int(vertex[1:]) - 1
    return (18 - row, col)  # Flip row to match the desired coordinate system


def coord_to_str(coord):
    """Convert (row, col) tuple to GTP vertex."""
    if coord == go_data_gen.pass_coord:
        return 'pass'
    row, col = coord
    if col > 7:
        col += 1  # Skip 'I'
    return f"{chr(col + ord('A'))}{19 - row}"


def fixed_handicap(num_stones):
    handicap_positions = {
        2: [(3, 15), (15, 3)],
        3: [(3, 15), (15, 3), (15, 15)],
        4: [(3, 3), (3, 15), (15, 3), (15, 15)],
        5: [(3, 3), (3, 15), (9, 9), (15, 3), (15, 15)],
        6: [(3, 3), (3, 15), (9, 3), (9, 15), (15, 3), (15, 15)],
        7: [(3, 3), (3, 15), (9, 3), (9, 9), (9, 15), (15, 3), (15, 15)],
        8: [(3, 3), (3, 9), (3, 15), (9, 3), (9, 15), (15, 3), (15, 9), (15, 15)],
        9: [(3, 3), (3, 9), (3, 15), (9, 3), (9, 9), (9, 15), (15, 3), (15, 9), (15, 15)]
    }

    if num_stones in handicap_positions:
        return handicap_positions[num_stones]
    else:
        return None


class GoGTPEngine:
    def __init__(self, model, board, device):
        self.model = model
        self.board = board
        self.device = device
        self.size = (19, 19)
        self.komi = 7.5
        self.commands = [
            'list_commands', 'boardsize', 'clear_board', 'komi', 'play', 'genmove',
            'fixed_handicap', 'place_free_handicap', 'set_free_handicap', 'quit'
        ]

    def run(self):
        while True:
            input_line = input().strip()

            if input_line == "quit":
                break

            command = input_line.split()

            if command[0] == "list_commands":
                print("= " + "\n".join(self.commands))
            elif command[0] == "boardsize":
                size = int(command[1])
                self.size = (size, size)
                self.board = go_data_gen.Board(self.size, self.komi)
                print("=")
            elif command[0] == "clear_board":
                self.board.reset()
                print("=")
            elif command[0] == "komi":
                self.komi = float(command[1])
                self.board.komi = self.komi
                print("=")
            elif command[0] == "play":
                color = str_to_color(command[1].lower())
                coord = str_to_coord(command[2])
                self.board.play(go_data_gen.Move(color, coord))
                print("=")
            elif command[0] == "genmove":
                color = str_to_color(command[1].lower())
                coord = self.model.gen_move(board, color)
                self.board.play(go_data_gen.Move(color, coord))
                print("= " + coord_to_str(coord))
            elif command[0] == "fixed_handicap":
                num_stones = int(command[1])
                handicap_coords = fixed_handicap(num_stones)
                if handicap_coords:
                    for coord in handicap_coords:
                        self.board.setup_move(go_data_gen.Move(
                            go_data_gen.Color.Black, coord))
                    print("= " + " ".join(coord_to_str(coord)
                                          for coord in handicap_coords))
                else:
                    print("? invalid number of handicap stones")
            elif command[0] == "place_free_handicap":
                num_stones = int(command[1])
                handicap_coords = []
                for i in range(num_stones):
                    coord = self.model.gen_move(
                        self.board, go_data_gen.Color.Black)
                    self.board.setup_move(go_data_gen.Move(
                        go_data_gen.Color.Black, coord))
                    handicap_coords.append(coord)
                print("= " + " ".join(coord_to_str(coord)
                                      for coord in handicap_coords))
            elif command[0] == "set_free_handicap":
                for coord in command[1:]:
                    self.board.setup_move(go_data_gen.Move(
                        go_data_gen.Color.Black, str_to_coord(coord)))
                print("=")
            else:
                print("? unknown_command")
                print("# {}".format(input_line))


if __name__ == "__main__":
    device = "cuda" if torch.cuda.is_available() else "cpu"
    model = GoNet(device=device, input_channels=go_data_gen.Board.num_feature_planes +
                  go_data_gen.Board.num_feature_scalars, width=32, depth=8).to(device)
    board = go_data_gen.Board()
    engine = GoGTPEngine(model, board, device)
    engine.run()
