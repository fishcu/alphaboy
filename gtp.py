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
    if vertex.lower() == 'pass':
        return None

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
    if coord is None:
        return "pass"

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
            'fixed_handicap', 'place_free_handicap', 'set_free_handicap', 'quit', 'showboard',
            'name', 'version', 'protocol_version', 'known_command'
        ]
        self.running = True

    def run(self):
        while self.running:
            try:
                input_line = input().strip()

                # Handle empty lines
                if not input_line:
                    continue

                if input_line == "quit":
                    print("=\n\n")
                    break

                command = input_line.split()
                cmd_name = command[0].lower()

                if cmd_name == "list_commands":
                    print("= " + " ".join(self.commands) + "\n\n")
                elif cmd_name == "known_command":
                    if len(command) < 2:
                        print("? known_command requires an argument\n\n")
                    else:
                        known = command[1].lower() in [cmd.lower()
                                                       for cmd in self.commands]
                        print(f"= {str(known).lower()}\n\n")
                elif cmd_name == "name":
                    print("= AlphaBoy\n\n")
                elif cmd_name == "version":
                    print("= 1.0\n\n")
                elif cmd_name == "protocol_version":
                    print("= 2\n\n")
                elif cmd_name == "boardsize":
                    if len(command) < 2:
                        print("? boardsize requires an argument\n\n")
                    else:
                        try:
                            size = int(command[1])
                            if size < 1 or size > 25:
                                print("? unacceptable size\n\n")
                            else:
                                self.board = go_data_gen.Board(
                                    go_data_gen.Vec2(size, size), self.board.komi)
                                print("=\n\n")
                        except ValueError:
                            print("? syntax error\n\n")
                elif cmd_name == "clear_board":
                    self.board.reset()
                    print("=\n\n")
                elif cmd_name == "komi":
                    if len(command) < 2:
                        print("? komi requires an argument\n\n")
                    else:
                        try:
                            self.board.komi = float(command[1])
                            print("=\n\n")
                        except ValueError:
                            print("? syntax error\n\n")
                elif cmd_name == "play":
                    if len(command) < 3:
                        print("? play requires two arguments\n\n")
                    else:
                        try:
                            color = str_to_color(command[1].lower())
                            vertex = command[2]

                            if vertex.lower() == "pass":
                                move = go_data_gen.Move(
                                    color, True, go_data_gen.Vec2(0, 0))
                                self.board.play(move)
                                print("=\n\n")
                            else:
                                coord = str_to_coord(
                                    vertex, self.board.get_board_size().y)
                                if coord is None:
                                    print("? invalid vertex\n\n")
                                elif not self.board.is_legal(go_data_gen.Move(color, False, coord)):
                                    print("? illegal move\n\n")
                                else:
                                    self.board.play(
                                        go_data_gen.Move(color, False, coord))
                                    print("=\n\n")
                        except ValueError as e:
                            print(f"? {str(e)}\n\n")
                elif cmd_name == "genmove":
                    if len(command) < 2:
                        print("? genmove requires an argument\n\n")
                    else:
                        try:
                            color = str_to_color(command[1].lower())
                            # Use predict_move to generate a move (ignore the value)
                            move, _ = predict_move(self.model, self.board, color, device="cuda",
                                                   temperature=self.temperature)
                            self.board.play(move)
                            vertex = move_to_str(
                                move, self.board.get_board_size().y)
                            print(f"= {vertex}\n\n")
                        except ValueError as e:
                            print(f"? {str(e)}\n\n")
                elif cmd_name == "fixed_handicap":
                    if len(command) < 2:
                        print("? fixed_handicap requires an argument\n\n")
                    else:
                        try:
                            num_stones = int(command[1])
                            handicap_coords = fixed_handicap(num_stones)
                            if handicap_coords:
                                vertices = []
                                for x, y in handicap_coords:
                                    coord = go_data_gen.Vec2(x, y)
                                    self.board.setup_move(go_data_gen.Move(
                                        go_data_gen.Color.Black, False, coord))
                                    vertices.append(coord_to_str(
                                        coord, self.board.get_board_size().y))
                                print(f"= {' '.join(vertices)}\n\n")
                            else:
                                print("? invalid number of handicap stones\n\n")
                        except ValueError:
                            print("? syntax error\n\n")
                elif cmd_name == "place_free_handicap":
                    if len(command) < 2:
                        print("? place_free_handicap requires an argument\n\n")
                    else:
                        try:
                            num_stones = int(command[1])
                            if num_stones < 2:
                                print("? invalid number of handicap stones\n\n")
                            else:
                                handicap_coords = []
                                for i in range(num_stones):
                                    # Use predict_move to generate a handicap stone placement with pass moves disabled
                                    move, _ = predict_move(self.model, self.board, go_data_gen.Color.Black,
                                                           device="cuda", temperature=self.temperature, allow_pass=False)
                                    self.board.setup_move(move)
                                    handicap_coords.append(move.coord)
                                vertices = [coord_to_str(
                                    coord, self.board.get_board_size().y) for coord in handicap_coords]
                                print(f"= {' '.join(vertices)}\n\n")
                        except ValueError:
                            print("? syntax error\n\n")
                elif cmd_name == "set_free_handicap":
                    if len(command) < 2:
                        print("? set_free_handicap requires at least one vertex\n\n")
                    else:
                        try:
                            for vertex in command[1:]:
                                coord = str_to_coord(
                                    vertex, self.board.get_board_size().y)
                                if coord is None:
                                    print("? invalid vertex\n\n")
                                    break
                                self.board.setup_move(go_data_gen.Move(
                                    go_data_gen.Color.Black, False, coord))
                            else:
                                print("=\n\n")
                        except ValueError as e:
                            print(f"? {str(e)}\n\n")
                elif cmd_name == "showboard":
                    self.board.print()
                    print("=\n\n")
                else:
                    print("? unknown command\n\n")
            except KeyboardInterrupt:
                print("\n=\n\n")
                break
            except EOFError:
                break
            except Exception as e:
                print(f"? internal error: {str(e)}\n\n")

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
