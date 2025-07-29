# Build environment for TERA Launcher AppImage + wine-tkg

FROM ubuntu:24.04

# Frontload MinGW because wine-tkg always seems to have trouble finding it
RUN dpkg --add-architecture i386 \
 && apt update \
 && DEBIAN_FRONTEND=noninteractive apt upgrade -y \
 && DEBIAN_FRONTEND=noninteractive apt install -y \
    gcc-mingw-w64 g++-mingw-w64 git aptitude tzdata \
 && aptitude remove -y '?narrow(?installed,?version(deb.sury.org))'

# Build wine-tkg and move it to /opt/wine-tkg
RUN git clone https://github.com/Frogging-Family/wine-tkg-git.git \
 && cd wine-tkg-git/wine-tkg-git \
 && sed -i 's/_NOLIB32="false"/_NOLIB32="wow64"/' wine-tkg-profiles/advanced-customization.cfg \
 && sed -i 's/_nomakepkg_prefix_path=""/_nomakepkg_prefix_path="\/opt"/' customization.cfg \
 && echo '_ci_build="true"' >> customization.cfg \
 && yes | ./non-makepkg-build.sh \
 && mv /opt/wine-tkg-* /opt/wine-tkg \
 && ldconfig

# Install remaining dependencies for TERA Launcher
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
