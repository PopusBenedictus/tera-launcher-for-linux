#!/usr/bin/env python3
import sys
import json
import os
import urllib.request

# Pretend to be a user on Firefox accessing the webui.
CUSTOM_HEADERS = {
    "User-Agent": "Mozilla/5.0 (X11; Linux x86_64; rv:134.0) Gecko/20100101 Firefox/134.0",
    "Accept": "image/avif,image/webp,image/png,image/svg+xml,image/*;q=0.8,*/*;q=0.5",
    "Accept-Language": "en-US,en;q=0.5",
    "Accept-Encoding": "gzip, deflate",
    "Connection": "keep-alive",
    "Priority": "u=5"
}

def download_asset(url, dest_path):
    req = urllib.request.Request(url, headers=CUSTOM_HEADERS)
    with urllib.request.urlopen(req) as response:
        data = response.read()
        with open(dest_path, 'wb') as f:
            f.write(data)

def main():
    if len(sys.argv) != 3:
        print('Usage: fetch_launcher_assets.py <gtk-assets-output-dir> <assets-config-path>')
        sys.exit(1)

    assets_output_dir = sys.argv[1]
    config_path = sys.argv[2]

    # Ensure the output directory exists (or can be created)
    if not os.path.exists(assets_output_dir):
        try:
            os.makedirs(assets_output_dir)
        except OSError as e:
            print(f'Failed to create output directory {assets_output_dir}: {e}')
            sys.exit(1)

    # Try to load the launcher config
    try:
        with open(config_path, 'r') as f:
            config = json.load(f)
    except Exception as e:
        print(f'Failed to load {config_path}: {e}')
        sys.exit(1)

    base_url = config.get('public_launcher_assets_url')
    assets = config.get('public_launcher_assets', [])
    if not base_url or not isinstance(assets, list):
        print('Invalid configuration in launcher-config.json')
        sys.exit(1)

    # Download each asset if not already present
    for asset in assets:
        dest_path = os.path.join(assets_output_dir, asset)
        # Create any subdirectories for this asset
        os.makedirs(os.path.dirname(dest_path), exist_ok=True)

        if not os.path.exists(dest_path):
            url = base_url.rstrip('/') + '/' + asset
            print(f'Downloading {asset} from {url}...')
            try:
                download_asset(url, dest_path)
            except Exception as e:
                print(f'First attempt failed: {e}')
                # If the first attempt fails, try again with '/en/' appended to the base URL
                url = base_url.rstrip('/') + '/en/' + asset
                print(f'Trying again from {url}...')
                try:
                    download_asset(url, dest_path)
                except Exception as inner_e:
                    print(f'Failed to download {url}: {inner_e}')
                    sys.exit(1)

    print('All launcher assets are up-to-date.')

if __name__ == '__main__':
    main()
