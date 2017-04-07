FROM hwcrypto-archlinux-base:latest
LABEL maintainer "arti.zirk@gmail.com"
VOLUME /artifacts
RUN useradd -m -G wheel -s /bin/bash --home-dir /build build
USER build
WORKDIR /build
CMD git clone https://github.com/hwcrypto/hwcrypto-native && cd hwcrypto-native && git checkout -qf ${TRAVIS_COMMIT:-master} && PKGDEST=/artifacts make
