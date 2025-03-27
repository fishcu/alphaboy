import torch.nn as nn
import torch

from .activation import Swish


class SEBlock(nn.Module):
    """
    Squeeze-and-Excitation Networks
    """

    def __init__(self, channels, reduction=16):
        super(SEBlock, self).__init__()
        self.fc1 = nn.Linear(channels, channels // reduction, bias=False)
        self.relu = nn.ReLU(inplace=True)
        self.fc2 = nn.Linear(channels // reduction, channels, bias=False)
        self.sigmoid = nn.Sigmoid()

    def forward(self, x, num_intersections):
        b, c, _, _ = x.size()
        # Custom pooling that normalizes by num_intersections for each sample in batch
        y = x.sum(dim=(2, 3)) / num_intersections.view(b, 1)
        y = self.fc1(y)
        y = self.relu(y)
        y = self.fc2(y)
        y = self.sigmoid(y)
        y = y.view(b, c, 1, 1)
        return x * y


class CBAMBlock(nn.Module):
    """
    CBAM: Convolutional Block Attention Module 
    """

    def __init__(self, channels, reduction=16, kernel_size=7):
        super(CBAMBlock, self).__init__()
        # Channel attention components
        self.mlp = nn.Sequential(
            nn.Conv2d(channels, channels // reduction, 1, bias=False),
            nn.ReLU(inplace=True),
            nn.Conv2d(channels // reduction, channels, 1, bias=False)
        )
        # Spatial attention components
        assert kernel_size in (3, 7), 'kernel size must be 3 or 7'
        padding = (kernel_size - 1) // 2
        self.spatial_conv = nn.Conv2d(
            2, 1, kernel_size, padding=padding, bias=False)
        self.sigmoid = nn.Sigmoid()

    def forward(self, x, num_intersections, on_board_mask):
        b, c, _, _ = x.shape

        # Channel attention
        avg_out = self.mlp(x.sum(dim=(2, 3)).view(
            b, c, 1, 1) / num_intersections.view(b, 1, 1, 1))

        # Create a copy of x and set masked values to -inf
        x_masked = x.clone()
        x_masked = torch.where(on_board_mask.unsqueeze(
            1) == 1.0, x_masked, torch.tensor(float('-inf'), device=x.device))
        max_out = self.mlp(torch.amax(x_masked, dim=(2, 3)).view(b, c, 1, 1))

        channel_att = self.sigmoid(avg_out + max_out)
        x = x * channel_att

        # Spatial attention
        avg_spatial = torch.sum(x, dim=1, keepdim=True) / c
        max_spatial, _ = torch.max(x, dim=1, keepdim=True)
        spatial_input = torch.cat([avg_spatial, max_spatial], dim=1)
        spatial_output = self.spatial_conv(
            spatial_input) * on_board_mask.unsqueeze(1)
        spatial_att = self.sigmoid(spatial_output)
        x = x * spatial_att

        return x


class ECABlock(nn.Module):
    """
    ECA-Net: Efficient Channel Attention for Deep Convolutional Neural Networks
    """

    def __init__(self, k=5):
        super(ECABlock, self).__init__()
        self.conv1d = nn.Conv1d(1, 1, kernel_size=k,
                                padding=(k - 1) // 2, bias=False)
        self.sigmoid = nn.Sigmoid()

    def forward(self, x, num_intersections):
        # Input shape: (batch_size, channels, height, width)
        b, _, _, _ = x.size()
        y = x.sum(dim=(2, 3)) / num_intersections.view(b, 1)
        # Reshape to (batch_size, 1, channels) for Conv1d
        y = y.unsqueeze(1)  # Shape: (batch_size, 1, channels)
        y = self.conv1d(y)  # Shape: (batch_size, 1, channels)
        # Reshape back to (batch_size, channels, 1, 1)
        y = y.squeeze(1).unsqueeze(-1).unsqueeze(-1)
        y = self.sigmoid(y)  # Shape: (batch_size, channels, 1, 1)
        # Shape: (batch_size, channels, height, width)
        return x * y


class CABlock(nn.Module):
    """
    Coordinate Attention for Efficient Mobile Network Design
    """

    def __init__(self, channels, reduction=32):
        super(CABlock, self).__init__()
        mip = max(8, channels // reduction)

        self.conv1 = nn.Conv2d(
            channels, mip, kernel_size=1, stride=1, padding=0)
        self.bn1 = nn.BatchNorm2d(mip)
        self.act = Swish()

        self.conv_h = nn.Conv2d(
            mip, channels, kernel_size=1, stride=1, padding=0)
        self.conv_w = nn.Conv2d(
            mip, channels, kernel_size=1, stride=1, padding=0)

    def forward(self, x, board_width, board_height):
        identity = x

        b, c, h, w = x.size()
        # Custom pooling with normalization by board dimensions
        x_h = torch.sum(x, dim=3, keepdim=True) / board_width.view(b, 1, 1, 1)
        x_w = torch.sum(x, dim=2, keepdim=True) / board_height.view(b, 1, 1, 1)
        x_w = x_w.permute(0, 1, 3, 2)

        y = torch.cat([x_h, x_w], dim=2)
        y = self.conv1(y)
        y = self.bn1(y)
        y = self.act(y)

        x_h, x_w = torch.split(y, [h, w], dim=2)
        x_w = x_w.permute(0, 1, 3, 2)

        a_h = self.conv_h(x_h).sigmoid()
        a_w = self.conv_w(x_w).sigmoid()

        out = identity * a_w * a_h

        return out


class SimAMBlock(torch.nn.Module):
    """
    SimAM: A Simple, Parameter-Free Attention Module for Convolutional Neural Networks
    """

    def __init__(self):
        super(SimAMBlock, self).__init__()

        self.activation = nn.Sigmoid()
        self.e_lambda = 1.0e-4

    def __repr__(self):
        s = self.__class__.__name__ + '('
        s += ('lambda=%f)' % self.e_lambda)
        return s

    @staticmethod
    def get_module_name():
        return "simam"

    def forward(self, x, num_intersections):

        b, _, _, _ = x.size()

        n = num_intersections - 1

        mu = x.sum(dim=(2, 3), keepdim=True) / \
            num_intersections.view(b, 1, 1, 1)
        diff = x - mu
        x_minus_mu_square = diff * diff
        denominator = 4 * \
            (x_minus_mu_square.sum(
                dim=[2, 3], keepdim=True) / n.view(b, 1, 1, 1) + self.e_lambda)
        y = x_minus_mu_square / denominator + 0.5

        return x * self.activation(y)


class EMABlock(nn.Module):
    """
    Efficient Multi-Scale Attention Module with Cross-Spatial Learning
    """

    def __init__(self, channels, factor=32):
        super(EMABlock, self).__init__()
        self.groups = factor
        assert channels // self.groups > 0
        self.softmax = nn.Softmax(-1)
        self.gn = nn.GroupNorm(channels // self.groups,
                               channels // self.groups)
        self.conv1x1 = nn.Conv2d(
            channels // self.groups, channels // self.groups, kernel_size=1, stride=1, padding=0)
        self.conv3x3 = nn.Conv2d(
            channels // self.groups, channels // self.groups, kernel_size=3, stride=1, padding=1)

    def forward(self, x, board_width, board_height, num_intersections):
        b, c, h, w = x.size()
        group_x = x.reshape(b * self.groups, c //
                            self.groups, h, w)  # b*g,c//g,h,w

        # Properly reshape board dimensions by unsqueezing and repeating
        b_width = board_width.unsqueeze(-1).unsqueeze(-1)  # [b, 1, 1]
        b_width = b_width.repeat_interleave(
            self.groups, dim=0)  # [b*groups, 1, 1]

        b_height = board_height.unsqueeze(-1).unsqueeze(-1)  # [b, 1, 1]
        b_height = b_height.repeat_interleave(
            self.groups, dim=0)  # [b*groups, 1, 1]

        # [b, 1, 1, 1]
        n_intersections = num_intersections.unsqueeze(
            -1).unsqueeze(-1).unsqueeze(-1)
        n_intersections = n_intersections.repeat_interleave(
            self.groups, dim=0)  # [b*groups, 1, 1, 1]

        # Use keepdim=True to maintain the dimension for proper permutation
        x_h = torch.sum(group_x, dim=3, keepdim=True) / \
            b_width.unsqueeze(-1)  # [b*g, c//g, h, 1]
        x_w = torch.sum(group_x, dim=2, keepdim=True) / \
            b_height.unsqueeze(-1)  # [b*g, c//g, 1, w]

        # Transpose x_w to the right shape for concatenation
        x_w = x_w.permute(0, 1, 3, 2)  # [b*g, c//g, w, 1]

        # Concatenate along height dimension
        hw = self.conv1x1(torch.cat([x_h, x_w], dim=2))  # [b*g, c//g, h+w, 1]

        # Split back to original shapes
        # [b*g, c//g, h, 1], [b*g, c//g, w, 1]
        x_h, x_w = torch.split(hw, [h, w], dim=2)

        # Permute x_w back to original orientation and ensure correct broadcasting
        x_w = x_w.permute(0, 1, 3, 2)  # [b*g, c//g, 1, w]

        x1 = self.gn(group_x * x_h.sigmoid() * x_w.sigmoid())
        x2 = self.conv3x3(group_x)

        # Global pooling with normalization
        x1_pooled = torch.sum(x1, dim=(2, 3), keepdim=True) / n_intersections
        x2_pooled = torch.sum(x2, dim=(2, 3), keepdim=True) / n_intersections

        x11 = self.softmax(x1_pooled.reshape(
            b * self.groups, -1, 1).permute(0, 2, 1))
        x12 = x2.reshape(b * self.groups, c //
                         self.groups, -1)  # b*g, c//g, hw
        x21 = self.softmax(x2_pooled.reshape(
            b * self.groups, -1, 1).permute(0, 2, 1))
        x22 = x1.reshape(b * self.groups, c //
                         self.groups, -1)  # b*g, c//g, hw

        weights = (torch.matmul(x11, x12) + torch.matmul(x21, x22)
                   ).reshape(b * self.groups, 1, h, w)
        return (group_x * weights.sigmoid()).reshape(b, c, h, w)


class ELABlock(nn.Module):
    """
    ELA: Efficient Local Attention for Deep Convolutional Neural Networks
    """

    def __init__(self, channels, kernel_size=7):
        super(ELABlock, self).__init__()
        self.pad = kernel_size // 2

        # Shared convolution for both pathways
        self.conv = nn.Conv1d(
            channels, channels, kernel_size=kernel_size,
            padding=self.pad, groups=channels // 2, bias=False
        )

        # Shared group normalization for both pathways
        self.gn = nn.GroupNorm(16, channels)
        self.sigmoid = nn.Sigmoid()

    def forward(self, x, board_width, board_height):
        b, c, h, w = x.size()

        # Horizontal attention (along height dimension)
        x_h = torch.sum(x, dim=3, keepdim=False) / \
            board_width.unsqueeze(1).unsqueeze(2)  # [b, c, h]
        x_h = self.conv(x_h)  # [b, c, h]
        x_h = self.gn(x_h)
        x_h = self.sigmoid(x_h)
        x_h = x_h.unsqueeze(-1)  # [b, c, h, 1]

        # Vertical attention (along width dimension)
        x_w = torch.sum(x, dim=2, keepdim=False) / \
            board_height.unsqueeze(1).unsqueeze(2)  # [b, c, w]
        x_w = self.conv(x_w)  # [b, c, w]
        x_w = self.gn(x_w)
        x_w = self.sigmoid(x_w)
        x_w = x_w.unsqueeze(2)  # [b, c, 1, w]

        # Apply attention
        return x * x_h * x_w


class EANBlock(nn.Module):
    def __init__(self, channels, groups=8, mode='l2'):
        """
        EAN: An Efficient Attention Module Guided by Normalization for Deep Neural Networks

        Args:
            channels (int): Number of input channels.
            groups (int): Number of groups for GroupNorm.
            mode (str): Normalization mode ('l2' or 'l1').
        """
        super(EANBlock, self).__init__()
        assert mode in ['l1', 'l2'], "mode must be 'l1' or 'l2'"

        self.groups = groups
        self.groupnorm = nn.GroupNorm(
            num_groups=channels // groups, num_channels=channels // groups, affine=True)
        self.activation = nn.Sigmoid()
        self.alpha = nn.Parameter(torch.zeros(1, channels // groups, 1, 1))
        self.delta = nn.Parameter(torch.zeros(1, channels // groups, 1, 1))
        self.epsilon = 1e-5
        self.mode = mode

    @staticmethod
    def get_module_name():
        return "ean"

    def forward(self, x):
        b, c, h, w = x.size()
        x = x.reshape(b * self.groups, -1, h, w)
        xs = self.groupnorm(x)

        weight = self.groupnorm.weight.view(1, -1, 1, 1)

        if self.mode == 'l2':
            weights = (weight.pow(2).mean(
                dim=1, keepdim=True) + self.epsilon).pow(0.5)
            norm = self.alpha * (weight / weights)
        elif self.mode == 'l1':
            weights = torch.abs(weight).mean(
                dim=1, keepdim=True) + self.epsilon
            norm = self.alpha * (weight / weights)

        out = x * self.activation(xs * norm + self.delta)
        out = out.view(b, -1, h, w)

        return out


class MECABlock(nn.Module):
    """
    ECA + max pooling
    """

    def __init__(self, k_size=7):
        super(MECABlock, self).__init__()

        # Using a 1D convolution with kernel size k
        # We'll apply it to the stacked pooling results
        self.conv = nn.Conv1d(
            in_channels=2,  # 2 for avg and max pooling
            out_channels=1,
            kernel_size=k_size,
            padding=(k_size - 1) // 2,
            bias=False
        )

        self.sigmoid = nn.Sigmoid()

    def forward(self, x, num_intersections, on_board_mask):
        # Get dimensions
        b, c, _, _ = x.shape

        # Channel attention
        # Compute average pooling with normalization by num_intersections
        # Sum over spatial dimensions and normalize to get [B, C]
        avg_out = x.sum(dim=(2, 3)) / num_intersections.view(b, 1)

        # Create a copy of x and set masked values to -inf for proper max pooling
        x_masked = x.clone()
        x_masked = torch.where(on_board_mask.unsqueeze(1) == 1.0,
                               x_masked,
                               torch.tensor(float('-inf'), device=x.device))

        # Compute max pooling over spatial dimensions to get [B, C]
        # torch.amax finds the maximum value across specified dimensions
        max_out = torch.amax(x_masked, dim=(2, 3))

        # Stack them along a new dimension
        y = torch.stack([avg_out, max_out], dim=1)  # [B, 2, C]

        # Apply 1D convolution
        y = self.conv(y)  # [B, 1, C]

        # Squeeze and apply sigmoid
        y = self.sigmoid(y.squeeze(1))  # [B, C]

        # Unsqueeze to match input dimensions for element-wise multiplication
        y = y.unsqueeze(-1).unsqueeze(-1)  # [B, C, 1, 1]

        # Apply the attention weights to the original input
        return x * y


class MVECABlock(nn.Module):
    """
    ECA + var, max pooling
    """

    def __init__(self, k_size=7):
        super(MVECABlock, self).__init__()

        # Using a 1D convolution with kernel size k
        # We'll apply it to the stacked pooling results (avg, var, max)
        self.conv = nn.Conv1d(
            in_channels=3,  # 3 for avg, var, and max pooling
            out_channels=1,
            kernel_size=k_size,
            padding=(k_size - 1) // 2,
            bias=False
        )

        self.sigmoid = nn.Sigmoid()

    def forward(self, x, num_intersections, on_board_mask):
        # Get dimensions
        b, c, _, _ = x.shape

        # Channel attention
        # Compute average pooling with normalization by num_intersections
        # Sum over spatial dimensions and normalize to get [B, C]
        avg_out = x.sum(dim=(2, 3)) / num_intersections.view(b, 1)

        # Create a copy of x and set masked values to -inf for proper max pooling
        x_masked = x.clone()
        x_masked = torch.where(on_board_mask.unsqueeze(1) == 1.0,
                               x_masked,
                               torch.tensor(float('-inf'), device=x.device))

        # Compute max pooling over spatial dimensions to get [B, C]
        max_out = torch.amax(x_masked, dim=(2, 3))

        # Compute variance over valid positions
        # Calculate mean for each channel (using avg_out)
        mean = avg_out.unsqueeze(-1).unsqueeze(-1)  # [B, C, 1, 1]

        # Calculate squared differences from mean only for valid positions
        squared_diff = torch.where(on_board_mask.unsqueeze(1) == 1.0,
                                   (x - mean) ** 2,
                                   torch.zeros_like(x))

        # Sum squared differences and normalize by num_intersections
        var_out = squared_diff.sum(dim=(2, 3)) / num_intersections.view(b, 1)

        # Stack all three statistics along a new dimension
        y = torch.stack([avg_out, var_out, max_out], dim=1)  # [B, 3, C]

        # Apply 1D convolution
        y = self.conv(y)  # [B, 1, C]

        # Squeeze and apply sigmoid
        y = self.sigmoid(y.squeeze(1))  # [B, C]

        # Unsqueeze to match input dimensions for element-wise multiplication
        y = y.unsqueeze(-1).unsqueeze(-1)  # [B, C, 1, 1]

        # Apply the attention weights to the original input
        return x * y


class MSECABlock(nn.Module):
    """
    ECA + std, max pooling
    """

    def __init__(self, k_size=7):
        super(MSECABlock, self).__init__()

        # Using a 1D convolution with kernel size k
        # We'll apply it to the stacked pooling results (avg, std, max)
        self.conv = nn.Conv1d(
            in_channels=3,  # 3 for avg, std, and max pooling
            out_channels=1,
            kernel_size=k_size,
            padding=(k_size - 1) // 2,
            bias=False
        )

        self.sigmoid = nn.Sigmoid()

    def forward(self, x, num_intersections, on_board_mask):
        # Get dimensions
        b, c, _, _ = x.shape

        # Channel attention
        # Compute average pooling with normalization by num_intersections
        # Sum over spatial dimensions and normalize to get [B, C]
        avg_out = x.sum(dim=(2, 3)) / num_intersections.view(b, 1)

        # Create a copy of x and set masked values to -inf for proper max pooling
        x_masked = x.clone()
        x_masked = torch.where(on_board_mask.unsqueeze(1) == 1.0,
                               x_masked,
                               torch.tensor(float('-inf'), device=x.device))

        # Compute max pooling over spatial dimensions to get [B, C]
        max_out = torch.amax(x_masked, dim=(2, 3))

        # Compute standard deviation over valid positions
        # Calculate mean for each channel (using avg_out)
        mean = avg_out.unsqueeze(-1).unsqueeze(-1)  # [B, C, 1, 1]

        # Calculate squared differences from mean only for valid positions
        squared_diff = torch.where(on_board_mask.unsqueeze(1) == 1.0,
                                   (x - mean) ** 2,
                                   torch.zeros_like(x))

        # Sum squared differences, normalize by num_intersections, and take square root for std
        std_out = torch.sqrt(squared_diff.sum(dim=(2, 3)) / num_intersections.view(b, 1))

        # Stack all three statistics along a new dimension
        y = torch.stack([avg_out, std_out, max_out], dim=1)  # [B, 3, C]

        # Apply 1D convolution
        y = self.conv(y)  # [B, 1, C]

        # Squeeze and apply sigmoid
        y = self.sigmoid(y.squeeze(1))  # [B, C]

        # Unsqueeze to match input dimensions for element-wise multiplication
        y = y.unsqueeze(-1).unsqueeze(-1)  # [B, C, 1, 1]

        # Apply the attention weights to the original input
        return x * y
