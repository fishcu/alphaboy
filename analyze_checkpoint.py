#!/usr/bin/env python3
"""
Analyze a GoNet checkpoint file to display network architecture information.
"""

import os
import argparse
import torch
import json
from pprint import pprint
import re
from collections import defaultdict

def analyze_checkpoint(checkpoint_path):
    """
    Analyze a checkpoint file and print information about the network architecture.
    
    Args:
        checkpoint_path: Path to the checkpoint file
    """
    print(f"Analyzing checkpoint: {checkpoint_path}")
    
    # Load checkpoint
    try:
        checkpoint = torch.load(checkpoint_path, map_location='cpu')
    except Exception as e:
        print(f"Error loading checkpoint: {e}")
        return
    
    print("\n=== Checkpoint Contents ===")
    # Print keys in the checkpoint
    print("Checkpoint keys:", list(checkpoint.keys()))
    
    # Print model configuration if available
    if 'model_config' in checkpoint:
        print("\n=== Model Configuration ===")
        model_config = checkpoint['model_config']
        print(f"Number of input planes: {model_config.get('num_input_planes', 'Unknown')}")
        print(f"Number of input features: {model_config.get('num_input_features', 'Unknown')}")
        print(f"Number of channels: {model_config.get('channels', 'Unknown')}")
        print(f"Number of blocks: {model_config.get('num_blocks', 'Unknown')}")
        print(f"C-head value: {model_config.get('c_head', 'Unknown')}")
    else:
        print("\nNo model_config found in checkpoint")
        # Try to infer some information from state dict
        print("Attempting to infer architecture from state_dict...")
        
    # Analyze state dict if available
    if 'model_state_dict' in checkpoint:
        state_dict = checkpoint['model_state_dict']
        print("\n=== State Dict Analysis ===")
        print(f"Number of parameters: {len(state_dict)}")
        total_params = sum(p.numel() for p in state_dict.values())
        print(f"Total parameters: {total_params:,}")
        
        # Count number of blocks by looking for block patterns in keys
        block_keys = [k for k in state_dict.keys() if 'blocks.' in k]
        if block_keys:
            block_indices = set()
            for key in block_keys:
                parts = key.split('.')
                if len(parts) >= 3 and parts[0] == 'blocks' and parts[1].isdigit():
                    block_indices.add(int(parts[1]))
            
            num_blocks = len(block_indices)
            print(f"Inferred number of blocks: {num_blocks}")
            
            # Try to identify block types
            block_types = set()
            for key in block_keys:
                if 'mobile' in key.lower():
                    block_types.add('MobileNetV2')
                elif 'shuffle' in key.lower():
                    block_types.add('ShuffleNetV2')
                elif 'ghost' in key.lower():
                    block_types.add('GhostBottleneck')
                elif 'resid' in key.lower() or 'skip' in key.lower():
                    block_types.add('Residual')
                elif 'attention' in key.lower():
                    block_types.add('Attention')
                elif 'se_' in key.lower():
                    block_types.add('Squeeze-Excitation')
            
            if block_types:
                print(f"Detected block types: {', '.join(block_types)}")
        
        # Look for input channels/features
        if 'input_process.spatial_conv.weight' in state_dict:
            shape = state_dict['input_process.spatial_conv.weight'].shape
            print(f"Input spatial channels: {shape[1]}")
            print(f"Output channels: {shape[0]}")
        
        if 'input_process.scalar_linear.weight' in state_dict:
            shape = state_dict['input_process.scalar_linear.weight'].shape
            print(f"Input scalar features: {shape[1]}")
        
        # Look for c_head value
        c_head_keys = [k for k in state_dict.keys() if 'pass_and_value_processing.0.weight' in k]
        if c_head_keys:
            shape = state_dict[c_head_keys[0]].shape
            print(f"C-head value: {shape[0]}")
            
        # Analyze attention blocks if present
        attention_keys = [k for k in state_dict.keys() if 'attention' in k]
        if attention_keys:
            print("\n=== Attention Block Analysis ===")
            # Group by block number
            block_pattern = re.compile(r'blocks\.(\d+)\.attention')
            attention_blocks = defaultdict(list)
            
            for key in attention_keys:
                match = block_pattern.search(key)
                if match:
                    block_num = match.group(1)
                    attention_blocks[block_num].append(key)
            
            for block_num, keys in sorted(attention_blocks.items(), key=lambda x: int(x[0])):
                print(f"\nAttention Block {block_num}:")
                
                # Analyze various attention components
                for key in sorted(keys):
                    tensor = state_dict[key]
                    shape_str = 'x'.join(str(dim) for dim in tensor.shape)
                    num_params = tensor.numel()
                    
                    # Extract component name (after 'attention.')
                    component_parts = key.split('.')
                    try:
                        attention_idx = component_parts.index('attention')
                        component_name = '.'.join(component_parts[attention_idx+1:])
                    except ValueError:
                        component_name = key
                    
                    print(f"  {component_name}: {shape_str} = {num_params:,} parameters")
                    
                    # Deeper analysis for specific components
                    if 'mlp' in key:
                        in_features = tensor.shape[1]
                        out_features = tensor.shape[0]
                        print(f"    MLP layer: input={in_features}, output={out_features}")
                    
                    if 'conv' in key and len(tensor.shape) >= 4:
                        out_channels, in_channels, kernel_h, kernel_w = tensor.shape
                        print(f"    Conv layer: in_channels={in_channels}, out_channels={out_channels}, "
                              f"kernel_size=({kernel_h}x{kernel_w})")
        
        # Analyze Squeeze-Excitation blocks
        se_keys = [k for k in state_dict.keys() if 'se_' in k.lower()]
        if se_keys:
            print("\n=== Squeeze-Excitation (SE) Block Analysis ===")
            block_pattern = re.compile(r'blocks\.(\d+)\.se_')
            se_blocks = defaultdict(list)
            
            for key in se_keys:
                match = block_pattern.search(key)
                if match:
                    block_num = match.group(1)
                    se_blocks[block_num].append(key)
            
            for block_num, keys in sorted(se_blocks.items(), key=lambda x: int(x[0])):
                print(f"\nSE Block {block_num}:")
                
                # Find related convolution or other parameters
                conv_keys = [k for k in state_dict.keys() if f'blocks.{block_num}.' in k and 'conv' in k]
                related_params = sum(state_dict[k].numel() for k in conv_keys if k not in se_keys)
                if related_params > 0:
                    print(f"  Related convolution parameters: {related_params:,}")
                
                # Analyze SE components
                for key in sorted(keys):
                    tensor = state_dict[key]
                    shape_str = 'x'.join(str(dim) for dim in tensor.shape)
                    num_params = tensor.numel()
                    
                    # Extract component name
                    component_parts = key.split('.')
                    try:
                        se_idx = next(i for i, part in enumerate(component_parts) if 'se_' in part.lower())
                        component_name = '.'.join(component_parts[se_idx:])
                    except (StopIteration, ValueError):
                        component_name = key
                    
                    print(f"  {component_name}: {shape_str} = {num_params:,} parameters")
                    
                    # Analyze FC layers
                    if 'fc' in key and len(tensor.shape) == 2:
                        in_features = tensor.shape[1]
                        out_features = tensor.shape[0]
                        reduction_ratio = in_features / out_features if 'fc1' in key else out_features / in_features
                        print(f"    FC layer: input={in_features}, output={out_features}, reduction_ratio={reduction_ratio:.1f}")
        
        # Look for spatial convolutions (might be the missing 98 parameters)
        spatial_conv_keys = [k for k in state_dict.keys() if 'spatial' in k.lower() and 'conv' in k.lower()]
        if spatial_conv_keys:
            print("\n=== Spatial Convolution Analysis ===")
            for key in sorted(spatial_conv_keys):
                tensor = state_dict[key]
                shape_str = 'x'.join(str(dim) for dim in tensor.shape)
                num_params = tensor.numel()
                print(f"{key}: {shape_str} = {num_params:,} parameters")
                if len(tensor.shape) >= 4:
                    out_channels, in_channels, kernel_h, kernel_w = tensor.shape
                    print(f"  Conv layer: in_channels={in_channels}, out_channels={out_channels}, "
                          f"kernel_size=({kernel_h}x{kernel_w})")
                    
    else:
        print("\nNo model_state_dict found in checkpoint")
    
    # Optimizer info
    if 'optimizer_config' in checkpoint:
        print("\n=== Optimizer Configuration ===")
        optimizer_config = checkpoint['optimizer_config']
        print(f"Type: {optimizer_config.get('type', 'Unknown')}")
        print(f"Learning rate: {optimizer_config.get('lr', 'Unknown')}")
        print(f"Weight decay: {optimizer_config.get('weight_decay', 'Unknown')}")
    
    # Scheduler info
    if 'scheduler_config' in checkpoint:
        print("\n=== Scheduler Configuration ===")
        scheduler_config = checkpoint['scheduler_config']
        print(f"Type: {scheduler_config.get('type', 'Unknown')}")
        print(f"T_max: {scheduler_config.get('T_max', 'Unknown')}")
        print(f"Eta min: {scheduler_config.get('eta_min', 'Unknown')}")
    
    # Training state
    if 'epoch' in checkpoint:
        print(f"\nTraining epoch: {checkpoint['epoch']}")
    
    if 'loss' in checkpoint:
        print(f"Last loss value: {checkpoint['loss']}")
    
    return checkpoint

def list_all_variables(checkpoint_path, filter_pattern=None):
    """
    List all variable names in the checkpoint state_dict with their shapes.
    
    Args:
        checkpoint_path: Path to the checkpoint file
        filter_pattern: Optional regex pattern to filter variable names
    """
    print(f"Listing all variables in: {checkpoint_path}")
    if filter_pattern:
        print(f"Using filter pattern: {filter_pattern}")
    
    try:
        checkpoint = torch.load(checkpoint_path, map_location='cpu')
    except Exception as e:
        print(f"Error loading checkpoint: {e}")
        return
    
    if 'model_state_dict' in checkpoint:
        state_dict = checkpoint['model_state_dict']
        print("\n=== All Variables ===")
        print(f"Total parameters: {len(state_dict)}")
        
        # Apply filter if provided
        if filter_pattern:
            pattern = re.compile(filter_pattern)
            filtered_keys = [k for k in state_dict.keys() if pattern.search(k)]
            filtered_params = sum(state_dict[k].numel() for k in filtered_keys)
            print(f"Filtered parameters: {len(filtered_keys)} (total: {filtered_params:,})")
            keys_to_display = filtered_keys
        else:
            keys_to_display = state_dict.keys()
            
        # Sort variables by name for easier reading
        for key in sorted(keys_to_display):
            tensor = state_dict[key]
            shape_str = 'x'.join(str(dim) for dim in tensor.shape)
            num_params = tensor.numel()
            print(f"{key}: shape={shape_str}, params={num_params:,}")
            
        # Calculate total parameter count
        total_params = sum(p.numel() for p in state_dict.values())
        print(f"\nTotal parameters in model: {total_params:,}")
    else:
        print("No model_state_dict found in checkpoint")

def main():
    parser = argparse.ArgumentParser(description='Analyze GoNet checkpoint')
    parser.add_argument('checkpoint_path', type=str, help='Path to checkpoint file')
    parser.add_argument('--detailed', action='store_true', 
                        help='Show detailed parameter count for each layer')
    parser.add_argument('--list-vars', action='store_true',
                        help='List all variables in the checkpoint')
    parser.add_argument('--filter', type=str, default=None,
                        help='Regex pattern to filter variable names when listing')
    parser.add_argument('--attention', action='store_true',
                        help='Focus analysis on attention blocks')
    args = parser.parse_args()
    
    if not os.path.exists(args.checkpoint_path):
        print(f"Error: Checkpoint file not found: {args.checkpoint_path}")
        return
    
    checkpoint = analyze_checkpoint(args.checkpoint_path)
    
    if args.list_vars:
        list_all_variables(args.checkpoint_path, args.filter)
    
    if args.detailed:
        print("\n=== Calculating detailed parameter counts... ===")
        try:
            from network import GoNet
            if 'model_config' in checkpoint:
                model = GoNet(**checkpoint['model_config'])
                model.load_state_dict(checkpoint['model_state_dict'], strict=False)
                
                print("\n=== Model Parameter Counts ===")
                for name, param in model.named_parameters():
                    print(f"{name}: {param.numel():,} parameters")
                
                total_params = sum(p.numel() for p in model.parameters())
                print(f"\nTotal parameters: {total_params:,}")
            else:
                print("Cannot create model: no model_config in checkpoint")
        except ImportError:
            print("Could not import GoNet class. Detailed analysis unavailable.")
        except Exception as e:
            print(f"Error during detailed analysis: {e}")

if __name__ == "__main__":
    main() 