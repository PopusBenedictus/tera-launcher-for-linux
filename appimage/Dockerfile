# Dockerfile: Build environment for TERA Launcher AppImage
FROM ubuntu:24.04

# Install system packages and build dependencies
RUN apt update && DEBIAN_FRONTEND=noninteractive apt install -y \
    build-essential cmake wine libwine-dev squashfs-tools \
    python3 python3-pip python3-setuptools file cabextract p7zip-full unzip zstd \
    libgtk-4-dev libcurl4-openssl-dev libssl-dev python3-gi gir1.2-gtk-4.0 \
    libsqlite3-dev libjansson-dev libprotobuf-c-dev adwaita-icon-theme \
    libmxml-dev pkg-config git wget xz-utils gnome-themes-extra \
    libsecret-1-dev libsecret-1-0 libsecret-common libsecret-tools \
    libtorrent-rasterbar-dev libtorrent-rasterbar2.0t64 \
    libboost-filesystem-dev libboost-filesystem1.83.0 libarchive-tools \
    gsettings-desktop-schemas-dev gsettings-ubuntu-schemas xdg-desktop-portal-gtk \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
