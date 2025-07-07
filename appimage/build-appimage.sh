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
  mkdir -p "$BUILD_DIR" "$TOOLS_DIR" "$APPDIR/usr/bin" "$APPDIR/usr/lib"
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
  chmod +x winetricks; mv winetricks "$APPDIR/usr/bin/"

  log "Downloading linuxdeploy and GTK plugin"
  wget -q -O "$LINUXDEPLOY" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/$(basename $LINUXDEPLOY)"
  wget -q -O "$PLUGIN_GTK" "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh"
  chmod +x "$LINUXDEPLOY" "$PLUGIN_GTK"

  log "Downloading appimagetool"
  wget -q -O "$APPIMAGETOOL" "https://github.com/AppImage/AppImageKit/releases/download/continuous/$(basename $APPIMAGETOOL)"
  chmod +x "$APPIMAGETOOL"
}

#========================================
# Package initial AppImage
#========================================
package_appimage() {
  log "Packaging initial AppImage"
  LDP_EXTRACT="$BUILD_DIR/linuxdeploy-squashfs"
  rm -rf "$LDP_EXTRACT"; "$LINUXDEPLOY" --appimage-extract; mv squashfs-root "$LDP_EXTRACT"
  cp "$PLUGIN_GTK" "$LDP_EXTRACT/"
  mapfile -t execs < <(find "$APPDIR/usr/bin" -maxdepth 1 -type f -executable ! -name "stub_launcher.exe*" ! -name "winetricks" ! -name "7z*" ! -name "unzstd" ! -name "zstdcat" ! -name "zstdmt" ! -name "zstdless" ! -name "zstdgrep")
  args=(--appdir "$APPDIR" -d "$APPDIR/tera-launcher.desktop" -i "$APPDIR/tera-launcher.png" --plugin gtk --output appimage)
  for bin in "${execs[@]}"; do args+=( -e "$bin" ); done
  printf "Will bundle executables: %s\n" "${execs[*]}"
  pushd "$LDP_EXTRACT" >/dev/null; ./AppRun "${args[@]}"; GENERATED=$(ls *.AppImage | head -n1) || error "No AppImage generated"; mv "$GENERATED" "$BUILD_DIR/"; log "Moved $GENERATED to $BUILD_DIR"; popd >/dev/null; rm -rf "$LDP_EXTRACT"
}

#========================================
# Inject GE-Proton cleanly
#========================================
inject_ge_proton() {
  log "Downloading GE-Proton"
  cd "$BUILD_DIR"; wget -q -O "$GE_PROTON_TARBALL" "$GE_PROTON_URL"
  mkdir -p "$APPDIR/usr/lib/$GE_PROTON_DEST"
  tar -xf "$GE_PROTON_TARBALL" --strip-components=1 -C "$APPDIR/usr/lib/$GE_PROTON_DEST"; rm "$GE_PROTON_TARBALL"

  APPIMAGE="$(ls "$BUILD_DIR"/*-x86_64.AppImage | head -n1)"
  [[ -f "$APPIMAGE" ]] || error "No AppImage to inject"
  log "Injecting into $(basename "$APPIMAGE")"

  INJ_EXTRACT="$BUILD_DIR/inject-squashfs"
  rm -rf "$INJ_EXTRACT"; mkdir -p "$INJ_EXTRACT"
  (cd "$INJ_EXTRACT"; export APPIMAGE_EXTRACT_AND_RUN=1; "$APPIMAGE" --appimage-extract; unset APPIMAGE_EXTRACT_AND_RUN)
  cp -r "$APPDIR/usr/lib/$GE_PROTON_DEST" "$INJ_EXTRACT/squashfs-root/usr/lib/"

  # Use extracted appimagetool run to avoid FUSE
  AI_EXTRACT="$TOOLS_DIR/appimagetool-squashfs"
  rm -rf "$AI_EXTRACT"; "$APPIMAGETOOL" --appimage-extract; mv squashfs-root "$AI_EXTRACT"
  AI_RUN="$AI_EXTRACT/AppRun"

  log "Re-packing AppImage"
  ARCH=x86_64 "$AI_RUN" "$INJ_EXTRACT/squashfs-root" "$APPIMAGE"

  mv "$APPIMAGE" "$SRC_DIR/"
  log "Done: moved $(basename "$APPIMAGE") to $SRC_DIR"

  rm -rf "$INJ_EXTRACT"
}

#========================================
# Main
#========================================
main() {
  log "Settings: CLONE_REPO=$CLONE_REPO, BRANCH=$BRANCH, GE_PROTON_VERSION=$GE_PROTON_VERSION"
  prepare_dirs; prepare_source; build_launcher; copy_binaries_and_assets; download_tools; package_appimage; inject_ge_proton
}

main "$@"
