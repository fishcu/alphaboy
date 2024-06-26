import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import TensorDataset, DataLoader
from torch.optim.lr_scheduler import StepLR

from datagen import GoDataGenerator
import go_data_gen
from model import GoNet, count_parameters


def main():
    torch.set_printoptions(linewidth=120)

    # Hyperparameters
    num_epochs = 800
    batch_size = 2**13
    learning_rate = 1.0e-4

    # Load data
    data_dir = "./data/"
    generator = GoDataGenerator(data_dir, debug=False)

    # Create model, loss, optimizer
    device = "cuda" if torch.cuda.is_available() else "cpu"
    model = GoNet(device=device, input_channels=go_data_gen.Board.num_feature_planes +
                  go_data_gen.Board.num_feature_scalars, width=32, depth=8)
    loss_fn = nn.CrossEntropyLoss()
    optimizer = optim.Adam(
        model.parameters(), lr=learning_rate, weight_decay=1e-5)

    # Count the parameters
    total_params, trainable_params = count_parameters(model)
    print(f"Total parameters: {total_params}")
    print(f"Trainable parameters: {trainable_params}")

    # Create the scheduler
    scheduler = StepLR(optimizer, step_size=100, gamma=0.5)

    # Training loop
    for epoch in range(num_epochs):
        print(f"Epoch [{epoch+1}/{num_epochs}]")

        # Generate training batch
        input_batch, policy_batch, _ = generator.generate_batch(batch_size)
        train_data = TensorDataset(input_batch, policy_batch)
        train_loader = DataLoader(
            train_data, batch_size=batch_size, shuffle=True)

        # Train on batch
        for inputs, labels in train_loader:
            inputs, labels = inputs.to(device), labels.to(device)

            outputs = model(inputs)

            outputs_flat = outputs.view(outputs.size(0), -1)
            labels_flat = labels.view(labels.size(0), -1)
            loss = loss_fn(outputs_flat, labels_flat)

            # Backpropagation
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()

            # Calculate accuracy
            correct = (outputs_flat.argmax(dim=1) ==
                       labels_flat.argmax(dim=1)).sum().item()
            accuracy = correct / labels.size(0)
            print(f"loss: {loss.item():>7f}  accuracy: {
                  100.0 * accuracy:.2f}%")

        # Validation
        input_batch, policy_batch, _ = generator.generate_batch(
            batch_size // 8)
        val_data = TensorDataset(input_batch, policy_batch)
        val_loader = DataLoader(val_data, batch_size=batch_size)

        correct = 0
        total = 0
        for inputs, labels in val_loader:
            inputs, labels = inputs.to(device), labels.to(device)

            outputs = model.forward_no_grad(inputs)

            outputs_flat = outputs.view(outputs.size(0), -1)
            labels_flat = labels.view(labels.size(0), -1)

            # Calculate accuracy
            correct += (outputs_flat.argmax(dim=1) ==
                        labels_flat.argmax(dim=1)).sum().item()
            total += labels.size(0)

        print(f'Validation Accuracy: {100 * correct / total:.2f}%')

        # Step the scheduler
        scheduler.step()
        print(f"Current learning rate: {scheduler.get_last_lr()[0]}")

        # Save checkpoint
        model.save_checkpoint(f'checkpoints/checkpoint_epoch_{epoch+1}.pth')

    print('Finished Training')


if __name__ == "__main__":
    main()
