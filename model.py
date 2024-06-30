# model.py

import torch
import torch.nn as nn


class GoNet(nn.Module):
    def __init__(self, input_channels, width=32, depth=8):
        super(GoNet, self).__init__()
        self.input_channels = input_channels
        self.width = width
        self.depth = depth

        # Input layer
        layers = [
            nn.Conv2d(input_channels, width,
                      kernel_size=3, stride=1, padding=1),
            nn.BatchNorm2d(width),
            nn.ReLU()
        ]

        # Hidden layers
        for _ in range(depth - 1):
            layers.extend([
                nn.Conv2d(width, width, kernel_size=3, stride=1, padding=1),
                nn.BatchNorm2d(width),
                nn.ReLU()
            ])

        # Output layer
        layers.append(nn.Conv2d(width, 1, kernel_size=1, stride=1, padding=0))

        self.network = nn.Sequential(*layers)

    def forward(self, x):
        self.train()  # Set to train mode
        x = self.network(x)
        x = x.view(x.size(0), -1)  # flatten
        policy = torch.softmax(x, dim=1)
        return policy

    def gen_move(self, x):
        self.eval()  # Set to eval mode
        with torch.no_grad():
            output = self(x)
            move = output.argmax(dim=1)

        return move


def count_parameters(model):
    total_params = 0
    trainable_params = 0
    for param in model.parameters():
        num_params = param.numel()
        total_params += num_params
        if param.requires_grad:
            trainable_params += num_params
    return total_params, trainable_params
