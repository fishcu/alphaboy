import torch
import torch.nn as nn


class GlobalAvgPool2d(nn.Module):
    def forward(self, x):
        # Global average pooling over H,W dimensions
        return torch.mean(x, dim=(2, 3))
