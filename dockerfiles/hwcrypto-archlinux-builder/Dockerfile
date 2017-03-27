FROM hwcrypto-archlinux-base:latest
LABEL maintainer "arti.zirk@gmail.com"
VOLUME /artifacts
RUN useradd -m -G wheel -s /bin/bash --home-dir /build build
USER build
WORKDIR /build
CMD git clone https://aur.archlinux.org/hwcrypto-native-git.git && cd hwcrypto-native-* && makepkg --syncdeps --noconfirm && cp hwcrypto-native-git-*.pkg.tar.xz /artifacts
