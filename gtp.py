import argparse
import signal
import sys

import torch

import go_data_gen

from model import GoNet, predict_move


def str_to_color(color_str: str):
    """Convert GTP color string to go_data_gen.Color enum."""
    if color_str.lower() in ['b', 'black']:
        return go_data_gen.Color.Black
    elif color_str.lower() in ['w', 'white']:
        return go_data_gen.Color.White
    else:
        raise ValueError(f"Invalid color string: {color_str}")


def color_to_str(color: go_data_gen.Color):
    """Convert go_data_gen.Color enum to GTP color string."""
    if color == go_data_gen.Color.Black:
        return 'B'
    elif color == go_data_gen.Color.White:
        return 'W'
    else:
        raise ValueError(f"Invalid Color enum: {color}")


def str_to_coord(vertex: str, board_size_y=19):
    """Convert GTP vertex (e.g., 'D4') to (row, col) tuple."""
    col = ord(vertex[0].upper()) - ord('A')
    if col > 7:  # Skip 'I'
        col -= 1
    row = int(vertex[1:])

    # A19 is top-left (0,0), row increases as we go down
    # Convert row from GTP format to internal format
    # where row board.size.y maps to y=0 and row 1 maps to y=board.size.y-1
    y = board_size_y - row

    return go_data_gen.Vec2(col, y)


def coord_to_str(coord: go_data_gen.Vec2, board_size_y=19):
    """Convert (row, col) tuple to GTP vertex."""
    x, y = coord.x, coord.y

    # Handle 'I' skip in column labels
    col_letter = x
    if x >= 8:
        col_letter += 1  # Skip 'I'

    # Convert from internal format to GTP format
    # where (0,0) is A<board.size.y> and (0,board.size.y-1) is A1
    row = board_size_y - y

    return f"{chr(col_letter + ord('A'))}{row}"


def str_to_move(move_str: str, board_size_y=19):
    """Convert GTP move string to go_data_gen.Move."""
    if move_str.lower() == 'pass':
        return go_data_gen.Move(go_data_gen.Color.Black, True, go_data_gen.Vec2(0, 0))
    else:
        return go_data_gen.Move(go_data_gen.Color.Black, False, str_to_coord(move_str, board_size_y))


def move_to_str(move: go_data_gen.Move, board_size_y=19):
    """Convert go_data_gen.Move to GTP move string."""
    if move.is_pass:
        return 'pass'
    else:
        return coord_to_str(move.coord, board_size_y)


def fixed_handicap(num_stones: int):
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
    def __init__(self, model, board, temperature=0.01):
        self.model = model
        self.board = board
        self.temperature = temperature
        self.commands = [
            'list_commands', 'boardsize', 'clear_board', 'komi', 'play', 'genmove',
            'fixed_handicap', 'place_free_handicap', 'set_free_handicap', 'quit', 'showboard'
        ]
        self.running = True

    def run(self):
        while self.running:
            try:
                input_line = input().strip()

                if input_line == "quit":
                    break

                command = input_line.split()

                if command[0] == "list_commands":
                    print("= " + "\n".join(self.commands))
                elif command[0] == "boardsize":
                    size = int(command[1])
                    self.board = go_data_gen.Board(
                        go_data_gen.Vec2(size, size), self.board.komi)
                    print("=")
                elif command[0] == "clear_board":
                    self.board.reset()
                    print("=")
                elif command[0] == "komi":
                    self.board.komi = float(command[1])
                    print("=")
                elif command[0] == "play":
                    color = str_to_color(command[1].lower())
                    coord = str_to_coord(
                        command[2], self.board.get_board_size().y)
                    if not self.board.is_legal(go_data_gen.Move(color, coord)):
                        print("? illegal move")
                    else:
                        self.board.play(go_data_gen.Move(color, coord))
                        print("=")
                elif command[0] == "genmove":
                    color = str_to_color(command[1].lower())
                    # Use predict_move to generate a move (ignore the value)
                    move, _ = predict_move(self.model, self.board, color, device="cuda",
                                           temperature=self.temperature)
                    self.board.play(move)
                    self.board.print()
                    print("= " + move_to_str(move, self.board.get_board_size().y))
                elif command[0] == "fixed_handicap":
                    handicap_coords = fixed_handicap(int(command[1]))
                    if handicap_coords:
                        for coord in handicap_coords:
                            self.board.setup_move(go_data_gen.Move(
                                go_data_gen.Color.Black, coord))
                        print("= " + " ".join(coord_to_str(coord, self.board.get_board_size().y)
                                              for coord in handicap_coords))
                    else:
                        print("? invalid number of handicap stones")
                elif command[0] == "place_free_handicap":
                    num_stones = int(command[1])
                    handicap_coords = []
                    for i in range(num_stones):
                        # Use predict_move to generate a handicap stone placement with pass moves disabled
                        move, _ = predict_move(self.model, self.board, go_data_gen.Color.Black,
                                               device="cuda", temperature=self.temperature, allow_pass=False)
                        self.board.setup_move(move)
                        handicap_coords.append(move.coord)
                    print("= " + " ".join(coord_to_str(coord, self.board.get_board_size().y)
                                          for coord in handicap_coords))
                elif command[0] == "set_free_handicap":
                    for coord in command[1:]:
                        self.board.setup_move(go_data_gen.Move(
                            go_data_gen.Color.Black, str_to_coord(coord, self.board.get_board_size().y)))
                    print("=")
                elif command[0] == "showboard":
                    self.board.print()
                    print("=")
                else:
                    print("? unknown_command")
                    print("# {}".format(input_line))
            except KeyboardInterrupt:
                print("\n=\n")
                break
            except EOFError:
                break

    def stop(self):
        self.running = False


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Load a GoNet model and run the GoGTPEngine")
    parser.add_argument("checkpoint_path", type=str,
                        help="Path to the checkpoint file")
    parser.add_argument("--temperature", type=float, default=0.01,
                        help="Temperature for move sampling (0=deterministic, 1=raw probabilities)")
    args = parser.parse_args()

    device = "cuda" if torch.cuda.is_available() else "cpu"

    model = GoNet.load_from_checkpoint(
        checkpoint_path=args.checkpoint_path, device=device)[0]
    
    # Explicitly set model to evaluation mode
    model.eval()

    board = go_data_gen.Board()
    engine = GoGTPEngine(model, board, temperature=args.temperature)

    # Set up signal handler for graceful exit on Ctrl+C
    def signal_handler(sig, frame):
        engine.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT, signal_handler)

    try:
        engine.run()
    except KeyboardInterrupt:
        sys.exit(0)
