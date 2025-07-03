# TERA Launcher for Linux

A community-created Linux launcher for TERA Online. This project is a **port** (in the loosest terms imaginable) of a closed-source TERA launcher ([original here](https://github.com/justkeepquiet/tera-launcher)) designed to work on retail servers. It utilizes the original launcher's graphical assets to provide a comparable experience for Linux users. By integrating Wine, it allows you to launch the TERA Online game client seamlessly on various Linux distributions.

> **Note about support:** Use the *officially provided launcher for the TERA Online service you are using* **unless** it does not work on Linux. Likewise, do not direct support requests for this launcher to the maintainers of said service.
>
> This is maintained by a single person in their spare time. Expect delays if you inquire about bugs or ask questions, but I will respond as time allows.

> **Disclaimer:** This launcher is in the it-will-probably-work-but-is-quirked-up phase of development. It will continue to see improvements—probably! As this launcher depends on third-party graphical assets and its target TERA server is baked in at compile time, we will *not* be distributing binaries.

> **Not officially supported:** This is **not** an officially supported launcher by the publishers of TERA Online. This software is provided for **educational and convenience purposes only**, and **users must respect TERA Online’s Terms of Service** and any other policies set by the game’s publishers.

---

## Table of Contents

* [Features](#features)
* [Dependencies and Requirements](#dependencies-and-requirements)
* [Docker Installation](#docker-installation)
* [Building](#building)

    * [AppImage Mode (Recommended)](#appimage-mode-recommended)

        * [Optional Build Parameters](#optional-build-parameters)
    * [Standard Build (Option 2)](#standard-build-option-2)
* [Configuring ](#configuring-launcher-configjson)[`launcher-config.json`](#configuring-launcher-configjson)
* [Where the Binaries Go](#where-the-binaries-go)
* [Usage](#usage)
* [License](#license)
* [Disclaimer](#disclaimer)

---

## Features

* Seamlessly launches TERA Online on Linux via Wine (see **Usage** for details).
* Imports the original launcher's graphical assets to mimic its look and feel.
* Automatic game client patching and update checks.
* Modular build using CMake and subprojects for easy maintenance.

---

## Dependencies and Requirements

To build and run this launcher natively, you will need:

* **CMake** (version 3.16 or later)
* A **C compiler** (e.g., `gcc`) and build tools (such as `make`)
* **winegcc** (for building the stub-launcher component)
* **Python 3** (used by a custom asset-fetching script)
* **GTK4** development libraries
* **libcurl** development libraries
* **OpenSSL** development libraries
* **SQLite3** development libraries
* **jansson** development libraries
* **libprotobuf-c** development libraries
* **MiniXML** development libraries
* An internet connection (for asset fetching)

This has been tested to build on:

* Ubuntu 24.04 LTS
* Fedora 42 Workstation
* Arch Linux

> **Note:** If you only intend to build via **AppImage Mode**, you do **not** need to install these host dependencies locally — the Docker container provides all necessary tools and libraries.

### Ubuntu/Debian-based

```bash
sudo apt update
sudo apt install build-essential cmake wine libwine-dev \
                 python3 python3-pip python3-setuptools \
                 libgtk-4-dev libcurl4-openssl-dev libssl-dev \
                 libsqlite3-dev libjansson-dev libprotobuf-c-dev \
                 libmxml-dev pkg-config git
```

### Fedora/RHEL-based

```bash
sudo dnf install gcc gcc-c++ cmake make wine-devel \
                 python3 python3-pip python3-setuptools \
                 gtk4-devel libcurl-devel openssl-devel \
                 sqlite-devel jansson-devel protobuf-c-devel \
                 mxml-devel pkg-config git
```

### Arch Linux

```bash
sudo pacman -S base-devel cmake wine \
             python python-pip \
             gtk4 curl openssl sqlite jansson \
             protobuf-c mxml pkgconf git
```

---

## Docker Installation

If you be doing an AppImage build, you will need Docker. Follow the official instructions for your distribution:

* **Ubuntu 24.04**: [https://docs.docker.com/engine/install/ubuntu/](https://docs.docker.com/engine/install/ubuntu/)
* **Fedora**: [https://docs.docker.com/engine/install/fedora/](https://docs.docker.com/engine/install/fedora/)
* **Arch Linux**: [https://wiki.archlinux.org/title/Docker](https://wiki.archlinux.org/title/Docker)

I cannot offer support for installing or configuring Docker itself. **Use online documentation to figure out such things on your own.**

Support for **the build script** and **the build image** and build failures in an otherwise properly configured Docker configuration are however something I can support.

---

## Building

You can build this launcher in **two** ways. **AppImage Mode** is the **recommended** approach for cross‑distro convenience.

### AppImage Mode (Recommended)

> **Requirement:** The resulting AppImage will **not** work on Linux distros whose GLIBC < 2.38.

1. **Clone** the repository as usual:

   ```bash
   git clone https://github.com/PopusBenedictus/tera-launcher-for-linux.git
   cd tera-launcher-for-linux
   ```

2. **Enter** the AppImage build directory and build the Docker image:

   ```bash
   cd appimage
   docker build -t tera-builder .
   ```

3. **Generate** the AppImage:

   ```bash
   docker run --rm -it \
     -v "$(pwd)/..:/src" \
     -w /src/appimage \
     tera-builder bash -lc "./build-appimage.sh"
   ```

   After completion, you’ll find `TERA_Launcher_for_Linux-x86_64.AppImage` in the project root.

#### Optional Build Parameters

The `build-appimage.sh` script supports customization via environment variables:

* `REPO_URL` — Repository URL to clone (default: host mount)
* `BRANCH` — Git branch to build (default: `main`)
* `CLONE_REPO` — `1` to clone inside container instead of mounting (default: `0`)
* `GE_PROTON_VERSION` — Proton GE version (default: `GE-Proton10-7`)

**Example:** Build from branch `dev` using GE-Proton v10.8

```bash
docker run --rm -it \
  -v "$(pwd)/..:/src" \
  -w /src/appimage \
  -e BRANCH=dev \
  -e GE_PROTON_VERSION=GE-Proton10-8 \
  tera-builder bash -lc "./build-appimage.sh"
```

### Standard Build (Option 2)

Follow the original CMake-based instructions:

1. **Clone** and configure your `launcher-config.json`:

   ```bash
   git clone https://github.com/PopusBenedictus/tera-launcher-for-linux.git
   cd tera-launcher-for-linux
   # edit launcher-config.json as needed
   ```

2. **Create** a build directory and run CMake:

   ```bash
   mkdir build && cd build
   cmake ..
   # or with custom config:
   # cmake .. -DCUSTOM_CONFIG_PATH="/path/to/launcher-config.json"
   ```

3. **Build**:

   ```bash
   cmake --build .
   ```

---

## Configuring `launcher-config.json`

Before building, populate **all** values (except `public_launcher_assets`) in `launcher-config.json`:

```json
{
  "auth_url":              "https://some.url/LauncherLoginAction",
  "public_patch_url":      "https://some.url/public/patch",
  "public_launcher_assets_url": "https://some.url/public/launcher/images",
  "server_list_url":       "https://some.url/ServerList?lang=en",

  "wine_prefix_name":      ".config_prefix_root/wineprefix",
  "config_prefix_name":    ".config_prefix_root/config",
  "game_prefix_name":      ".config_prefix_root/files",
  "game_lang":             "EUR",

  "public_launcher_assets": [
    "bg.jpg",
    "btn-auth.png",
    "logo.png",
    "..."
  ],

  "service_name":          "Your TERA Server"
}
```

> **Note:** Outside AppImage Mode, `game_prefix_name` is ignored; `wine_prefix_name` and `config_prefix_name` are still honored. These are relative paths from your home directory.

---

## Where the Binaries Go

* **Standard build:** Binaries appear in `build/bin`. Copy them next to your game client:

  ```bash
  cp build/bin/* /path/to/TERA_game_folder/
  ```

* **AppImage build:** The single `.AppImage` contains everything. Make it executable and run:

  ```bash
  chmod +x TERA_Launcher_for_Linux-x86_64.AppImage
  ./TERA_Launcher_for_Linux-x86_64.AppImage
  ```

---

## Usage

* **Standard launcher:**

  ```bash
  ./tera_launcher_for_linux
  ```

* **AppImage launcher:**

  ```bash
  ./TERA_Launcher_for_Linux-x86_64.AppImage
  ```

You can customize the taskbar icon for the AppImage by replacing `appimage/assets/tera-launcher.png` with a 512×512 PNG of your choice. Do this before building the AppImage.

---

## License

All code in this repo is licensed under the WTFPL license. See `COPYING` and `COPYING.WTFPL` for details.

---

## Disclaimer

**TERA Launcher for Linux** is **not** endorsed by nor affiliated with the publishers of TERA Online. The original proprietary Windows launcher assets remain the property of their respective owners. Use of the game client is subject to the **TERA Online Terms of Service** and **End User License Agreement**. By using this launcher, you agree to abide by all respective legal agreements.

This software is offered purely **for educational purposes** and to give Linux users a convenient way to launch the game. Any misuse or violation of the publisher’s policies is the sole responsibility of the end user.
