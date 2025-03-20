import requests
import tarfile
import os
import time
import shutil
from datetime import date, timedelta

# Set the base URLs and date range
games_base_url = "https://katagoarchive.org/kata1/traininggames/"
data_base_url = "https://katagoarchive.org/kata1/trainingdata/"
start_date = date(2025, 1, 4)
end_date = date(2025, 3, 17)

# Configuration
cooldown_time = 1  # seconds between requests
max_retries = 10  # maximum number of retry attempts
chunk_size = 8192  # download in chunks of 8KB

# Create directory to store the extracted files
script_dir = os.path.dirname(os.path.abspath(__file__))
output_dir = os.path.join(script_dir, "data")
os.makedirs(output_dir, exist_ok=True)


def download_file(url, file_path):
    """Download a file with progress checking and validation."""
    try:
        # Stream the download to handle large files
        with requests.get(url, stream=True) as response:
            response.raise_for_status()  # Raise exception for bad status codes

            # Get the expected content size if available
            total_size = int(response.headers.get('content-length', 0))

            with open(file_path, 'wb') as f:
                downloaded_size = 0
                for chunk in response.iter_content(chunk_size=chunk_size):
                    if chunk:
                        f.write(chunk)
                        downloaded_size += len(chunk)

            # Verify download size if content-length was provided
            if total_size > 0 and downloaded_size != total_size:
                raise Exception(
                    f"Download incomplete: Got {downloaded_size} bytes, expected {total_size}")

            return True
    except Exception as e:
        print(f"Download error: {str(e)}")
        # Clean up partial download
        if os.path.exists(file_path):
            os.remove(file_path)
        return False


def extract_archive(archive_path, extract_dir, archive_type):
    """Extract an archive file with validation, ignoring directory structure."""
    try:
        if archive_type == "tar.bz2":
            extraction_mode = "r:bz2"
        elif archive_type == "tgz":
            extraction_mode = "r:gz"
        else:
            raise Exception(f"Unsupported archive type: {archive_type}")

        with tarfile.open(archive_path, extraction_mode) as tar:
            # Check archive integrity
            if not all(tarinfo.name for tarinfo in tar.getmembers()):
                raise Exception("Corrupted archive detected")

            # Verify archive contains files
            member_count = len(tar.getmembers())
            if member_count == 0:
                raise Exception("No files found in archive")

            # Extract each file directly to the target directory, ignoring internal directory structure
            for member in tar.getmembers():
                if member.isfile():  # Skip directories, only extract files
                    # Extract only the filename without path
                    filename = os.path.basename(member.name)
                    source = tar.extractfile(member)
                    if source is not None:
                        # Create the target file path and write content
                        target_path = os.path.join(extract_dir, filename)
                        with open(target_path, "wb") as target:
                            target.write(source.read())

        return True
    except Exception as e:
        print(f"Extraction error: {str(e)}")
        return False


def process_file(base_url, date_str, file_suffix, file_type, extract_dir):
    """Process a single file download and extraction."""
    url = base_url + date_str + file_suffix
    file_name = date_str + file_suffix
    file_path = os.path.join(extract_dir, file_name)

    retry_count = 0
    success = False

    while retry_count < max_retries and not success:
        if retry_count > 0:
            print(f"Retry attempt {retry_count} for {file_name}")
            time.sleep(cooldown_time * 2)  # Extra wait time for retries

        # Download the file
        print(f"Downloading: {file_name}")
        download_success = download_file(url, file_path)

        if download_success:
            print(f"Download completed: {file_name}")

            # Extract the contents of the archive file
            print(f"Extracting: {file_name}")
            extract_success = extract_archive(
                file_path, extract_dir, file_type)

            if extract_success:
                print(f"Extraction completed: {file_name}")

                # Remove the downloaded archive file
                os.remove(file_path)
                success = True
                print(f"Successfully processed {file_name}")

        if not success:
            # Clean up any partial downloads
            if os.path.exists(file_path):
                os.remove(file_path)

            retry_count += 1
            if retry_count >= max_retries:
                print(
                    f"FAILED to process {file_name} after {max_retries} attempts")

    return success


# Iterate over the dates
current_date = start_date
while current_date <= end_date:
    date_str = current_date.strftime("%Y-%m-%d")
    date_output_dir = os.path.join(output_dir, date_str)
    os.makedirs(date_output_dir, exist_ok=True)

    # Process both file types
    games_success = process_file(
        games_base_url,
        date_str,
        "sgfs.tar.bz2",
        "tar.bz2",
        date_output_dir
    )

    data_success = process_file(
        data_base_url,
        date_str,
        "npzs.tgz",
        "tgz",
        date_output_dir
    )

    # Unified success handling - both must succeed
    if games_success and data_success:
        print(f"All files for {date_str} processed successfully")
    else:
        print(f"Failed to process all required files for {date_str}")
        # Clean up any partial successes if one failed
        if games_success or data_success:
            print(f"Cleaning up partial success for {date_str}")
            # Remove the entire date directory since we require both types
            shutil.rmtree(date_output_dir)

    # Increment the current date
    current_date += timedelta(days=1)

    # Add a cooldown timer between dates
    time.sleep(cooldown_time)

print("Script completed.")
