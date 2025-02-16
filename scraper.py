import requests
import tarfile
import os
import time
from datetime import date, timedelta

# Set the base URL and date range
base_url = "https://katagoarchive.org/kata1/traininggames/"
start_date = date(2025, 1, 1)
end_date = date(2025, 2, 13)

# Set the cooldown time in seconds between requests
cooldown_time = 1

# Create a directory to store the extracted files
script_dir = os.path.dirname(os.path.abspath(__file__))
output_dir = os.path.join(script_dir, "data")
os.makedirs(output_dir, exist_ok=True)

# Iterate over the dates in December 2023
current_date = start_date
while current_date <= end_date:
    # Construct the URL for the current date
    url = base_url + current_date.strftime("%Y-%m-%d") + "sgfs.tar.bz2"
    
    # Download the file
    response = requests.get(url)
    
    if response.status_code == 200:
        # Save the downloaded file
        file_name = current_date.strftime("%Y-%m-%d") + "sgfs.tar.bz2"
        file_path = os.path.join(output_dir, file_name)
        with open(file_path, "wb") as file:
            file.write(response.content)
        
        print(f"Downloaded: {file_name}")
        
        # Extract the contents of the tar.bz2 file
        with tarfile.open(file_path, "r:bz2") as tar:
            tar.extractall(output_dir)
        
        print(f"Extracted: {file_name}")
        
        # Remove the downloaded tar.bz2 file
        os.remove(file_path)
        
        print(f"Successfully downloaded and extracted files for {current_date}")
    else:
        print(f"Failed to download files for {current_date}")
    
    # Increment the current date
    current_date += timedelta(days=1)
    
    # Add a cooldown timer between requests
    time.sleep(cooldown_time)
