import torch
import torch.nn as nn


class GlobalAvgPool2d(nn.Module):
    def forward(self, x, num_intersections):
        # Global average pooling over H,W dimensions, normalized by number of intersections
        b, c, _, _ = x.size()
        return x.sum(dim=(2, 3)) / num_intersections.view(b, 1)
