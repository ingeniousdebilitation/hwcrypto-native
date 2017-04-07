# This is a Arch Linux package build script to be used with makepkg command
# Maintainer: Arti Zirk <arti.zirk@gmail.com>

pkgname=web-eid
pkgver=17.3.18
pkgrel=1
pkgdesc="Web eID native component"
arch=('x86_64' 'i686')
url="https://github.com/hwcrypto/hwcrypto-native"
license=('LGPL2.1')
depends=('qt5-base' 'pcsclite' 'ccid')
makedepends=('git' 'qt5-tools')
conflicts=('hwcrypto-native' 'web-eid')
replaces=('hwcrypto-native')
options=()

pkgver() {
    if [ -n "$VERSION" ]; then
        echo $VERSION
    else
        cd ..
        grep "VERSION=" VERSION.mk | tr "=" "\n" | tail -1
    fi
}

build() {
    cd ..
    make -C src
}

package() {
    cd ..

    mkdir -p $pkgdir/usr/bin
    install -p -m 755 src/web-eid $pkgdir/usr/bin

    mkdir -p $pkgdir/etc/opt/chrome/native-messaging-hosts
    install -p -m 644 linux/org.hwcrypto.native.json $pkgdir/etc/opt/chrome/native-messaging-hosts/org.hwcrypto.native.json

    mkdir -p $pkgdir/etc/chromium/native-messaging-hosts
    install -p -m 644 linux/org.hwcrypto.native.json $pkgdir/etc/chromium/native-messaging-hosts/org.hwcrypto.native.json

    mkdir -p $pkgdir/usr/lib/mozilla/native-messaging-hosts
    install -p -m 644 linux/org.hwcrypto.native.firefox.json $pkgdir/usr/lib/mozilla/native-messaging-hosts/org.hwcrypto.native.json
}
