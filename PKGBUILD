# Maintainer: FTEXX00 Linux driver contributors

pkgname=focaltech-ftexx00-dkms
pkgver=0.2
pkgrel=1
pkgdesc="DKMS SPI transport for FocalTech FTE3600/FTE4800/FTE6600/FTE6900 fingerprint readers"
arch=('any')
url="https://github.com/io-tl/fte3600-piccolo"
license=('GPL-2.0-only')
depends=('dkms')
optdepends=('linux-headers: build for the Arch Linux kernel'
            'linux-lts-headers: build for the Arch Linux LTS kernel')
provides=('focaltech-spi-dkms' 'focaltech-fte3600-piccolo-dkms')
conflicts=('focaltech-spi-dkms' 'focaltech-fte3600-piccolo-dkms')
options=('!strip' '!debug')
source=('focal_spi.c' 'focal_spi_uapi.h' 'Makefile' 'dkms.conf' 'LICENSE')
sha256sums=('d26f5fc70caca7d661a0dc808bef6ae8163f277143dcfe42b2f0d1facc1ee30a'
            'e59efac494f18560aceb8c1906751b0f868c0284d132abf4d6301de9d9c8c2b1'
            'd2b7813a3ddef556f14d6ffd76b8237701e57e57a17718e8bf0174b74a77b223'
            'fa30ce821a6c206f07493f65d55c33e7cf86e64ef7a7604572665bc78b19b301'
            'aaf135472f81c5b4a0dca9367e5bb5e9750032b5bebe5442b36e4c0a47430df3')

package() {
  local dkms_src="$pkgdir/usr/src/focaltech-ftexx00-$pkgver"

  install -Dm644 focal_spi.c "$dkms_src/focal_spi.c"
  install -Dm644 focal_spi_uapi.h "$dkms_src/focal_spi_uapi.h"
  install -Dm644 Makefile "$dkms_src/Makefile"
  install -Dm644 dkms.conf "$dkms_src/dkms.conf"
  install -Dm644 LICENSE \
    "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
