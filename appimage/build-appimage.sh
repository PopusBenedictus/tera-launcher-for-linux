#!/usr/bin/env bash
set -euo pipefail

#========================================
# Configuration (environment overrides)
#========================================
REPO_URL="${REPO_URL:-}"
BRANCH="${BRANCH:-main}"
CLONE_REPO="${CLONE_REPO:-0}"
GE_PROTON_VERSION="${GE_PROTON_VERSION:-GE-Proton10-7}"
GE_PROTON_TARBALL="${GE_PROTON_TARBALL:-${GE_PROTON_VERSION}.tar.gz}"
GE_PROTON_URL="${GE_PROTON_URL:-https://github.com/GloriousEggroll/proton-ge-custom/releases/download/${GE_PROTON_VERSION}/${GE_PROTON_TARBALL}}"
GE_PROTON_DEST="ge-proton"
SCRATCH_DIR="/scratch"
BUILD_DIR="${SCRATCH_DIR}/build"
APPDIR="$(pwd)/AppDir"
LINUXDEPLOY="linuxdeploy-x86_64.AppImage"
PLUGIN_GTK_SH="linuxdeploy-plugin-gtk.sh"
PLUGIN_GTK_BIN="linuxdeploy-plugin-gtk"
APPIMAGETOOL="appimagetool-x86_64.AppImage"

#========================================
# Helpers
#========================================
usage() {
  cat <<EOF
Usage: ${0##*/} [options]

Options:
  -h, --help            Show this help message and exit
  -c, --clone           Clone the repository (requires REPO_URL)
  -b, --branch BRANCH   Git branch to use (default: $BRANCH)
EOF
  exit 0
}

log() {
  printf "[INFO] %s\n" "$*"
}

error() {
  printf "[ERROR] %s\n" "$*" >&2
  exit 1
}

#========================================
# Argument parsing
#========================================
while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      ;;
    -c|--clone)
      CLONE_REPO=1
      shift
      ;;
    -b|--branch)
      BRANCH="${2:-}"
      shift 2
      ;;
    *)
      error "Unknown option: $1"
      ;;
  esac
done

#========================================
# Ensure dependencies
#========================================
dependencies=(git cmake wget tar)
for cmd in "${dependencies[@]}"; do
  command -v "$cmd" >/dev/null 2>&1 || error "Required command '$cmd' not found"
done

#========================================
# Prepare source
#========================================
prepare_source() {
  if [[ "$CLONE_REPO" == "1" ]]; then
    [[ -n "$REPO_URL" ]] || error "CLONE_REPO is set but REPO_URL is empty"
    SRC_DIR="src-repo"
    rm -rf "$SRC_DIR"
    log "Cloning $REPO_URL into $SRC_DIR"
    git clone "$REPO_URL" "$SRC_DIR"
    cd "$SRC_DIR"
    git fetch --all --prune
    git checkout "$BRANCH"
  else
    SRC_DIR=".."
    cd "$SRC_DIR"
    git config --global --add safe.directory /src || true
  fi
  [[ -d ".git" ]] || error "No git repository in $SRC_DIR"
  SRC_DIR="$(pwd)"
  log "Source directory: $SRC_DIR"
}

#========================================
# Build the launcher
#========================================
build_launcher() {
  log "Building launcher in $BUILD_DIR"
  rm -rf "$BUILD_DIR"
  mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"
  cmake "$SRC_DIR" -DCMAKE_BUILD_TYPE=Release
  cmake --build . --parallel "$(nproc)"
}

#========================================
# Set up AppDir structure
#========================================
prepare_appdir() {
  log "Preparing AppDir at $APPDIR"
  rm -rf "$APPDIR"
  mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib"
}

#========================================
# Copy binaries and assets
#========================================
copy_binaries_and_assets() {
  log "Copying launcher binaries and assets"
  cp "$BUILD_DIR/bin/"* "$APPDIR/usr/bin/"

  copy_tool() {
    local tool="$1"
    log "Including $tool"
    cp "$(command -v $tool)" "$APPDIR/usr/bin/"
  }

  for t in cabextract unzip 7z 7za 7zr sh bash; do
    copy_tool "$t"
  done
  cp -r /usr/lib/7zip "$APPDIR/usr/lib/"

  for asset in tera-launcher.desktop tera-launcher.png AppRun; do
    cp "$SRC_DIR/appimage/assets/$asset" "$APPDIR/$asset"
    [[ "$asset" == "AppRun" ]] && chmod +x "$APPDIR/AppRun"
  done
}

#========================================
# Download common tools
#========================================
download_tools() {
  log "Downloading Winetricks"
  wget -q -O winetricks \
    https://raw.githubusercontent.com/Winetricks/winetricks/master/src/winetricks
  chmod +x winetricks
  mv winetricks "$APPDIR/usr/bin/"

  log "Fetching linuxdeploy and GTK plugin"
  wget -q -O "$LINUXDEPLOY" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/$LINUXDEPLOY"
  wget -q -O "$PLUGIN_GTK_SH" \
    "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh"
  chmod +x "$LINUXDEPLOY" "$PLUGIN_GTK_SH"
  mv "$PLUGIN_GTK_SH" "$PLUGIN_GTK_BIN"
}

#========================================
# Package initial AppImage
#========================================
package_appimage() {
  log "Packaging initial AppImage"
  ./$LINUXDEPLOY --appimage-extract

  mapfile -t execs < <(
    find "$APPDIR/usr/bin" -maxdepth 1 -type f -executable \
      ! -name "stub_launcher.exe*" ! -name "winetricks" ! -name "7z*"
  )

  args=(--appdir "$APPDIR" -d "$APPDIR/tera-launcher.desktop"
        -i "$APPDIR/tera-launcher.png" --plugin gtk --output appimage)
  for bin in "${execs[@]}"; do
    args+=( -e "$bin" )
  done

  printf "Will bundle executables: %s\n" "${execs[*]}"
  ./squashfs-root/AppRun "${args[@]}"
}

#========================================
# Inject GE-Proton
#========================================
inject_ge_proton() {
  log "Downloading GE-Proton ($GE_PROTON_URL)"
  wget -q -O "$GE_PROTON_TARBALL" "$GE_PROTON_URL"

  mkdir -p "$APPDIR/usr/lib/$GE_PROTON_DEST"
  tar -xf "$GE_PROTON_TARBALL" --strip-components=1 -C "$APPDIR/usr/lib/$GE_PROTON_DEST"
  rm "$GE_PROTON_TARBALL"

  APPIMAGE_FILE=$(ls ./*-x86_64.AppImage | head -n1)
  log "Injecting GE-Proton into $APPIMAGE_FILE"

  export APPIMAGE_EXTRACT_AND_RUN=1
  ./"$APPIMAGE_FILE" --appimage-extract
  unset APPIMAGE_EXTRACT_AND_RUN

  [[ -d squashfs-root ]] || error "Extraction failed"
  cp -r "$APPDIR/usr/lib/$GE_PROTON_DEST" squashfs-root/usr/lib/

  log "Fetching appimagetool"
  wget -q -O "$APPIMAGETOOL" \
    "https://github.com/AppImage/AppImageKit/releases/download/continuous/$APPIMAGETOOL"
  chmod +x "$APPIMAGETOOL"

  log "Re-packing AppImage"
  export APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64
  ./$APPIMAGETOOL squashfs-root "$APPIMAGE_FILE"
  unset APPIMAGE_EXTRACT_AND_RUN ARCH

  mv "$APPIMAGE_FILE" "$SRC_DIR/"
  log "Done: $APPIMAGE_FILE moved to $SRC_DIR"
}

#========================================
# Main
#========================================
main() {
  log "Settings: CLONE_REPO=$CLONE_REPO, BRANCH=$BRANCH, GE_PROTON_VERSION=$GE_PROTON_VERSION"
  prepare_source
  build_launcher
  prepare_appdir
  copy_binaries_and_assets
  download_tools
  package_appimage
  inject_ge_proton
}

main "$@"
