import torch

import go_data_gen


def encode_input(board: go_data_gen.Board, to_play: go_data_gen.Color):
    # Get 2D feature planes and scalar features as numpy arrays
    stacked_maps, scalar_features = board.get_nn_input_data(to_play)
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


def encode_output(next_move: go_data_gen.Move, result: float):
    # Encode policy (next move)
    policy = torch.zeros(go_data_gen.Board.data_size,
                         go_data_gen.Board.data_size)
    # Pass is encoded just outside the board area, within the padded area.
    # Since the pass coordinate is (-1, -1), summing with the padding will work.
    policy[next_move.coord[1] + go_data_gen.Board.padding,
           next_move.coord[0] + go_data_gen.Board.padding] = 1.0

    # Encode value (game result)
    value = torch.tanh(torch.tensor([result]))
    if next_move.color == go_data_gen.Color.Black:
        value = -value

    assert policy.shape == (
        go_data_gen.Board.data_size, go_data_gen.Board.data_size)
    assert value.shape == (1,)

    return policy, value
