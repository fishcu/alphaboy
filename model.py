import torch
import torch.nn as nn

from io_conversions import *


class GoNet(nn.Module):
    def __init__(self, device, input_channels, width=32, depth=8):
        super(GoNet, self).__init__()
        self.device = device
        self.input_channels = input_channels
        self.width = width
        self.depth = depth

        self._create_network()

    def _create_network(self):
        # Input layer
        layers = [
            nn.Conv2d(self.input_channels, self.width,
                      kernel_size=3, stride=1, padding=1),
            nn.BatchNorm2d(self.width),
            nn.ReLU()
        ]

        # Hidden layers
        for _ in range(self.depth - 1):
            layers.extend([
                nn.Conv2d(self.width, self.width,
                          kernel_size=3, stride=1, padding=1),
                nn.BatchNorm2d(self.width),
                nn.ReLU()
            ])

        # Output layer
        layers.append(
            nn.Conv2d(self.width, 1, kernel_size=1, stride=1, padding=0))

        self.network = nn.Sequential(*layers).to(self.device)

    def forward(self, x):
        self.train()  # Set to train mode
        x = self.network(x.to(self.device))
        x = x.view(x.size(0), -1)  # flatten
        policy = torch.softmax(x, dim=1)
        return policy

    def forward_no_grad(self, x):
        self.eval()  # Set to eval mode
        with torch.no_grad():
            output = self(x.to(self.device))
        return output

    def gen_move(self, board: go_data_gen.Board, to_play: go_data_gen.Color):
        x = encode_input(board, to_play).unsqueeze(0).to(self.device)
        policy = self.forward_no_grad(x)

        # Reshape to (batch_size, data_size, data_size)
        policy = torch.reshape(
            policy, (go_data_gen.Board.data_size, go_data_gen.Board.data_size))

        # Apply legality map
        legal_map = board.get_legal_map(to_play)
        # print(legal_map)
        legal_policy = policy.cpu() * legal_map

        # Find best move
        flat_coord = torch.argmax(legal_policy)
        y, x = torch.unravel_index(flat_coord, legal_policy.shape)

        if (x, y) == go_data_gen.pass_coord:
            return go_data_gen.pass_coord

        x -= go_data_gen.Board.padding
        y -= go_data_gen.Board.padding

        return (x, y)

    @classmethod
    def load_from_checkpoint(cls, checkpoint_path, device):
        checkpoint = torch.load(checkpoint_path, map_location=device)

        # Load the model parameters directly from the checkpoint
        input_channels = checkpoint['input_channels']
        width = checkpoint['width']
        depth = checkpoint['depth']

        # Create a new instance of the model
        model = cls(device, input_channels, width, depth)

        # Load the state dict
        model.load_state_dict(checkpoint['model_state_dict'])

        return model

    def save_checkpoint(self, checkpoint_path):
        torch.save({
            'model_state_dict': self.state_dict(),
            'input_channels': self.input_channels,
            'width': self.width,
            'depth': self.depth
        }, checkpoint_path)


def count_parameters(model):
    total_params = 0
    trainable_params = 0
    for param in model.parameters():
        num_params = param.numel()
        total_params += num_params
        if param.requires_grad:
            trainable_params += num_params
    return total_params, trainable_params
