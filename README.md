# TERA Launcher for Linux

A community-created Linux launcher for TERA Online. This project is a **port** (in the loosest terms imaginable) of a closed-source TERA launcher ([original here](https://github.com/justkeepquiet/tera-launcher)) designed to work on retail servers. It utilizes the original launcher's graphical assets to provide a comparable experience for Linux users. By integrating Wine, it allows you to launch the TERA Online game client seamlessly on Linux distributions.

> **Disclaimer**: This launcher is in the it-will-probably-work-but-is-quirked-up phase of development. It will continue to see improvements -- probably! As this launcher depends on third party graphical assets and its target TERA server is baked in at compile time, we will _not_ be distributing binaries.

> ***Disclaimer but slantways***: This is **not** an officially supported launcher by the publishers of TERA Online. This software is provided for **educational and convenience purposes only**, and **users must respect TERA Online’s Terms of Service** and any other policies set by the game’s publishers.

---

## Table of Contents

- [Features](#features)
- [Dependencies and Requirements](#dependencies-and-requirements)
    - [Ubuntu/Debian-based](#ubuntudebian-based)
    - [Fedora/RHEL-based](#fedorarhel-based)
    - [Arch Linux](#arch-linux)
- [Building](#building)
    - [Configuring `launcher-config.json`](#configuring-launcher-configjson)
    - [Build Steps](#build-steps)
    - [Where the Binaries Go](#where-the-binaries-go)
- [Usage](#usage)
- [License](#license)
- [Disclaimer](#disclaimer)

---

## Features

- Seamlessly launches TERA Online on Linux via Wine (see **Usage** for details).
- Imports the original launcher's graphical assets to mimic its look and feel.
- Automatic game client patching and update checks.
- Modular build using CMake and subprojects for easy maintenance.

---

## Dependencies and Requirements

To build and run this launcher, you will need:

- **CMake** (version 3.16 or later).
- A **C compiler** (such as `gcc`) and build tools (like `make`).
- **winegcc** (for building the stub launcher component).
- **Python 3** (used by a custom asset-fetching script).
- **GTK4** development libraries.
- **libcurl** development libraries.
- **OpenSSL** development libraries.
- **SQLite3** development libraries.
- **jansson** development libraries.
- **libprotobuf-c** development libraries.
- **MiniXML** development libraries.
- Internet connection (for asset fetching).

This has been tested to build on:
- Ubuntu Noble (24.04 LTS). It will probably work on spins that use the same packages
- Fedora Workstation 41
- Arch Linux

There may be other factors outside the launcher that could influence whether the game 
launches successfully or not. You will want _at least_ the latest wine stable with 
winelib/winegcc to build the application. You can, as explained later on down below, 
specify a path to a custom wine build to choose a different version of wine with which 
to run the game (such as [GE-Proton](https://github.com/GloriousEggroll/proton-ge-custom) 
which you can acquire following their instructions or a helper app like 
[ProtonUp-Qt](https://davidotek.github.io/protonup-qt/)).

**Installing custom versions of wine/proton is beyond the scope of these instructions.**

### Ubuntu/Debian-based

```bash
sudo apt update
sudo apt install build-essential cmake wine libwine-dev \
                 python3 python3-pip python3-setuptools \
                 libgtk-4-dev libcurl4-openssl-dev libssl-dev \
                 libsqlite3-dev libjansson-dev libprotobuf-c-dev \
                 libmxml-dev pkg-config git
```

> Depending on your distribution, package names for Wine or some dependencies (like `libgtk-4-dev`) may vary.

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

> On Arch, development headers are typically included with the main packages, but ensure you have all necessary *-devel or similar if your config requires it. **Be advised that debug symbols are stripped from all arch official packages and are not included in official repos.** That means if you use valgrind for memchecks/debugging purposes on Arch you will need to go to the arch wiki and learn how to acquire debug symbol packages for some things like wine, glibc, glib, etc.

---

## Building

This project uses a top-level **CMakeLists.txt** that orchestrates the build of both the GUI launcher (native compilation) and a stub launcher (using winegcc). It also builds the [easylzma](https://github.com/justkeepquiet/easylzma) project as an external dependency.

### Why Winelib?
This launcher includes a GUI binary, a stub mimicking the original launcher for game compatibility, and binaries for updates and file repairs (easylzma). Separating the stub resolves threading issues with GTK's async callbacks. Using winelib to build the stub avoids requiring mingw/msys, leveraging Wine as a necessary project dependency.

### Configuring `launcher-config.json`

Before building, you **must** populate all values in the `launcher-config.json` file **except** `public_launcher_assets`, which is already filled in with the graphics needed by the launcher. This is because the build process will download these assets via the `fetch_launcher_assets.py` script and then embed them into the final launcher binary.

> The `public_launcher_assets` field contains a list of files to fetch (e.g., `bg.jpg`, `btn-auth.png`, etc.). Ensure all other fields (`auth_url`, `public_patch_url`, `public_launcher_assets_url`, and `service_name`) point to the server and assets that you want the launcher to use. The default settings for TERA Starscape are shown purely as an example.

### Build Steps

1. **Clone** this repository:
   ```bash
   git clone https://github.com/PopusBenedictus/tera-launcher-for-linux.git
   cd tera-launcher-for-linux
   ```

2. **Configure** your `launcher-config.json` accordingly. For example:
   ```json
   {
     "auth_url": "https://some.url/LauncherLoginAction",
     "public_patch_url": "https://some.url/public/patch",
     "public_launcher_assets_url": "https://some.url/public/launcher/images",
     "wine_prefix_name": ".prefix_folder_name_here",
     "game_lang": "EUR",
     "public_launcher_assets": [
       "bg.jpg",
       "bg.png",
       "btn-auth.png",
       "btn-close.png",
       "..."
     ],
     "service_name": "Your TERA Server"
   }
   ```

   Please note that `wine_prefix_name` _must_ be the name of a folder and may not contain any path separators. It will be created in the home folder of the user like the system `.wine` wineprefix typically would be.

3. **Create a build directory** and run CMake:
   ```bash
   mkdir build
   cd build
   cmake ..
   ```
   By default, CMake will detect your system’s native toolchain. The subproject that builds the stub uses `winegcc` automatically.

4. **Build** the project:
   ```bash
   cmake --build .
   ```
   This may take a while, especially the first time, because it downloads and builds dependencies like `easylzma`.

### Where the Binaries Go

After a successful build, the resulting binaries (including the main `tera_launcher_for_linux` executable and the Wine stub) will be placed in the `build/bin` directory by default.

You can copy or move these files into any directory where you want to keep your TERA client files. For example:

```bash
cp build/bin/* /path/to/your/tera_game_folder/
```
(If you're already in the `build` folder from earlier then replace `build/bin/*` with `bin/*`)

In that folder, you will typically have:

- `tera_launcher_for_linux` (the main Linux GUI launcher binary)
- `stub_launcher.exe` (the Wine-based stub)
- `stub_launcher.exe.so` (required for `stub_launcher.exe` to work, as `stub_launcher.exe` is a winelib binary)
- Both of the `easylzma` binaries

---

## Usage

Once you’ve placed the binaries in the same folder as your game client, simply run:

```bash
./tera_launcher_for_linux
```

The launcher will open a GTK-based GUI, fetch updates (if configured), and then allow you to start the game via Wine.

By default, the launcher uses the base, system version of Wine to launch the game and if it does not exist will use the `wine_prefix_name` value in `launcher-config.json` to create a new wine prefix in the users home folder. If you would like to specify the version of wine you would like to use, use the `TERA_CUSTOM_WINE_DIR` environment variable to specify the path that contains `/bin` and `/lib` folders containing the respective `wine`, `wineserver` and pertinent libraries.

It is **highly recommended** to install [DXVK](https://github.com/doitsujin/dxvk) in the wineprefix the launcher uses for optimal performance. You can prepare the wineprefix in advance like so:
```bash
WINEPREFIX="/home/USERNAME/WINE_PREFIX_FOLDER_NAME" wineboot
WINEPREFIX="/home/USERNAME/WINE_PREFIX_FOLDER_NAME" winetricks dxvk
```

---

## License

All code in this repo is subject to the WTFPL license. Please refer to `COPYING` and `COPYING.WTFPL` file for details.
---

## Disclaimer

**TERA Launcher for Linux** is **not endorsed by**, **affiliated with**, or **officially supported by** the publishers of TERA Online. The original proprietary Windows launcher assets remain the property of their respective owners. Use of the game client is subject to the **TERA Online Terms of Service** and **End User License Agreement**. By using this launcher, you agree to abide by all respective legal agreements.

This software is offered purely **for educational purposes** and to give Linux users a convenient way to launch the game. No game assets are kept with this repository. Any misuse or violation of the publisher’s policies is the sole responsibility of the end user.