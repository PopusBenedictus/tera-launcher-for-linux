#!/usr/bin/env bash
set -euo pipefail

#========================================
# Configuration (environment overrides)
#========================================
REPO_URL="${REPO_URL:-}"
BRANCH="${BRANCH:-main}"
CLONE_REPO="${CLONE_REPO:-0}"
SCRATCH_DIR="/scratch"
BUILD_DIR="${SCRATCH_DIR}/build"
APPDIR="${SCRATCH_DIR}/AppDir"
TOOLS_DIR="${SCRATCH_DIR}/tools"

# External tooling paths (will live outside AppDir)
LINUXDEPLOY="${TOOLS_DIR}/linuxdeploy-x86_64.AppImage"
PLUGIN_GTK="${TOOLS_DIR}/linuxdeploy-plugin-gtk"
APPIMAGETOOL="${TOOLS_DIR}/appimagetool-x86_64.AppImage"

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

log() { printf "[INFO] %s\n" "$*"; }
error() { printf "[ERROR] %s\n" "$*" >&2; exit 1; }

#========================================
# Argument parsing
#========================================
while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage;;
    -c|--clone) CLONE_REPO=1; shift;;
    -b|--branch) BRANCH="${2:-}"; shift 2;;
    *) error "Unknown option: $1";;
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
# Prepare directories
#========================================
prepare_dirs() {
  log "Preparing build and tool directories"
  rm -rf "$BUILD_DIR" "$TOOLS_DIR"
  mkdir -p "$BUILD_DIR" \
  "$TOOLS_DIR" \
  "$APPDIR/usr/bin" \
  "$APPDIR/usr/lib" \
  "$APPDIR/usr/share/glib-2.0/schemas" \
  "$APPDIR/config/gtk-4.0" \
  "$APPDIR/opt"
}

#========================================
# Prepare source
#========================================
prepare_source() {
  if [[ "$CLONE_REPO" == "1" ]]; then
    [[ -n "$REPO_URL" ]] || error "CLONE_REPO is set but REPO_URL is empty"
    SRC_DIR="src-repo"; rm -rf "$SRC_DIR"
    log "Cloning $REPO_URL into $SRC_DIR"
    git clone "$REPO_URL" "$SRC_DIR"; cd "$SRC_DIR"
    git fetch --all --prune; git checkout "$BRANCH"
  else
    SRC_DIR=".."; cd "$SRC_DIR" || true
    git config --global --add safe.directory /src || true
  fi
  [[ -d ".git" ]] || error "No git repository in $SRC_DIR"
  SRC_DIR="$(pwd)"; log "Source directory: $SRC_DIR"
}

#========================================
# Build the launcher
#========================================
build_launcher() {
  log "Building launcher in $BUILD_DIR"
  cd "$BUILD_DIR"
  cmake "$SRC_DIR" -DCMAKE_BUILD_TYPE=Release
  cmake --build . --parallel "$(nproc)"
}

#========================================
# Copy launcher binaries and assets
#========================================
copy_binaries_and_assets() {
  log "Copying launcher binaries and assets"
  cp "$BUILD_DIR/bin/"* "$APPDIR/usr/bin/"
  cp -r "/opt/wine-tkg" "$APPDIR/opt/"
  echo "[Settings]" > "$APPDIR/config/gtk-4.0/settings.ini"
  copy_tool() { local tool="$1"; log "Including system tool: $tool"; cp "$(command -v $tool)" "$APPDIR/usr/bin/"; }
  for t in cabextract unzip bsdtar 7z 7za 7zr pzstd unzstd zstd zstdcat zstdgrep zstdless zstdmt sh bash; do copy_tool "$t"; done
  cp -r /usr/lib/7zip "$APPDIR/usr/lib/"
  for asset in tera-launcher.desktop tera-launcher.png AppRun; do
    cp "$SRC_DIR/appimage/assets/$asset" "$APPDIR/$asset"
    [[ "$asset" == "AppRun" ]] && chmod +x "$APPDIR/AppRun"
  done
}

#========================================
# Download external tools
#========================================
download_tools() {
  log "Downloading Winetricks into AppDir"
  wget -q -O winetricks https://raw.githubusercontent.com/Winetricks/winetricks/master/src/winetricks
  chmod +x winetricks
  mv winetricks "$APPDIR/usr/bin/"

  log "Downloading linuxdeploy"
  wget -q -O "$LINUXDEPLOY" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/$(basename "$LINUXDEPLOY")"
  chmod +x "$LINUXDEPLOY"

  log "Copying linuxdeploy-plugin-gtk from submodule"
  PLUGIN_GTK_SOURCE="$SRC_DIR/appimage/plugins/linuxdeploy-plugin-gtk/linuxdeploy-plugin-gtk.sh"
  if [[ ! -f "$PLUGIN_GTK_SOURCE" ]]; then
    error "Missing linuxdeploy-plugin-gtk submodule at $PLUGIN_GTK_SOURCE"
  fi
  cp "$PLUGIN_GTK_SOURCE" "$TOOLS_DIR/linuxdeploy-plugin-gtk"
  chmod +x "$TOOLS_DIR/linuxdeploy-plugin-gtk"

  log "Downloading appimagetool"
  wget -q -O "$APPIMAGETOOL" "https://github.com/AppImage/AppImageKit/releases/download/continuous/$(basename "$APPIMAGETOOL")"
  chmod +x "$APPIMAGETOOL"
}

#========================================
# Package initial AppImage
#========================================
package_appimage() {
  log "Packaging initial AppImage"
  LDP_EXTRACT="$BUILD_DIR/linuxdeploy-squashfs"
  rm -rf "$LDP_EXTRACT"
  "$LINUXDEPLOY" --appimage-extract
  mv squashfs-root "$LDP_EXTRACT"
  cp "$PLUGIN_GTK" "$LDP_EXTRACT/"

  # Collect executables from both the AppDir and the wine-tkg bin directory
  mapfile -t execs < <(find \
    "$APPDIR/usr/bin" \
    "$APPDIR/usr/local/bin" \
    "$APPDIR/opt/wine-tkg/bin" \
    -maxdepth 1 -type f -executable \
    -exec sh -c 'readelf -h "$1" >/dev/null 2>&1' _ {} \; \
    -print
  )

  args=(
    --appdir "$APPDIR"
    -d "$APPDIR/tera-launcher.desktop"
    -i "$APPDIR/tera-launcher.png"
    -l "/lib/x86_64-linux-gnu/libgpg-error.so.0"
    --plugin gtk
    --output appimage
  )

  for bin in "${execs[@]}"; do
    args+=( -e "$bin" )
  done

  printf "Will bundle executables: %s\n" "${execs[*]}"

  pushd "$LDP_EXTRACT" >/dev/null
    ./AppRun "${args[@]}"
    GENERATED=$(ls *.AppImage | head -n1) || error "No AppImage generated"
    mv "$GENERATED" "$SRC_DIR/"
    log "Done: moved $(basename "$GENERATED") to $SRC_DIR"
  popd >/dev/null

  rm -rf "$LDP_EXTRACT"
}

#========================================
# Main
#========================================
main() {
  log "Settings: CLONE_REPO=$CLONE_REPO, BRANCH=$BRANCH, GE_PROTON_VERSION=$GE_PROTON_VERSION"
  prepare_dirs; prepare_source; build_launcher; copy_binaries_and_assets; download_tools; package_appimage
}

main "$@"
