#!/bin/bash

# Default values
CONDA_ENV_NAME="base"
DEFAULT_TEMPERATURE=0.01

# Parse command line arguments
if [ $# -ge 1 ]; then
    CHECKPOINT_PATH="$1"
else
    echo "Error: Checkpoint path is required"
    echo "Usage: $0 <checkpoint_path> [temperature]"
    exit 1
fi

if [ $# -ge 2 ]; then
    TEMPERATURE="$2"
else
    TEMPERATURE="$DEFAULT_TEMPERATURE"
    echo "No temperature provided, using default: $TEMPERATURE"
fi

# Source bashrc to make conda available
if [ -f "$HOME/.bashrc" ]; then
    source "$HOME/.bashrc"
fi

# Activate conda environment
# Use the full path to conda if needed
if command -v conda &> /dev/null; then
    eval "$(conda shell.bash hook)"
    conda activate $CONDA_ENV_NAME
else
    # Try using the full path to conda if the command isn't found
    CONDA_PATH="$HOME/miniconda3/bin/conda"
    if [ -f "$CONDA_PATH" ]; then
        eval "$($CONDA_PATH shell.bash hook)"
        $CONDA_PATH activate $CONDA_ENV_NAME
    else
        echo "Conda not found. Please update the script with the correct path to conda."
        exit 1
    fi
fi

# Check if activation was successful
if [ $? -ne 0 ]; then
    echo "Failed to activate conda environment: $CONDA_ENV_NAME"
    exit 1
fi

# Run the GTP engine
python gtp.py "$CHECKPOINT_PATH" --temperature "$TEMPERATURE"

# Deactivate conda environment when done
conda deactivate
