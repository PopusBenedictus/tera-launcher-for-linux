#!/usr/bin/env python3
import sys
import json
import os
import urllib.request

# Define the custom headers based on your provided example.
CUSTOM_HEADERS = {
    "User-Agent": "Mozilla/5.0 (X11; Linux x86_64; rv:134.0) Gecko/20100101 Firefox/134.0",
    "Accept": "image/avif,image/webp,image/png,image/svg+xml,image/*;q=0.8,*/*;q=0.5",
    "Accept-Language": "en-US,en;q=0.5",
    "Accept-Encoding": "gzip, deflate",
    "Connection": "keep-alive",
    "Priority": "u=5"
}

def download_asset(url, dest_path):
    """Helper function to download a file from a given URL using custom headers."""
    req = urllib.request.Request(url, headers=CUSTOM_HEADERS)
    with urllib.request.urlopen(req) as response:
        data = response.read()
        with open(dest_path, 'wb') as f:
            f.write(data)

def main():
    if len(sys.argv) != 2:
        print('Usage: fetch_launcher_assets.py <gtk-assets-dir>')
        sys.exit(1)

    assets_dir = sys.argv[1]
    config_path = os.path.join(assets_dir, 'launcher-config.json')

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

    for asset in assets:
        dest_path = os.path.join(assets_dir, asset)
        if not os.path.exists(dest_path):
            print(f'Downloading {asset} from {base_url.rstrip("/")}/{asset}...')
            url = base_url.rstrip('/') + '/' + asset
            try:
                download_asset(url, dest_path)
            except Exception as e:
                try:
                    # If the first attempt fails, try again with '/en/' appended to the base URL.
                    url = base_url.rstrip('/') + '/en/' + asset
                    print(f'Trying again from {url}...')
                    download_asset(url, dest_path)
                except Exception as inner_e:
                    print(f'Failed to download {url}: {inner_e}')
                    sys.exit(1)
    print('All launcher assets are up-to-date.')

if __name__ == '__main__':
    main()
