#!/usr/bin/env python3
import sys
import json
import os
import urllib.request

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
                urllib.request.urlretrieve(url, dest_path)
            except Exception as e:
                print(f'Failed to download {url}: {e}')
                sys.exit(1)
    print('All launcher assets are up-to-date.')

if __name__ == '__main__':
    main()
