#!/usr/bin/env bash
set -euo pipefail

REPO_URL="${REPO_URL:-}" # e.g. "https://github.com/PopusBenedictus/tera-launcher-for-linux.git"
BRANCH="${BRANCH:-main}"
# If CLONE_REPO=1, we'll clone into SRC_DIR; otherwise we assume SRC_DIR="../"
CLONE_REPO="${CLONE_REPO:-0}"
GE_PROTON_VERSION="${GE_PROTON_VERSION:-GE-Proton10-7}"
GE_PROTON_TARBALL="${GE_PROTON_TARBALL:-${GE_PROTON_VERSION}.tar.gz}"
GE_PROTON_URL="${GE_PROTON_URL:-\
https://github.com/GloriousEggroll/proton-ge-custom/releases/download/\
${GE_PROTON_VERSION}/${GE_PROTON_TARBALL}}"
GE_PROTON_DEST="ge-proton"
LINUXDEPLOY="linuxdeploy-x86_64.AppImage"
PLUGIN_GTK_SH="linuxdeploy-plugin-gtk.sh"
PLUGIN_GTK_BIN="linuxdeploy-plugin-gtk"

echo "== Settings =="
echo " CLONE_REPO=$CLONE_REPO"
[[ "$CLONE_REPO" == "1" ]] && echo "  REPO_URL=$REPO_URL"
echo " BRANCH=$BRANCH"
echo " GE_PROTON_VERSION=$GE_PROTON_VERSION"
echo ""

# Use parent source directory if not cloning a repo, otherwise move into cloned repo.
if [[ "$CLONE_REPO" == "1" ]]; then
  if [[ -z "$REPO_URL" ]]; then
    echo "Error: CLONE_REPO=1 but REPO_URL is empty." >&2
    exit 1
  fi
  SRC_DIR="src-repo"
  rm -rf "$SRC_DIR"
  echo "Cloning $REPO_URL → $SRC_DIR"
  git clone "$REPO_URL" "$SRC_DIR"
else
  # Assume we're in <repo_root>/appimage, so parent (..) is repo root
  SRC_DIR="../"
  # Git might get fussy if the owner of the src directory differs from the container UID
  git config --global --add safe.directory /src
fi

if [[ ! -d "$SRC_DIR/.git" ]]; then
  echo "Error: No git repo found in '$SRC_DIR'." >&2
  exit 1
fi

cd "$SRC_DIR"
if [[ "$CLONE_REPO" == "1" ]]; then
    git fetch --all --prune
    git checkout "$BRANCH"
fi

# Build the launcher
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel "$(nproc)"
cd ..

# Prepare AppDir
APPDIR="$(pwd)/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"

# Copy binaries and assets
cp build/bin/* "$APPDIR/usr/bin/"
cp appimage/assets/tera-launcher.desktop "$APPDIR/tera-launcher.desktop"
cp appimage/assets/tera-launcher.png    "$APPDIR/tera-launcher.png"
cp appimage/assets/AppRun               "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"

echo "Fetching linuxdeploy"
wget -q -O "$LINUXDEPLOY" \
  "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/$LINUXDEPLOY"

echo "Fetching linuxdeploy-plugin-gtk script"
wget -q -O "$PLUGIN_GTK_SH" \
  "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh"

chmod +x "$LINUXDEPLOY" "$PLUGIN_GTK_SH"
mv "$PLUGIN_GTK_SH" "$PLUGIN_GTK_BIN"


echo "Packaging AppImage (with GTK plugin)"
./"$LINUXDEPLOY" --appimage-extract

# Specify target binaries to fetch dependent libraries for.
declare -a E_ARGS
E_ARGS+=( -e "$APPDIR/usr/bin/tera_launcher_for_linux" )
while IFS= read -r exe; do
  [[ -n "$exe" ]] && E_ARGS+=( -e "$exe" )
done < <(
  find "$APPDIR/usr/bin" -maxdepth 1 -type f -executable -name 'easylzma*'
)
echo "Will bundle executables:"
printf "  %s\n" "${E_ARGS[@]}"

# Perform first phase of AppImage setup.
./squashfs-root/AppRun \
    --appdir "$APPDIR" \
    -d "$APPDIR/tera-launcher.desktop" \
    -i "$APPDIR/tera-launcher.png" \
    "${E_ARGS[@]}" \
    --plugin gtk \
    --output appimage


# Download GE-Proton and then re-package the AppImage with it.
# This workaround is to avoid having to provide libraries for GE-Proton binaries
# as they include their own binaries already.
echo "Downloading GE-Proton: $GE_PROTON_URL"
wget -q -O "$GE_PROTON_TARBALL" "$GE_PROTON_URL"

mkdir -p "$APPDIR/usr/lib"
mkdir -p "$APPDIR/usr/lib/$GE_PROTON_DEST"
tar -xf "$GE_PROTON_TARBALL" --strip-components=1 -C "$APPDIR/usr/lib/$GE_PROTON_DEST"
rm "$GE_PROTON_TARBALL"

APPIMAGE="$(ls ./*-x86_64.AppImage | head -n1)"
echo "Injecting GE‑Proton into $APPIMAGE"

# You may notice this ENV get toggled on/off a few times
# This is a workaround for the absence of FUSE to be able to manipulate
# the squashfs filesystem without it.
export APPIMAGE_EXTRACT_AND_RUN=1
echo "Unpack Phase 1 TERA Launcher AppImage"
./"$APPIMAGE" --appimage-extract
unset APPIMAGE_EXTRACT_AND_RUN

if [[ ! -d squashfs-root ]]; then
  echo "ERROR: extraction failed; 'squashfs-root' not found" >&2
  exit 1
fi

echo "Copying GE‑Proton files"
cp -r "$APPDIR/usr/lib/ge-proton" squashfs-root/usr/lib/

echo "Fetching appimagetool"
APPIMAGETOOL="appimagetool-x86_64.AppImage"
wget -q -O "$APPIMAGETOOL" \
  "https://github.com/AppImage/AppImageKit/releases/download/continuous/$APPIMAGETOOL"
chmod +x "$APPIMAGETOOL"

# Re‑pack the AppImage
echo "Re‑packing AppImage (injecting GE‑Proton)"
export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH=x86_64
./"$APPIMAGETOOL" squashfs-root "$APPIMAGE"
unset APPIMAGE_EXTRACT_AND_RUN ARCH

echo "Done, AppImage Here: $APPIMAGE"
